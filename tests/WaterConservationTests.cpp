// Tok vode metodom virtuelnih cijevi (AC3, AC6): test 2 (ravan teren), test 7 (očuvanje vode),
// osnovna svojstva cijevi i stabilnost duboke vode.

#include "ErosionSimulation.hpp"
#include "TestFramework.hpp"
#include "VirtualPipesSolver.hpp"

#include <algorithm>
#include <cmath>

using namespace prs;
using namespace prs::test;

namespace
{
    double relativeError(double a, double b)
    {
        const double scale = std::max(std::abs(a), std::abs(b));
        return scale > 0.0 ? std::abs(a - b) / scale : 0.0;
    }
}

PRS_TEST("Water", cijev_prati_razliku_vodenih_povrsina)
{
    const double gDt = 9.81 * 0.5;
    const double friction = 0.5 * 9.81 * 0.04 * 0.04 / 10.0;
    // Protok nastaje iz razlike η = b + d, ne samo terena: niže dno, ali VIŠA vodena površina susjeda -> 0.
    CHECK(VirtualPipesSolver::pipeOutflow(0.0, 10.0, 10.5, 9.0, 10.6, gDt, friction, 1e-7) == 0.0);
    // Više dno, ali niža površina susjeda -> voda teče prema susjedu.
    CHECK(VirtualPipesSolver::pipeOutflow(0.0, 10.0, 10.5, 10.2, 10.3, gDt, friction, 1e-7) > 0.0);
    // Ista površina -> nema ubrzanja; postojeći protok se samo prigušuje trenjem.
    const double kept = VirtualPipesSolver::pipeOutflow(1.0, 10.0, 10.5, 9.0, 10.5, gDt, friction, 1e-7);
    CHECK(kept > 0.0 && kept < 1.0);
    // Izlazni protok nikad nije negativan.
    CHECK(VirtualPipesSolver::pipeOutflow(0.1, 10.0, 10.5, 9.0, 12.0, gDt, friction, 1e-7) == 0.0);
    // Trenje smanjuje protok u odnosu na isti slučaj bez trenja.
    const double noFriction = VirtualPipesSolver::pipeOutflow(0.5, 10.0, 10.5, 9.8, 10.0, gDt, 0.0, 1e-7);
    const double withFriction = VirtualPipesSolver::pipeOutflow(0.5, 10.0, 10.5, 9.8, 10.0, gDt, friction, 1e-7);
    CHECK(withFriction < noFriction);
    CHECK_NEAR(noFriction, 0.5 + gDt * 0.5 * 0.5, 1e-12); // f + Δt g h_f Δη, h_f = 10.5 − 10.0
}

PRS_TEST("Water", jezero_u_mirovanju_na_neravnom_dnu)
{
    // Neravno dno, ali ravna vodena površina η = 20 m -> nijedna cijev ne smije provesti vodu.
    Heightmap map = roughTerrain(30, 20, 8.0);
    WaterFlowParameters p;
    VirtualPipesSolver solver(p);
    SimulationState s;
    s.allocate(30, 20, 10.0);
    s.terrain = map.elevation;
    for (std::size_t i = 0; i < s.cellCount(); ++i)
        s.water[i] = 20.0 + 10.0 - s.terrain[i]; // teren je u [10, 18+], površina na 30 m
    const Grid2D<double> before = s.water;
    for (int k = 0; k < 200; ++k)
    {
        solver.computeOutflowFlux(s, 0.5);
        solver.updateWaterAndVelocity(s, 0.5);
    }
    double maxChange = 0.0, maxFlux = 0.0;
    for (std::size_t i = 0; i < s.cellCount(); ++i)
    {
        maxChange = std::max(maxChange, std::abs(s.water[i] - before[i]));
        maxFlux = std::max({maxFlux, s.fluxLeft[i], s.fluxRight[i], s.fluxTop[i], s.fluxBottom[i]});
    }
    note("max |promjena dubine| = " + fmt(maxChange) + " m, max protok = " + fmt(maxFlux) + " m3/s");
    CHECK_LE(maxChange, 1e-9);
    CHECK_LE(maxFlux, 1e-9);
}

