#include "VirtualPipesSolver.hpp"

#include "MathUtils.hpp"

#if PRS_PROFIL_NITI
#include "ThreadProfile.hpp"
#include <omp.h>
#endif

#include <algorithm>
#include <cmath>

namespace prs
{
    VirtualPipesSolver::VirtualPipesSolver(const WaterFlowParameters& parameters) : _p(parameters) {}

    double VirtualPipesSolver::addUniformWater(SimulationState& s, double depthMeters) const
    {
        if (depthMeters <= 0.0)
            return 0.0;
        double* d = s.water.data();
        const std::size_t n = s.cellCount();
        for (std::size_t i = 0; i < n; ++i)
            d[i] += depthMeters;
        return depthMeters * s.cellArea() * static_cast<double>(n);
    }

    double VirtualPipesSolver::pipeOutflow(double fOld, double terrainHere, double surfaceHere,
                                           double terrainNeighbor, double surfaceNeighbor, double gravityDt,
                                           double frictionCoefficient, double minDepth)
    {
        const double surfaceDifference = surfaceHere - surfaceNeighbor; // Δη
        if (fOld <= 0.0 && surfaceDifference <= 0.0)
            return 0.0; // nema toka i nema pada prema susjedu -> protok ostaje 0

        // Dubina toka na granici ćelija: voda iznad višeg od dva dna, do više od dvije vodene površine.
        const double flowDepth = std::max(surfaceHere, surfaceNeighbor) - std::max(terrainHere, terrainNeighbor);
        if (flowDepth <= minDepth)
            return 0.0;

        // Ubrzanje zbog razlike hidrostatičkog pritiska:  Δf = Δt · g · (l · h_f) · Δη / l
        double f = fOld + gravityDt * flowDepth * surfaceDifference;
        if (f <= 0.0)
            return 0.0; // cijev vodi samo VAN ćelije (izlazni protok nije negativan)

        // Manningovo trenje, poluimplicitno (stabilno i za vrlo plitku vodu):
        // dijeli se sa 1 + Δt g n² |f| / (l h_f^(7/3)); frictionCoefficient = Δt g n² / l.
        // (Imenilac je > 0, pa ne mijenja znak; za fOld = 0 jednak je 1.)
        if (frictionCoefficient > 0.0 && fOld > 0.0)
        {
            const double h73 = flowDepth * flowDepth * cubeRootPositive(flowDepth); // h^(7/3)
            // Algebarski isto kao  f /= 1 + c·fOld/h73, ali s JEDNIM dijeljenjem umjesto dva:
            //     f / (1 + c·fOld/h73)  =  f·h73 / (h73 + c·fOld)        (h73 > 0)
            f = f * h73 / (h73 + frictionCoefficient * fOld);
        }
        return f;
    }

    namespace
    {
        // Koliko uzastopnih ćelija čini blok kod preskakanja suhih dijelova mreže.
        // 64 × 8 B = 512 B, dakle provjera je nekoliko keš linija, a ušteda je cijeli blok računa.
        constexpr int kBlokCelija = 64;

        // Prekidač za mjerenje: -DPRS_PRESKOCI_SUHE=0 isključuje preskakanje suhih blokova.
#ifndef PRS_PRESKOCI_SUHE
#define PRS_PRESKOCI_SUHE 1
#endif

        // Ista formula kao VirtualPipesSolver::pipeOutflow, ali BEZ GRANANJA: umjesto tri ranija izlaza
        // (return 0.0) računa se sve, pa se na kraju bira vrijednost ili nula. Tako petlja nema skokova i
        // kompajler je može vektorizovati (NEON obrađuje dva double broja odjednom).
        //
        // Cijena: za cijevi koje bi "ispale" računa se i ono što se u originalu preskakalo. Isplati se jer
        // je nakon kiše većina ćelija mokra, pa je takvih cijevi malo.
        //
        // Jedina pažnja: kubni korijen traži pozitivan broj, pa se dubina toka prvo ograniči odozdo na
        // minDepth (> 0). Tamo gdje je dubina manja od toga rezultat se ionako odbacuje izborom na kraju.
        inline double pipeOutflowBezGrananja(double fOld, double bHere, double etaHere, double bNb, double etaNb,
                                             double gravityDt, double frictionCoefficient, double minDepth)
        {
            const double dEta = etaHere - etaNb;                                        // Δη
            const double h = std::max(etaHere, etaNb) - std::max(bHere, bNb);           // dubina toka
            const double hs = std::max(h, minDepth);                                    // sigurna za kubni korijen
            const double h73 = hs * hs * cubeRootPositive(hs);                          // h^(7/3)

            const double f0 = fOld + gravityDt * h * dEta;
            const double fTrenje = f0 * h73 / (h73 + frictionCoefficient * fOld);       // jedno dijeljenje
            const double f = (frictionCoefficient > 0.0 && fOld > 0.0) ? fTrenje : f0;

            // Uslovi iz originala: (a) ima toka ili pada, (b) dubina veća od praga, (c) protok pozitivan.
            const bool vrijedi = (fOld > 0.0 || dEta > 0.0) && (h > minDepth) && (f0 > 0.0);
            return vrijedi ? f : 0.0;
        }
    }

