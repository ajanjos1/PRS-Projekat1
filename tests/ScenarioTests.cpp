// Scenariji: test 4 (planinski teren), test 5 (različite količine kiše), test 6 (različito trajanje).
// Testovi 5 i 6 ispisuju tabele za analizu; provjeravaju samo ono što fizika sigurno zahtijeva (bilansi,
// valjanost, smjer promjene intenziteta), a NE strogu proporcionalnost erozije.

#include "ErosionSimulation.hpp"
#include "HeightmapIO.hpp"
#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numeric>
#include <vector>

using namespace prs;
using namespace prs::test;

namespace
{
    struct Outcome
    {
        double lowered = 0.0;     // [m³]
        double raised = 0.0;      // [m³]
        double maxLowering = 0.0; // [m]
        double peakDepth = 0.0;   // [m]
        double peakOutflow = 0.0; // [m³/s] najveći protok kroz otvoreni rub
        double waterError = 0.0;
        double materialError = 0.0;
        bool valid = false;
        std::int64_t steps = 0;
    };

    Outcome simulate(const Heightmap& map, SimulationConfig cfg)
    {
        ErosionSimulation sim(map, cfg);
        Outcome o;
        double lastOut = 0.0;
        while (!sim.finished())
        {
            const double dt = sim.step();
            const double out = sim.statistics().boundaryWaterOutflow;
            if (dt > 0.0)
                o.peakOutflow = std::max(o.peakOutflow, (out - lastOut) / dt);
            lastOut = out;
        }
        sim.finalize();
        const SimulationStatistics& st = sim.statistics();
        o.lowered = st.netLoweredVolume;
        o.raised = st.netRaisedVolume;
        o.maxLowering = st.maxLowering;
        o.peakDepth = st.peakWaterDepth;
        o.waterError = std::abs(st.waterBalanceRelativeError());
        o.materialError = std::abs(st.materialBalanceRelativeError());
        o.valid = st.allFinite && !st.aborted && st.minWaterDepth >= 0.0 && st.minSediment >= 0.0;
        o.steps = st.steps;
        return o;
    }
}

PRS_TEST("Scenario", test4_planinski_teren)
{
    // Sintetička planina s radijalnim dolinama: voda se skuplja u dolinama, erozija je lokalizovana na
    // tokovima, sediment se taloži niže (podnožje, doline), teren se stvarno mijenja.
    Heightmap map = mountainTerrain(96, 96, 80.0, 10.0);
    SimulationConfig cfg;
    cfg.rainfall.totalMillimeters = 60.0;
    cfg.rainfall.durationHours = 1.0;
    cfg.runoffHours = 0.5;
    ErosionSimulation sim(map, cfg);
    sim.run();
    const SimulationState& s = sim.state();
    const SimulationStatistics& st = sim.statistics();
    const std::size_t n = s.cellCount();

    // Lokalizacija: 10 % ćelija s najvećim sniženjem nosi većinu ukupnog sniženja.
    std::vector<double> lowering(n);
    for (std::size_t i = 0; i < n; ++i)
        lowering[i] = std::max(0.0, map.elevation[i] - s.terrain[i]);
    std::vector<double> sorted = lowering;
    std::sort(sorted.begin(), sorted.end(), std::greater<double>());
    const double total = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    const double top10 = std::accumulate(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(n / 10), 0.0);

    // Erozija prati tok: srednja najveća dubina vode u jako erodiranim ćelijama je veća od prosjeka.
    const double threshold = sorted[n / 20]; // granica 5 % najjače erodiranih
    double depthEroded = 0.0, depthAll = 0.0;
    int countEroded = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        depthAll += s.maxWaterDepth[i];
        if (lowering[i] >= threshold && lowering[i] > 0.0)
            depthEroded += s.maxWaterDepth[i], ++countEroded;
    }
    depthAll /= static_cast<double>(n);
    depthEroded /= std::max(1, countEroded);

    // Depozicija niže od erozije: prosječna početna visina povišenih ćelija < prosjek sniženih.
    double hLow = 0.0, hRaise = 0.0, wLow = 0.0, wRaise = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double dh = s.terrain[i] - map.elevation[i];
        if (dh < 0.0)
            hLow += -dh * map.elevation[i], wLow += -dh;
        else if (dh > 0.0)
            hRaise += dh * map.elevation[i], wRaise += dh;
    }
    hLow /= wLow;
    hRaise /= wRaise;

    note("snizeno " + fmt(st.netLoweredVolume) + " m3, povišeno " + fmt(st.netRaisedVolume) + " m3, max snizenje " +
         fmt(st.maxLowering) + " m, max povisenje " + fmt(st.maxRaise) + " m");
    note("udio sniženja u 10 % najjace erodiranih celija: " + fmt(100.0 * top10 / total, 3) + " %");
    note("srednja max dubina vode: erodirane celije " + fmt(depthEroded) + " m, sve celije " + fmt(depthAll) + " m");
    note("tezinska srednja pocetna visina: snizenih " + fmt(hLow) + " m, povisenih " + fmt(hRaise) + " m");
    CHECK(st.cellsChangedMoreThan1mm > 0);
    CHECK(top10 / total > 0.5);
    CHECK(depthEroded > depthAll);
    CHECK(hRaise < hLow);
    CHECK_LE(std::abs(st.materialBalanceRelativeError()), 1e-9);
}