PRS_TEST("Water", test2_ravan_teren_bez_kretanja_vode)
{
    // Potpuno ravan teren, ravnomjerna kiša, zatvoreni rubovi: simetrični uslovi -> nema usmjerenog toka.
    Heightmap map = flatTerrain(40, 30, 50.0);
    SimulationConfig cfg;
    cfg.rainfall.totalMillimeters = 80.0;
    cfg.rainfall.durationHours = 2.0;
    cfg.runoffHours = 1.0;
    ErosionSimulation sim(map, cfg);
    double maxFlux = 0.0, maxSpeed = 0.0;
    while (!sim.finished())
    {
        sim.step();
        const SimulationState& s = sim.state();
        for (std::size_t i = 0; i < s.cellCount(); ++i)
        {
            maxFlux = std::max({maxFlux, s.fluxLeft[i], s.fluxRight[i], s.fluxTop[i], s.fluxBottom[i]});
            maxSpeed = std::max({maxSpeed, std::abs(s.velocityX[i]), std::abs(s.velocityY[i])});
        }
    }
    sim.finalize();
    const SimulationState& s = sim.state();
    const SimulationStatistics& st = sim.statistics();
    CHECK(maxFlux == 0.0);
    CHECK(maxSpeed == 0.0);
    CHECK(st.grossErodedVolume == 0.0);
    CHECK(st.grossDepositedVolume == 0.0);
    bool terrainUnchanged = true, waterUniform = true;
    for (std::size_t i = 0; i < s.cellCount(); ++i)
    {
        terrainUnchanged &= (s.terrain[i] == 50.0);
        waterUniform &= (s.water[i] == s.water[0]);
    }
    CHECK(terrainUnchanged);
    CHECK(waterUniform);
    CHECK_NEAR(s.water[0], 0.080, 1e-12); // 80 mm svuda
    note("koraka: " + std::to_string(st.steps) + ", dubina vode svuda = " + fmt(s.water[0] * 1000.0, 12) + " mm");
}

PRS_TEST("Water", test7_ocuvanje_vode_zatvorena_mreza)
{
    // Hrapav teren, zatvoreni rubovi, bez isparavanja i infiltracije: početna voda + kiša = konačna voda,
    // provjereno POSLIJE SVAKOG KORAKA, uz uključenu eroziju (teren se mijenja tokom simulacije).
    Heightmap map = roughTerrain(48, 32, 30.0);
    SimulationConfig cfg;
    cfg.rainfall.totalMillimeters = 60.0;
    cfg.rainfall.durationHours = 0.5;
    cfg.runoffHours = 0.5;
    ErosionSimulation sim(map, cfg);
    double worst = 0.0;
    while (!sim.finished())
    {
        sim.step();
        worst = std::max(worst, relativeError(sim.totalWaterVolume(),
                                              sim.statistics().rainVolume + sim.statistics().negativeDepthCorrection));
    }
    sim.finalize();
    const SimulationStatistics& st = sim.statistics();
    note("najveca relativna greska bilansa po koraku: " + fmt(worst) + " (" + std::to_string(st.steps) + " koraka)");
    note("kisa " + fmt(st.rainVolume, 10) + " m3, na terenu " + fmt(st.finalWaterVolume, 10) + " m3, izlaz " +
         fmt(st.boundaryWaterOutflow) + " m3");
    CHECK_LE(worst, 1e-12);
    CHECK(st.boundaryWaterOutflow == 0.0); // zatvoreni rubovi: ništa ne izlazi
    CHECK_LE(std::abs(st.waterBalanceRelativeError()), 1e-12);
    CHECK_LE(st.negativeDepthCorrection, 1e-9 * st.rainVolume); // korekcija zaokruživanja zanemariva
    CHECK_NEAR(st.rainVolume, 0.060 * 48 * 32 * 100.0, 1e-9);
    CHECK(st.grossErodedVolume > 0.0); // erozija je bila aktivna
}