    void VirtualPipesSolver::computeOutflowFlux(SimulationState& s, double dt) const
    {
        const int W = s.width;
        const int H = s.height;
        const double area = s.cellArea();
        const double gravityDt = _p.gravity * dt;
        const double friction = gravityDt * _p.manningN * _p.manningN / s.cellSize;
        const double minDepth = _p.pipeMinDepth;
        const bool open = _p.boundary == BoundaryCondition::Open;

        const double* b = s.terrain.data();
        const double* d = s.water.data();
        double* d1 = s.waterBeforeFlow.data();
        double* fL = s.fluxLeft.data();
        double* fR = s.fluxRight.data();
        double* fT = s.fluxTop.data();
        double* fB = s.fluxBottom.data();

        // Susjed izvan mreže kod otvorenog ruba: isto dno kao ćelija, bez vode (slobodan izliv).
        // Kod zatvorenog ruba cijev ne postoji (zid) -> protok 0.
        auto boundaryPipe = [&](double fOld, double bi, double etai) {
            return open ? pipeOutflow(fOld, bi, etai, bi, bi, gravityDt, friction, minDepth) : 0.0;
        };

        // Jedna ćelija SA provjerama ruba. Poslije razdvajanja petlje poziva se samo za ćelije na rubu
        // mreže; na 736 × 736 to je 2940 od 541 696 ćelija, dakle 0,5 %.
        auto celijaNaRubu = [&](int x, int y) {
            {
                const std::size_t i = s.water.index(x, y);
                const double di = d[i];
                d1[i] = di;
                if (di <= 0.0)
                {
                    fL[i] = fR[i] = fT[i] = fB[i] = 0.0; // suha ćelija nema šta poslati
                    return;
                }
                const double bi = b[i];
                const double etai = bi + di; // visina vodene površine η = b + d

                const double left = (x > 0)
                    ? pipeOutflow(fL[i], bi, etai, b[i - 1], b[i - 1] + d[i - 1], gravityDt, friction, minDepth)
                    : boundaryPipe(fL[i], bi, etai);
                const double right = (x < W - 1)
                    ? pipeOutflow(fR[i], bi, etai, b[i + 1], b[i + 1] + d[i + 1], gravityDt, friction, minDepth)
                    : boundaryPipe(fR[i], bi, etai);
                const double top = (y > 0)
                    ? pipeOutflow(fT[i], bi, etai, b[i - W], b[i - W] + d[i - W], gravityDt, friction, minDepth)
                    : boundaryPipe(fT[i], bi, etai);
                const double bottom = (y < H - 1)
                    ? pipeOutflow(fB[i], bi, etai, b[i + W], b[i + W] + d[i + W], gravityDt, friction, minDepth)
                    : boundaryPipe(fB[i], bi, etai);

                // Mei 2007, jed. 4: odljev u koraku ne smije biti veći od vode u ćeliji.
                const double outVolume = (left + right + top + bottom) * dt;
                const double waterVolume = di * area;
                const double k = (outVolume > waterVolume) ? waterVolume / outVolume : 1.0;

                fL[i] = left * k;
                fR[i] = right * k;
                fT[i] = top * k;
                fB[i] = bottom * k;
            }
        };

        // Redoslijed obilaska ne utiče na rezultat: u ovoj fazi svaka ćelija piše SAMO svoje protoke i d1,
        // a od susjeda čita b i d, koji se ovdje ne mijenjaju.
        if (W < 3 || H < 3) // preuska mreža nema unutrašnjost
        {
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                    celijaNaRubu(x, y);
            return;
        }

        for (int x = 0; x < W; ++x) // gornji i donji red
        {
            celijaNaRubu(x, 0);
            celijaNaRubu(x, H - 1);
        }

        // Red po red nezavisno: ćelija piše samo svoje protoke, a od susjeda čita b i d koji se ne mijenjaju.
        // Petlja je pisana razdvojeno (`parallel` + `for`) jer je to isto kao `parallel for`, a omogućava
        // da se u mjernom buildu (-DPRS_PROFIL_NITI=1) zabilježi kada je koja nit završila svoj dio.
#pragma omp parallel if (s.cellCount() >= kMinCelijaZaParalelizaciju)
        {
#if PRS_PROFIL_NITI
            const double tPocetak = omp_get_wtime();
#pragma omp for schedule(guided) nowait
#else
#pragma omp for schedule(guided)
#endif
        for (int y = 1; y < H - 1; ++y)
        {
            celijaNaRubu(0, y);     // lijeva kolona
            celijaNaRubu(W - 1, y); // desna kolona

            // UNUTRAŠNJOST: susjedi i-1, i+1, i-W, i+W uvijek postoje, pa otpadaju četiri provjere
            // ruba po ćeliji (x > 0, x < W-1, y > 0, y < H-1). Ostaje samo provjera suhe ćelije.
            const std::size_t red = static_cast<std::size_t>(y) * static_cast<std::size_t>(W);

            // PRESKAKANJE SUHIH BLOKOVA (ideja: Khrapov i Khoperskov 2020, tamo nad blokovima mreže).
            // Ćelija bez vode ne može ništa poslati, pa su sve njene četiri cijevi nula. Ako u cijelom
            // bloku nema vode, preskače se sav račun i samo se upišu nule. Provjera ima rani izlaz, pa
            // na mokrom bloku staje već na prvoj ćeliji s vodom.
            for (int x0 = 1; x0 < W - 1; x0 += kBlokCelija)
            {
                const int x1 = std::min(x0 + kBlokCelija, W - 1);
                bool imaVode = true;
#if PRS_PRESKOCI_SUHE
                imaVode = false;
                for (int x = x0; x < x1 && !imaVode; ++x)
                    imaVode = d[red + static_cast<std::size_t>(x)] > 0.0;
#endif
                if (!imaVode)
                {
                    for (int x = x0; x < x1; ++x)
                    {
                        const std::size_t i = red + static_cast<std::size_t>(x);
                        d1[i] = d[i]; // = 0
                        fL[i] = fR[i] = fT[i] = fB[i] = 0.0;
                    }
                    continue;
                }
#pragma omp simd
            for (int x = x0; x < x1; ++x)
            {
                const std::size_t i = red + static_cast<std::size_t>(x);
                const double di = d[i];
                d1[i] = di;
                const double bi = b[i];
                const double etai = bi + di;

                const double left = pipeOutflowBezGrananja(fL[i], bi, etai, b[i - 1], b[i - 1] + d[i - 1],
                                                           gravityDt, friction, minDepth);
                const double right = pipeOutflowBezGrananja(fR[i], bi, etai, b[i + 1], b[i + 1] + d[i + 1],
                                                            gravityDt, friction, minDepth);
                const double top = pipeOutflowBezGrananja(fT[i], bi, etai, b[i - W], b[i - W] + d[i - W],
                                                          gravityDt, friction, minDepth);
                const double bottom = pipeOutflowBezGrananja(fB[i], bi, etai, b[i + W], b[i + W] + d[i + W],
                                                             gravityDt, friction, minDepth);

                const double outVolume = (left + right + top + bottom) * dt;
                const double waterVolume = di * area;
                const double k = (outVolume > waterVolume) ? waterVolume / outVolume : 1.0;

                // Suha ćelija nema šta poslati: umjesto `continue` (koji bi spriječio vektorizaciju)
                // bira se nula. Rezultat je isti kao u originalu.
                const bool mokra = di > 0.0;
                fL[i] = mokra ? left * k : 0.0;
                fR[i] = mokra ? right * k : 0.0;
                fT[i] = mokra ? top * k : 0.0;
                fB[i] = mokra ? bottom * k : 0.0;
            }
            }
        }
#if PRS_PROFIL_NITI
            // Vrijeme PRIJE barijere: ovdje je nit gotova sa svojim redovima, ali ostale možda nisu.
            profil::zabiljezi(omp_get_thread_num(), tPocetak, omp_get_wtime());
#pragma omp barrier
#endif
        }
    }

