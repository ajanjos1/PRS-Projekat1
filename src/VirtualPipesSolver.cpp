#include "VirtualPipesSolver.hpp"

#include "MathUtils.hpp"

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
            f /= 1.0 + frictionCoefficient * fOld / h73;
        }
        return f;
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

        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                const std::size_t i = s.water.index(x, y);
                const double di = d[i];
                d1[i] = di;
                if (di <= 0.0)
                {
                    fL[i] = fR[i] = fT[i] = fB[i] = 0.0; // suha ćelija nema šta poslati
                    continue;
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
                    st.negativeDepthCorrection += -after * area;
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
                st.maxWaveSpeed = std::max(st.maxWaveSpeed, wave);
                st.maxSpeed = std::max(st.maxSpeed, ui * ui + vi * vi); // kvadrat; korijen na kraju
                st.maxDepth = std::max(st.maxDepth, after);
            }
        }
        st.maxSpeed = std::sqrt(st.maxSpeed);
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
