// Erozija, depozicija i transport sedimenta (AC4, AC6): testovi 1, 3, 8 i 9, kapacitet i konvergencija u Δt.

#include "ErosionSimulation.hpp"
#include "HydraulicErosion.hpp"
#include "MathUtils.hpp"
#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>

using namespace prs;
using namespace prs::test;

PRS_TEST("Erosion", kubni_korijen)
{
    double worst = 0.0;
    for (double x = 1e-12; x < 1e4; x *= 1.37)
        worst = std::max(worst, std::abs(cubeRootPositive(x) / std::cbrt(x) - 1.0));
    note("najveca relativna greska cubeRootPositive: " + fmt(worst));
    CHECK_LE(worst, 1e-13);
}

PRS_TEST("Erosion", kapacitet_transporta)
{
    ErosionParameters p;
    CHECK(HydraulicErosion::transportCapacity(p, 0.3, 0.0, 0.1) == 0.0); // voda miruje
    CHECK(HydraulicErosion::transportCapacity(p, 0.3, 1.0, 0.0) == 0.0); // nema vode
    CHECK(HydraulicErosion::transportCapacity(p, 0.0, 1.0, 0.1) == 0.0); // ravna površina, minSlope = 0
    const double c1 = HydraulicErosion::transportCapacity(p, 0.1, 0.5, 0.05);
    const double c2 = HydraulicErosion::transportCapacity(p, 0.1, 1.0, 0.05);
    const double c3 = HydraulicErosion::transportCapacity(p, 0.2, 1.0, 0.05);
    const double c4 = HydraulicErosion::transportCapacity(p, 0.2, 1.0, 0.10);
    CHECK(c1 < c2 && c2 < c3 && c3 < c4); // raste s brzinom, nagibom i dubinom
    CHECK_NEAR(c1, p.capacityCoefficient * 0.1 * 0.5 * 0.05, 1e-15);
    // Gornja granica koncentracije
    CHECK_NEAR(HydraulicErosion::transportCapacity(p, 1.0, 100.0, 0.2), p.maxConcentration * 0.2, 1e-15);
    // sin α iz gradijenta: nagib 1 (45°) -> sin = 1/√2
    CHECK_NEAR(HydraulicErosion::sinSlope(1.0, 0.0), 1.0 / std::sqrt(2.0), 1e-15);
    CHECK_NEAR(HydraulicErosion::sinSlope(0.6, 0.8), 1.0 / std::sqrt(2.0), 1e-15);
}

PRS_TEST("Erosion", relaksacija_ne_preskace_kapacitet)
{
    // Jedna ćelija s brzom vodom: i uz ogroman Δt sediment se približi kapacitetu, ali ga ne pređe.
    ErosionParameters p;
    HydraulicErosion ero(p);
    SimulationState s;
    s.allocate(3, 1, 10.0);
    s.terrain(0, 0) = 12.0;
    s.terrain(1, 0) = 11.0;
    s.terrain(2, 0) = 10.0;
    s.terrainNext = s.terrain;
    s.water.fill(0.2);
    s.velocityX.fill(2.0);
    for (std::size_t i = 0; i < 3; ++i)
        s.bedrock[i] = -1e9;
    ero.erodeAndDeposit(s, 1.0e6);
    const double capacity = HydraulicErosion::transportCapacity(p, HydraulicErosion::sinSlope(0.1, 0.0), 2.0, 0.2);
    CHECK(s.sediment(1, 0) <= capacity);
    CHECK_NEAR(s.sediment(1, 0), capacity, 1e-12);
    CHECK_NEAR(s.terrain(1, 0) + s.sediment(1, 0), 11.0, 1e-12); // materijal je samo premješten
}

PRS_TEST("Erosion", test1_bez_kise_teren_nepromijenjen)
{
    // 0 L/m², nema početne vode: nema protoka, nema erozije, height mapa ostaje bit-identična.
    Heightmap map = roughTerrain(50, 35, 40.0);
    SimulationConfig cfg;
    cfg.rainfall.totalMillimeters = 0.0;
    cfg.rainfall.durationHours = 3.0;
    cfg.runoffHours = 1.0;
    ErosionSimulation sim(map, cfg);
    sim.run();
    const SimulationStatistics& st = sim.statistics();
    bool identical = true;
    for (std::size_t i = 0; i < map.elevation.size(); ++i)
        identical &= (sim.state().terrain[i] == map.elevation[i]);
    CHECK(identical);
    CHECK(st.rainVolume == 0.0);
    CHECK(st.finalWaterVolume == 0.0);
    CHECK(st.grossErodedVolume == 0.0);
    CHECK(st.grossDepositedVolume == 0.0);
    CHECK(st.maxLowering == 0.0 && st.maxRaise == 0.0);
    CHECK_NEAR(st.simulatedSeconds, 4.0 * 3600.0, 0.0);
    note("koraka: " + std::to_string(st.steps) + " (suh teren bez kise se preskace jednim korakom po fazi)");
}