PRS_TEST("Scenario", test4_stvarna_height_mapa)
{
    // Stvarna mapa iz priloga (primjer_1, 412 x 221, 16 bita), ako je dostupna. Kraća kiša da test bude brz.
    const char* path = "assets/heightmaps/primjer_1/height-map-input-try-1-height-map-16bit-texturemap-app-2026-11-08.png";
    if (!std::filesystem::exists(path))
    {
        note("primjer nije pronadjen iz tekuceg foldera - preskoceno");
        return;
    }
    TerrainScale scale;
    scale.cellSizeMeters = 30.0;
    const Heightmap map = loadHeightmap(path, scale);
    SimulationConfig cfg;
    cfg.rainfall.totalMillimeters = 30.0;
    cfg.rainfall.durationHours = 0.5;
    const Outcome o = simulate(map, cfg);
    note("30 mm / 0.5 h, celija 30 m: " + std::to_string(o.steps) + " koraka, snizeno " + fmt(o.lowered) +
         " m3, max snizenje " + fmt(o.maxLowering) + " m, max dubina " + fmt(o.peakDepth) + " m");
    CHECK(o.valid);
    CHECK(o.lowered > 0.0);
    CHECK_LE(o.waterError, 1e-12);
    CHECK_LE(o.materialError, 1e-9);
}

PRS_TEST("Scenario", test5_razlicite_kolicine_kise)
{
    // Ista mapa, isto trajanje (1 h), kiša 10/50/100/200 L/m².
    Heightmap map = mountainTerrain(80, 80, 60.0, 10.0);
    const double amounts[] = {10.0, 50.0, 100.0, 200.0};
    std::vector<Outcome> results;
    note("kisa [L/m2] | snizeno [m3] | max snizenje [m] | max dubina [m] | koraka");
    for (double mm : amounts)
    {
        SimulationConfig cfg;
        cfg.rainfall.totalMillimeters = mm;
        cfg.rainfall.durationHours = 1.0;
        cfg.runoffHours = 0.25;
        results.push_back(simulate(map, cfg));
        const Outcome& o = results.back();
        note(fmt(mm) + " | " + fmt(o.lowered) + " | " + fmt(o.maxLowering) + " | " + fmt(o.peakDepth) + " | " +
             std::to_string(o.steps));
        CHECK(o.valid);
        CHECK_LE(o.waterError, 1e-12);
        CHECK_LE(o.materialError, 1e-9);
    }
    // Fizički sigurno: više vode -> dublja voda i (ovdje) više erozije; proporcionalnost se NE zahtijeva.
    CHECK(results[3].peakDepth > results[0].peakDepth);
    CHECK(results[3].lowered > results[0].lowered);
    note("omjer snizenja 200/10 L/m2 = " + fmt(results[3].lowered / results[0].lowered) + " (kisa 20x)");
}

PRS_TEST("Scenario", test6_isto_ukupno_razlicito_trajanje)
{
    // 100 L/m² za 1, 5 i 10 h (intenzitet 100, 20 i 10 mm/h) na nagnutom terenu s otvorenim rubom.
    // Kraće trajanje = jača kiša -> veći vršni protok i dublji tok. Erozija se samo ispisuje.
    Heightmap map = slopeTerrain(80, 24, 20.0, 10.0, 0.3, 5);
    const double hours[] = {1.0, 5.0, 10.0};
    std::vector<Outcome> results;
    note("trajanje [h] | intenzitet [mm/h] | vrsni izlaz [m3/s] | max dubina [m] | snizeno [m3] | koraka");
    for (double h : hours)
    {
        SimulationConfig cfg;
        cfg.rainfall.totalMillimeters = 100.0;
        cfg.rainfall.durationHours = h;
        cfg.runoffHours = 0.5;
        cfg.flow.boundary = BoundaryCondition::Open;
        results.push_back(simulate(map, cfg));
        const Outcome& o = results.back();
        note(fmt(h) + " | " + fmt(cfg.rainfall.intensityMillimetersPerHour()) + " | " + fmt(o.peakOutflow) + " | " +
             fmt(o.peakDepth) + " | " + fmt(o.lowered) + " | " + std::to_string(o.steps));
        CHECK(o.valid);
        CHECK_LE(o.waterError, 1e-12);
        CHECK_LE(o.materialError, 1e-9);
    }
    // Vršni protok mora opadati s trajanjem (manji intenzitet), iako je ukupna kiša ista.
    CHECK(results[0].peakOutflow > results[1].peakOutflow);
    CHECK(results[1].peakOutflow > results[2].peakOutflow);
    // Gornja granica: u ravnoteži izlaz ne može biti veći od intenziteta × površine
    const double area = 80 * 24 * 100.0;
    CHECK_LE(results[2].peakOutflow, 1.05 * 0.1 / 36000.0 * area);
}