    WaterStepStats VirtualPipesSolver::updateWaterAndVelocity(SimulationState& s, double dt) const
    {
        const int W = s.width;
        const int H = s.height;
        const double l = s.cellSize;
        const double area = s.cellArea();
        const double dtOverArea = dt / area;
        const double g = _p.gravity;
        const double minDepth = _p.velocityMinDepth;

        const double* d1 = s.waterBeforeFlow.data();
        const double* fL = s.fluxLeft.data();
        const double* fR = s.fluxRight.data();
        const double* fT = s.fluxTop.data();
        const double* fB = s.fluxBottom.data();
        double* d = s.water.data();
        double* u = s.velocityX.data();
        double* v = s.velocityY.data();
        double* dMax = s.maxWaterDepth.data();

        WaterStepStats st;
        double boundaryOut = 0.0; // [m³/s], pomnoži se sa dt na kraju
        // Lokalne promjenjive za redukcije (OpenMP ne dozvoljava redukciju po članu strukture).
        double negCorrection = 0.0, maxWave = 0.0, maxSpeed2 = 0.0, maxDepth = 0.0;

#pragma omp parallel for schedule(guided) if (s.cellCount() >= kMinCelijaZaParalelizaciju) \
    reduction(+ : boundaryOut, negCorrection) reduction(max : maxWave, maxSpeed2, maxDepth)
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                const std::size_t i = s.water.index(x, y);
                const double outL = fL[i], outR = fR[i], outT = fT[i], outB = fB[i];
                // Ulaz = ono što susjedi šalju PREMA ovoj ćeliji.
                const double inL = (x > 0) ? fR[i - 1] : 0.0;     // lijevi susjed šalje udesno
                const double inR = (x < W - 1) ? fL[i + 1] : 0.0; // desni susjed šalje ulijevo
                const double inT = (y > 0) ? fB[i - W] : 0.0;     // gornji susjed šalje nadolje
                const double inB = (y < H - 1) ? fT[i + W] : 0.0; // donji susjed šalje nagore

                // Protok kroz rubne cijevi napušta mrežu (kod zatvorenog ruba je uvijek 0).
                if (x == 0) boundaryOut += outL;
                if (x == W - 1) boundaryOut += outR;
                if (y == 0) boundaryOut += outT;
                if (y == H - 1) boundaryOut += outB;

                const double before = d1[i];
                double after = before + dtOverArea * ((inL + inR + inT + inB) - (outL + outR + outT + outB));
                if (after < 0.0)
                {
                    negCorrection += -after * area;
                    after = 0.0;
                }
                d[i] = after;
                if (after > dMax[i])
                    dMax[i] = after;

                // Brzina iz neto protoka kroz ćeliju i srednje dubine u koraku.
                const double meanDepth = 0.5 * (before + after);
                double ui = 0.0, vi = 0.0;
                if (meanDepth > minDepth)
                {
                    const double inv = 1.0 / (l * meanDepth);
                    ui = 0.5 * ((inL - outL) + (outR - inR)) * inv;
                    vi = 0.5 * ((inT - outT) + (outB - inB)) * inv;
                }
                u[i] = ui;
                v[i] = vi;

                // Brzina širenja poremećaja za CFL uslov: gravitacioni talas √(g d) i advekcija |u|.
                // Faktor 2: u Mei modelu svaka ćelija ima VLASTITU izlaznu cijev prema susjedu, pa kad su obje
                // cijevi između dvije ćelije aktivne, neto protok se mijenja dvostruko brže nego u standardnoj
                // šemi. Von Neumannova analiza (2D, "šahovski" mod) tada daje Δt ≤ l / (2 √(g d)) umjesto
                // l / √(2 g d); bez ovog faktora duboke bare (d > ~1 m) počnu numerički oscilovati.
                const double wave = 2.0 * std::sqrt(g * after) + std::max(std::abs(ui), std::abs(vi));
                maxWave = std::max(maxWave, wave);
                maxSpeed2 = std::max(maxSpeed2, ui * ui + vi * vi); // kvadrat; korijen na kraju
                maxDepth = std::max(maxDepth, after);
            }
        }
        st.negativeDepthCorrection = negCorrection;
        st.maxWaveSpeed = maxWave;
        st.maxSpeed = std::sqrt(maxSpeed2);
        st.maxDepth = maxDepth;
        st.boundaryOutflowVolume = boundaryOut * dt;
        return st;
    }

    LossStepStats VirtualPipesSolver::applyLosses(SimulationState& s, double dt) const
    {
        LossStepStats st;
        const double toMetersPerSecond = 1.0e-3 / 3600.0;
        const double infRate = _p.infiltrationMillimetersPerHour * toMetersPerSecond;
        const double evapRate = _p.evaporationMillimetersPerHour * toMetersPerSecond;
        const double totalRate = infRate + evapRate;
        if (totalRate <= 0.0)
            return st;

        const double potential = totalRate * dt; // [m] koliko bi se izgubilo da ima dovoljno vode
        const double infShare = infRate / totalRate;
        const double area = s.cellArea();
        double removed = 0.0;
        double remaining = 0.0;
        double* d = s.water.data();
        const std::size_t n = s.cellCount();
        for (std::size_t i = 0; i < n; ++i)
        {
            const double loss = std::min(d[i], potential);
            d[i] -= loss;
            removed += loss;
            remaining = std::max(remaining, d[i]);
        }
        st.waterRemains = remaining > 0.0;
        st.infiltratedVolume = removed * area * infShare;
        st.evaporatedVolume = removed * area * (1.0 - infShare);
        return st;
    }

    double VirtualPipesSolver::stableTimeStep(double maxWaveSpeed, double cellSize) const
    {
        if (!(maxWaveSpeed > 0.0))
            return _p.maxTimeStepSeconds;
        return std::min(_p.maxTimeStepSeconds, _p.courantNumber * cellSize / maxWaveSpeed);
    }
}