PRS_TEST("Water", bilans_sa_otvorenim_rubom_i_gubicima)
{
    // Otvoreni rub + infiltracija + isparavanje: kiša = voda na terenu + izlaz + infiltracija + isparavanje.
    Heightmap map = slopeTerrain(40, 16, 20.0, 10.0, 0.5);
    SimulationConfig cfg;
    cfg.rainfall.totalMillimeters = 50.0;
    cfg.rainfall.durationHours = 1.0;
    cfg.runoffHours = 0.5;
    cfg.flow.boundary = BoundaryCondition::Open;
    cfg.flow.infiltrationMillimetersPerHour = 5.0;
    cfg.flow.evaporationMillimetersPerHour = 1.0;
    ErosionSimulation sim(map, cfg);
    sim.run();
    const SimulationStatistics& st = sim.statistics();
    note("kisa " + fmt(st.rainVolume) + ", teren " + fmt(st.finalWaterVolume) + ", izlaz " + fmt(st.boundaryWaterOutflow) +
         ", infiltracija " + fmt(st.infiltratedVolume) + ", isparavanje " + fmt(st.evaporatedVolume) + " m3");
    CHECK(st.boundaryWaterOutflow > 0.0);
    CHECK(st.infiltratedVolume > 0.0);
    CHECK(st.evaporatedVolume > 0.0);
    CHECK_LE(std::abs(st.waterBalanceRelativeError()), 1e-12);
    CHECK_NEAR(st.infiltratedVolume / st.evaporatedVolume, 5.0, 1e-9); // podjela gubitka u omjeru brzina
    CHECK(st.sedimentBoundaryOutflow >= 0.0);
    CHECK_LE(std::abs(st.materialBalanceError), 1e-9 * std::max(1.0, st.grossErodedVolume));
}

PRS_TEST("Water", stabilnost_duboke_bare)
{
    // Duboka bara (5 m) s početnim poremećajem, bez kiše: s adaptivnim Δt (faktor 2 u CFL uslovu)
    // poremećaj se ne smije pojačavati. (Sa Δt = 0.7 · l / √(g d) ova šema "eksplodira" za < 20 koraka.)
    WaterFlowParameters p;
    VirtualPipesSolver solver(p);
    SimulationState s;
    s.allocate(32, 32, 10.0);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            s.water(x, y) = 5.0 + 0.5 * std::exp(-((x - 16.0) * (x - 16.0) + (y - 16.0) * (y - 16.0)) / 8.0);
    double total0 = 0.0;
    for (double d : s.water.values())
        total0 += d;
    double waveSpeed = 2.0 * std::sqrt(9.81 * 5.5);
    double maxAmplitude = 0.0;
    for (int k = 0; k < 5000; ++k)
    {
        const double dt = solver.stableTimeStep(waveSpeed, s.cellSize);
        solver.computeOutflowFlux(s, dt);
        const WaterStepStats st = solver.updateWaterAndVelocity(s, dt);
        waveSpeed = st.maxWaveSpeed;
        if (k > 100)
        {
            double mean = 0.0;
            for (double d : s.water.values())
                mean += d;
            mean /= static_cast<double>(s.cellCount());
            for (double d : s.water.values())
                maxAmplitude = std::max(maxAmplitude, std::abs(d - mean));
        }
    }
    double total = 0.0;
    bool finite = true;
    for (double d : s.water.values())
    {
        total += d;
        finite &= std::isfinite(d);
    }
    note("najveca amplituda poslije 100 koraka: " + fmt(maxAmplitude) + " m (pocetni poremecaj 0.5 m)");
    CHECK(finite);
    CHECK_LE(maxAmplitude, 0.5);
    CHECK_NEAR(total, total0, 1e-9 * total0);
}

PRS_TEST("Water", voda_tece_prema_nizem_terenu)
{
    // Test 3 (dio za vodu): nagnut teren, voda se kreće niz nagib (+x) i skuplja na nižem kraju.
    Heightmap map = slopeTerrain(60, 20, 30.0);
    SimulationConfig cfg;
    cfg.rainfall.totalMillimeters = 50.0;
    cfg.rainfall.durationHours = 0.5;
    cfg.runoffHours = 1.0;
    cfg.erosion.enabled = false;
    ErosionSimulation sim(map, cfg);
    double meanU = 0.0;
    int samples = 0;
    while (!sim.finished())
    {
        sim.step();
        if (sim.time() > 600.0 && sim.time() < 1800.0) // tokom kiše
        {
            const SimulationState& s = sim.state();
            for (int y = 0; y < 20; ++y)
                for (int x = 10; x < 40; ++x)
                    meanU += s.velocityX(x, y), ++samples;
        }
    }
    sim.finalize();
    meanU /= std::max(1, samples);
    const SimulationState& s = sim.state();
    double low = 0.0, all = 0.0;
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 60; ++x)
        {
            all += s.water(x, y);
            if (x >= 45)
                low += s.water(x, y);
        }
    note("srednja brzina niz padinu u = " + fmt(meanU) + " m/s; udio vode u najnizoj cetvrtini = " + fmt(low / all));
    CHECK(meanU > 0.0);
    CHECK(low / all > 0.9);
}