PRS_TEST("Erosion", test3_nagnuti_teren_erozija_prati_tok)
{
    // Nagnut teren (pad 40 m na 790 m, ~5 %) sa sitnim šumom; kiša 60 mm za 1 h, zatvoreni rubovi.
    // Očekivano: voda teče niz padinu, gornji/srednji dio se snižava (erozija), podnožje se povisuje
    // (depozicija tamo gdje se voda skuplja i usporava).
    Heightmap map = slopeTerrain(80, 24, 40.0, 10.0, 0.2, 11);
    SimulationConfig cfg;
    cfg.rainfall.totalMillimeters = 60.0;
    cfg.rainfall.durationHours = 1.0;
    cfg.runoffHours = 0.5;
    ErosionSimulation sim(map, cfg);
    sim.run();
    const SimulationState& s = sim.state();
    const SimulationStatistics& st = sim.statistics();
    double slopePart = 0.0, toe = 0.0;
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 80; ++x)
        {
            const double dh = s.terrain(x, y) - map.elevation(x, y);
            if (x >= 10 && x < 60)
                slopePart += dh;
            if (x >= 72)
                toe += dh;
        }
    note("neto promjena na padini (x 10..59): " + fmt(slopePart * 100.0) + " m3, u podnozju (x >= 72): " +
         fmt(toe * 100.0) + " m3; bruto erozija " + fmt(st.grossErodedVolume) + " m3");
    CHECK(st.grossErodedVolume > 0.0);
    CHECK(slopePart < 0.0); // padina se snižava
    CHECK(toe > 0.0);       // podnožje se povisuje
    CHECK(st.maxLowering > 0.0 && st.maxRaise > 0.0);
}

PRS_TEST("Erosion", test8_ocuvanje_materijala)
{
    // Zatvoren sistem: Σ (teren + suspendovani sediment) je konstantan u SVAKOM koraku.
    Heightmap map = roughTerrain(48, 40, 35.0, 10.0, 21);
    SimulationConfig cfg;
    cfg.rainfall.totalMillimeters = 100.0;
    cfg.rainfall.durationHours = 0.5;
    cfg.runoffHours = 0.5;
    ErosionSimulation sim(map, cfg);
    double worst = 0.0, maxSuspended = 0.0;
    while (!sim.finished())
    {
        sim.step();
        worst = std::max(worst, std::abs(sim.materialImbalance()));
        maxSuspended = std::max(maxSuspended, sim.totalSuspendedSedimentVolume());
    }
    sim.finalize();
    const SimulationStatistics& st = sim.statistics();
    note("bruto erozija " + fmt(st.grossErodedVolume) + " m3, max suspendovano " + fmt(maxSuspended) +
         " m3, najveca greska bilansa po koraku " + fmt(worst) + " m3");
    CHECK(st.grossErodedVolume > 1.0);
    CHECK(maxSuspended > 0.0);
    CHECK_LE(worst, 1e-9 * st.grossErodedVolume);
    CHECK_LE(std::abs(st.materialBalanceError), 1e-9 * st.grossErodedVolume);
    // Poslije taloženja na kraju: sniženi volumen = povišeni volumen (sav materijal je ostao u mapi)
    CHECK_NEAR(st.netLoweredVolume, st.netRaisedVolume, 1e-9 * st.netLoweredVolume);
    CHECK(sim.totalSuspendedSedimentVolume() == 0.0);

    // Otvoreni rub: Σ (teren + sediment) + izneseni sediment = konstanta.
    cfg.flow.boundary = BoundaryCondition::Open;
    Heightmap slope = slopeTerrain(60, 20, 30.0, 10.0, 0.3);
    ErosionSimulation open(slope, cfg);
    open.run();
    note("otvoren rub: izneseno sedimenta " + fmt(open.statistics().sedimentBoundaryOutflow) + " m3, greska " +
         fmt(open.statistics().materialBalanceError) + " m3");
    CHECK(open.statistics().sedimentBoundaryOutflow > 0.0);
    CHECK_LE(std::abs(open.statistics().materialBalanceError), 1e-9 * open.statistics().grossErodedVolume);
}

