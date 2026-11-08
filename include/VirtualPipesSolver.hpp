#pragma once
// Tok vode metodom virtuelnih cijevi (Virtual Pipes / hydrostatic pipe model):
//   O'Brien & Hodgins (1995), Mei, Decaudin & Hu (2007), Šťava i dr. (2008).
//
// Svaka ćelija je povezana sa 4 susjeda virtuelnim cijevima. U cijevi voda ubrzava zbog razlike hidrostatičkog
// pritiska, tj. razlike visina vodenih površina η = b + d (NE samo terena). Za cijev prema susjedu j:
//
//     f_new = max(0, f + Δt · g · A_cijevi · (η_i − η_j) / l)                         (Mei 2007, jed. 2–3)
//
// Ovdje je presjek cijevi A_cijevi = l · h_f, gdje je h_f = max(η_i, η_j) − max(b_i, b_j) stvarna dubina toka
// na granici dvije ćelije, pa se l skrati:  f_new = f + Δt · g · h_f · Δη. Uz to se dodaje trenje o podlogu po
// Manningu, poluimplicitno (Bates, Horritt & Fewtrell 2010 – "local inertial" šema, LISFLOOD-FP):
//
//     f_new = (f + Δt · g · h_f · Δη) / (1 + Δt · g · n² · |f| / (l · h_f^(7/3)))
//
// Sa n = 0 ovo je Mei model s presjekom l·h_f. Bez trenja voda na padini ubrzava neograničeno, što za
// simulaciju koja traje satima fizičkog vremena nije realno.
//
// Ograničenje odljeva (Mei 2007, jed. 4): ukupni izlaz u koraku ne smije premašiti vodu u ćeliji,
//     K = min(1, d1 · l² / (Σ f · Δt)),   f ← K · f   → dubina vode nikad ne postaje negativna.
//
// Nova dubina (jed. 5–6):  d2 = d1 + Δt · (Σ f_ulaz − Σ f_izlaz) / l²
// Brzina (jed. 7–9):        u = ΔWx / (l · d̄),  ΔWx = ½ (f^R(x−1) − f^L(x) + f^R(x) − f^L(x+1)),  d̄ = ½ (d1 + d2)

#include "SimulationConfig.hpp"
#include "SimulationState.hpp"

namespace prs
{
    struct WaterStepStats
    {
        double boundaryOutflowVolume = 0.0;   // [m³] voda koja je u koraku izašla kroz otvorene rubove
        double negativeDepthCorrection = 0.0; // [m³] korekcija zaokruživanja (d < 0 -> 0); očekivano ~0
        double maxWaveSpeed = 0.0;            // max(2√(g d) + max(|u|, |v|)) [m/s] – za CFL uslov
        double maxSpeed = 0.0;                // max |v| [m/s]
        double maxDepth = 0.0;                // max d [m]
    };

    struct LossStepStats
    {
        double infiltratedVolume = 0.0; // [m³]
        double evaporatedVolume = 0.0;  // [m³]
        bool waterRemains = true;       // ostaje li igdje voda nakon gubitaka
    };

    class VirtualPipesSolver
    {
    public:
        explicit VirtualPipesSolver(const WaterFlowParameters& parameters);

        const WaterFlowParameters& parameters() const { return _p; }

        // Korak 1: ravnomjerno dodaje sloj vode (kiša). Vraća dodanu zapreminu [m³].
        double addUniformWater(SimulationState& s, double depthMeters) const;

        // Koraci 2–4: visine vodenih površina, protoci kroz virtuelne cijevi, ograničenje dostupnom vodom.
        // Čita b i d (i kod susjeda), piše f^L, f^R, f^T, f^B i d1 samo za svoju ćeliju.
        void computeOutflowFlux(SimulationState& s, double dt) const;

        // Koraci 5–6: nova dubina vode (bilans ulaz − izlaz) i brzine u, v.
        // Čita protoke susjeda (već konačne za ovaj korak), piše d, u, v samo za svoju ćeliju.
        WaterStepStats updateWaterAndVelocity(SimulationState& s, double dt) const;

        // Gubici vode (infiltracija i isparavanje konstantnom brzinom), ograničeni dostupnom vodom.
        LossStepStats applyLosses(SimulationState& s, double dt) const;

        // Stabilan korak (CFL): dt = α · l / max(2√(g d) + |u|), ograničeno sa maxTimeStepSeconds.
        double stableTimeStep(double maxWaveSpeed, double cellSize) const;

        // Protok jedne cijevi (javno radi testova). fOld >= 0 je protok iz prethodnog koraka.
        static double pipeOutflow(double fOld, double terrainHere, double surfaceHere, double terrainNeighbor,
                                  double surfaceNeighbor, double gravityDt, double frictionCoefficient,
                                  double minDepth);

    private:
        WaterFlowParameters _p;
    };
}
