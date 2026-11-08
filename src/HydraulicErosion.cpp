#include "HydraulicErosion.hpp"

#include <algorithm>
#include <cmath>

namespace prs
{
    HydraulicErosion::HydraulicErosion(const ErosionParameters& parameters) : _p(parameters) {}

    double HydraulicErosion::sinSlope(double gradientX, double gradientY)
    {
        const double tan2 = gradientX * gradientX + gradientY * gradientY; // tan² α
        return std::sqrt(tan2 / (1.0 + tan2));
    }

    double HydraulicErosion::transportCapacity(const ErosionParameters& p, double sinAlpha, double speed,
                                               double depth)
    {
        if (depth <= 0.0 || speed <= 0.0)
            return 0.0;
        const double concentration =
            std::min(p.capacityCoefficient * std::max(sinAlpha, p.minimumSlope) * speed, p.maxConcentration);
        return concentration * depth;
    }

    ErosionStepStats HydraulicErosion::erodeAndDeposit(SimulationState& s, double dt) const
    {
        const int W = s.width;
        const int H = s.height;
        const double l = s.cellSize;
        const double inv2l = 1.0 / (2.0 * l);
        const double invl = 1.0 / l;
        // Udio razlike (C − s) koji se razmijeni u koraku dužine dt (tačno rješenje relaksacije).
        const double erodeFraction = 1.0 - std::exp(-_p.dissolvingRate * dt);
        const double depositFraction = 1.0 - std::exp(-_p.depositionRate * dt);

        const double* b = s.terrain.data();
        const double* rock = s.bedrock.data();
        const double* d = s.water.data();
        const double* u = s.velocityX.data();
        const double* v = s.velocityY.data();
        double* bNext = s.terrainNext.data();
        double* sed = s.sediment.data();

        double eroded = 0.0;
        double deposited = 0.0;

        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                const std::size_t i = s.terrain.index(x, y);
                // Nagib VODENE POVRŠINE η = b + d (hidraulički gradijent koji pokreće tok): centralne razlike
                // u unutrašnjosti, jednostrane na rubu mreže. U tankom sloju vode na padini to je praktično
                // nagib terena (Mei: "local tilt angle"); u jezeru je površina ravna pa voda ne erodira dno.
                const auto eta = [&](std::size_t k) { return b[k] + d[k]; };
                double gx = 0.0, gy = 0.0;
                if (W > 1)
                {
                    if (x == 0) gx = (eta(i + 1) - eta(i)) * invl;
                    else if (x == W - 1) gx = (eta(i) - eta(i - 1)) * invl;
                    else gx = (eta(i + 1) - eta(i - 1)) * inv2l;
                }
                if (H > 1)
                {
                    if (y == 0) gy = (eta(i + W) - eta(i)) * invl;
                    else if (y == H - 1) gy = (eta(i) - eta(i - W)) * invl;
                    else gy = (eta(i + W) - eta(i - W)) * inv2l;
                }
                const double speed = std::sqrt(u[i] * u[i] + v[i] * v[i]);
                const double capacity = transportCapacity(_p, sinSlope(gx, gy), speed, d[i]);

                const double bi = b[i];
                double si = sed[i];
                double bn = bi;
                if (capacity > si)
                {
                    // Erozija: voda može nositi više nego što nosi -> podiže materijal s terena.
                    double amount = erodeFraction * (capacity - si);
                    const double available = bi - rock[i];
                    if (amount > available)
                        amount = available > 0.0 ? available : 0.0;
                    bn = bi - amount;
                    si += amount;
                    eroded += amount;
                }
                else if (si > capacity)
                {
                    // Depozicija: voda nosi više nego što može -> višak se taloži.
                    const double amount = depositFraction * (si - capacity);
                    bn = bi + amount;
                    si -= amount;
                    deposited += amount;
                }
                bNext[i] = bn;
                sed[i] = si;
            }
        }
        s.terrain.swap(s.terrainNext); // double buffering: novi teren postaje tekući

        ErosionStepStats st;
        st.erodedVolume = eroded * s.cellArea();
        st.depositedVolume = deposited * s.cellArea();
        return st;
    }

    TransportStepStats HydraulicErosion::transportSediment(SimulationState& s, double dt) const
    {
        const int W = s.width;
        const int H = s.height;
        const double area = s.cellArea();
        const std::size_t n = s.cellCount();

        const double* d1 = s.waterBeforeFlow.data();
        const double* sed = s.sediment.data();
        const double* fL = s.fluxLeft.data();
        const double* fR = s.fluxRight.data();
        const double* fT = s.fluxTop.data();
        const double* fB = s.fluxBottom.data();
        double* perFlux = s.sedimentPerOutflow.data();
        double* next = s.sedimentNext.data();

        // (a) Za svaku ćeliju: koliko sedimenta [m] nosi 1 m³/s izlaznog protoka u ovom koraku.
        //     Koncentracija s/d1 puta zapremina vode f·dt podijeljena površinom ćelije.
        const double dtOverArea = dt / area;
        for (std::size_t i = 0; i < n; ++i)
            perFlux[i] = (d1[i] > 0.0) ? sed[i] * dtOverArea / d1[i] : 0.0;

        // (b) Bilans: s_novo = s − (poslano kroz 4 cijevi) + (primljeno od 4 susjeda).
        TransportStepStats st;
        double boundaryOut = 0.0;
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                const std::size_t i = s.sediment.index(x, y);
                const double sent = perFlux[i] * (fL[i] + fR[i] + fT[i] + fB[i]);
                double received = 0.0;
                if (x > 0) received += perFlux[i - 1] * fR[i - 1];
                if (x < W - 1) received += perFlux[i + 1] * fL[i + 1];
                if (y > 0) received += perFlux[i - W] * fB[i - W];
                if (y < H - 1) received += perFlux[i + W] * fT[i + W];

                if (x == 0) boundaryOut += perFlux[i] * fL[i];
                if (x == W - 1) boundaryOut += perFlux[i] * fR[i];
                if (y == 0) boundaryOut += perFlux[i] * fT[i];
                if (y == H - 1) boundaryOut += perFlux[i] * fB[i];

                double sn = sed[i] - sent + received;
                if (sn < 0.0)
                {
                    st.negativeCorrection += -sn * area;
                    sn = 0.0;
                }
                next[i] = sn;
            }
        }
        s.sediment.swap(s.sedimentNext);
        st.boundaryOutflowVolume = boundaryOut * area;
        return st;
    }

    double HydraulicErosion::settleSuspendedSediment(SimulationState& s) const
    {
        double settled = 0.0;
        double* b = s.terrain.data();
        double* sed = s.sediment.data();
        const std::size_t n = s.cellCount();
        for (std::size_t i = 0; i < n; ++i)
        {
            b[i] += sed[i];
            settled += sed[i];
            sed[i] = 0.0;
        }
        return settled * s.cellArea();
    }
}