PRS_TEST("Erosion", test9_stabilnost)
{
    // Strm, hrapav teren i jaka kiša (200 mm za 1 h) + 1 h oticanja, mali erodibilni sloj.
    Heightmap map = roughTerrain(64, 48, 80.0, 5.0, 99);
    SimulationConfig cfg;
    cfg.rainfall.totalMillimeters = 200.0;
    cfg.rainfall.durationHours = 1.0;
    cfg.runoffHours = 1.0;
    cfg.erosion.erodibleDepthMeters = 0.5;
    ErosionSimulation sim(map, cfg);
    double minWater = 0.0, minSediment = 0.0;
    bool finite = true;
    while (!sim.finished())
    {
        sim.step();
        const SimulationState& s = sim.state();
        for (std::size_t i = 0; i < s.cellCount(); ++i)
        {
            finite &= std::isfinite(s.water[i]) && std::isfinite(s.sediment[i]) && std::isfinite(s.terrain[i]) &&
                      std::isfinite(s.velocityX[i]) && std::isfinite(s.velocityY[i]);
            minWater = std::min(minWater, s.water[i]);
            minSediment = std::min(minSediment, s.sediment[i]);
        }
    }
    sim.finalize();
    const SimulationStatistics& st = sim.statistics();
    note("koraka " + std::to_string(st.steps) + ", dt " + fmt(st.minTimeStep) + ".." + fmt(st.maxTimeStep) +
         " s, max dubina " + fmt(st.peakWaterDepth) + " m, max brzina " + fmt(st.peakSpeed) + " m/s, max snizenje " +
         fmt(st.maxLowering) + " m, max povisenje " + fmt(st.maxRaise) + " m");
    CHECK(finite);
    CHECK(!st.aborted);
    CHECK(minWater >= 0.0);    // nema negativne dubine
    CHECK(minSediment >= 0.0); // nema negativnog sedimenta
    CHECK(st.allFinite);
    CHECK_LE(st.maxLowering, 0.5 + 1e-9); // erozija ograničena erodibilnim slojem
    // Povišenje ne može biti veće od svega što je erodirano, raspoređeno na jednu ćeliju
    CHECK_LE(st.maxRaise, st.grossErodedVolume / 25.0);
    CHECK_LE(st.peakSpeed, 30.0); // nema numeričke eksplozije brzine
    CHECK_LE(std::abs(st.waterBalanceRelativeError()), 1e-12);
    CHECK_LE(std::abs(st.materialBalanceRelativeError()), 1e-9);
}

PRS_TEST("Erosion", bez_erozije_kad_je_iskljucena)
{
    Heightmap map = slopeTerrain(40, 12, 20.0);
    SimulationConfig cfg;
    cfg.rainfall.totalMillimeters = 50.0;
    cfg.rainfall.durationHours = 0.5;
    cfg.erosion.enabled = false;
    ErosionSimulation sim(map, cfg);
    sim.run();
    bool identical = true;
    for (std::size_t i = 0; i < map.elevation.size(); ++i)
        identical &= (sim.state().terrain[i] == map.elevation[i]);
    CHECK(identical);
    CHECK(sim.statistics().rainVolume > 0.0);
}

PRS_TEST("Erosion", konvergencija_u_vremenskom_koraku)
{
    // Isti scenarij s Courantovim brojem 0.7 (podrazumijevano) i 0.35 (dvostruko manji Δt):
    // rezultat ne smije bitno zavisiti od Δt (izvorni Mei model bez trenja i bez Δt u eroziji zavisi).
    Heightmap map = mountainTerrain(64, 64, 60.0, 10.0);
    SimulationConfig cfg;
    cfg.rainfall.totalMillimeters = 80.0;
    cfg.rainfall.durationHours = 1.0;
    cfg.runoffHours = 0.25;
    double lowered[2], maxLow[2];
    std::int64_t steps[2];
    const double courant[2] = {0.7, 0.35};
    for (int k = 0; k < 2; ++k)
    {
        cfg.flow.courantNumber = courant[k];
        ErosionSimulation sim(map, cfg);
        sim.run();
        lowered[k] = sim.statistics().netLoweredVolume;
        maxLow[k] = sim.statistics().maxLowering;
        steps[k] = sim.statistics().steps;
    }
    const double rel = std::abs(lowered[0] - lowered[1]) / lowered[1];
    note("C = 0.7: " + std::to_string(steps[0]) + " koraka, snizeno " + fmt(lowered[0]) + " m3, max " + fmt(maxLow[0]) +
         " m;  C = 0.35: " + std::to_string(steps[1]) + " koraka, snizeno " + fmt(lowered[1]) + " m3, max " +
         fmt(maxLow[1]) + " m;  razlika " + fmt(100.0 * rel, 3) + " %");
    CHECK(lowered[1] > 0.0);
    CHECK_LE(rel, 0.05);
}
