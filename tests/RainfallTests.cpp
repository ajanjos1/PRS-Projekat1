// Padavine i korisnički unos (AC2, spec. poglavlje 6): jedinice, intenzitet, raspodjela po koracima, validacija.

#include "ConsoleInput.hpp"
#include "ErosionSimulation.hpp"
#include "Rainfall.hpp"
#include "TestFramework.hpp"

#include <limits>
#include <sstream>

using namespace prs;
using namespace prs::test;

PRS_TEST("Rainfall", jedinice_i_intenzitet)
{
    RainfallEvent r;
    r.totalMillimeters = 100.0; // 100 L/m²
    r.durationHours = 5.0;
    CHECK_NEAR(r.totalMeters(), 0.1, 1e-15);                          // 1 L/m² = 1 mm = 0.001 m
    CHECK_NEAR(r.durationSeconds(), 18000.0, 0.0);                    // 5 h = 18000 s
    CHECK_NEAR(r.intensityMillimetersPerHour(), 20.0, 1e-12);         // 100 / 5 = 20 mm/h
    CHECK_NEAR(r.intensityMetersPerSecond(), 0.02 / 3600.0, 1e-18);   // 20 mm/h u m/s

    RainfallEvent one;
    one.totalMillimeters = 1.0;
    one.durationHours = 1.0;
    CHECK_NEAR(one.totalMeters(), 0.001, 1e-18);
}

PRS_TEST("Rainfall", kisa_se_dodaje_samo_u_trajanju)
{
    RainfallEvent r;
    r.totalMillimeters = 100.0;
    r.durationHours = 5.0;
    const double rate = r.intensityMetersPerSecond();
    CHECK_NEAR(r.depthAddedBetween(0.0, 3600.0), 0.02, 1e-15);            // prvi sat: 20 mm
    CHECK_NEAR(r.depthAddedBetween(17000.0, 19000.0), 1000.0 * rate, 1e-15); // samo dio prije kraja kiše
    CHECK(r.depthAddedBetween(18000.0, 20000.0) == 0.0);                  // poslije kiše: ništa
    CHECK(r.depthAddedBetween(-100.0, 0.0) == 0.0);                       // prije početka: ništa
    CHECK(r.depthAddedBetween(10.0, 10.0) == 0.0);                        // prazan interval

    // Zbir po proizvoljnoj (neuniformnoj) podjeli vremena mora biti tačno ukupna količina, a NE
    // "ukupno u svakom koraku" niti "ukupno svakog sata".
    double t = 0.0, sum = 0.0;
    int steps = 0;
    while (t < 20000.0)
    {
        const double dt = 0.37 + 3.1 * ((steps * 7919) % 13) / 13.0;
        sum += r.depthAddedBetween(t, t + dt);
        t += dt;
        ++steps;
    }
    CHECK_NEAR(sum, 0.1, 1e-12);
    note("zbir po " + std::to_string(steps) + " neuniformnih koraka = " + fmt(sum * 1000.0, 15) + " mm (ocekivano 100)");
}

PRS_TEST("Rainfall", validacija_unosa)
{
    CHECK(!validateRainfallInput(100.0, 5.0).has_value());
    CHECK(!validateRainfallInput(0.0, 5.0).has_value());     // 0 L/m² je dozvoljeno (test 1)
    CHECK(!validateRainfallInput(12.75, 0.5).has_value());   // decimalne vrijednosti
    CHECK(validateRainfallInput(-1.0, 5.0).has_value());     // negativna kiša
    CHECK(validateRainfallInput(100.0, 0.0).has_value());    // trajanje 0 -> dijeljenje nulom
    CHECK(validateRainfallInput(100.0, -2.0).has_value());   // negativno trajanje
    CHECK(validateRainfallInput(std::numeric_limits<double>::quiet_NaN(), 1.0).has_value());
    CHECK(validateRainfallInput(100.0, std::numeric_limits<double>::infinity()).has_value());
    CHECK(validateRainfallInput(std::numeric_limits<double>::infinity(), 1.0).has_value());
}

PRS_TEST("Rainfall", parsiranje_brojeva)
{
    using console::parseNumber;
    CHECK(parseNumber("100") == 100.0);
    CHECK(parseNumber("12.5") == 12.5);
    CHECK(parseNumber("12,5") == 12.5); // decimalni zarez
    CHECK(parseNumber("  7  ") == 7.0);
    CHECK(parseNumber("+3") == 3.0);
    CHECK(parseNumber("-2.5") == -2.5);
    CHECK(parseNumber("1e2") == 100.0);
    CHECK(!parseNumber("").has_value());
    CHECK(!parseNumber("abc").has_value());
    CHECK(!parseNumber("12abc").has_value());
    CHECK(!parseNumber("1.2.3").has_value());
    CHECK(!parseNumber("nan").has_value());
    CHECK(!parseNumber("inf").has_value());
    CHECK(!parseNumber("1e400").has_value()); // izvan opsega double
}

PRS_TEST("Rainfall", neispravan_unos_ne_rusi_program)
{
    // Simulirani korisnik: tekst, negativna vrijednost, pa ispravna. Program ponavlja pitanje.
    std::istringstream in("abc\n-5\n\n100\n");
    std::ostringstream out;
    console::Prompter ask(in, out);
    const auto value = ask.askNumber("Total rainfall (L/m2)", std::nullopt, [](double v) {
        return validateRainfallInput(v, 1.0);
    });
    CHECK(value.has_value() && *value == 100.0);
    CHECK(out.str().find("nije ispravan broj") != std::string::npos);
    CHECK(out.str().find("negativna") != std::string::npos);

    // Prazan unos -> podrazumijevana vrijednost; kraj ulaza (EOF) -> nullopt, bez izuzetka.
    std::istringstream in2("\n");
    console::Prompter ask2(in2, out);
    CHECK(ask2.askNumber("Cell size", 10.0) == 10.0);
    CHECK(!ask2.askNumber("Cell size", 10.0).has_value());

    std::istringstream in3("x\nY\n");
    console::Prompter ask3(in3, out);
    CHECK(ask3.askYesNo("Start?", std::nullopt) == true);
    CHECK(console::stripQuotes("\"C:\\Maps\\a b.png\"") == "C:\\Maps\\a b.png");
}

PRS_TEST("Rainfall", simulacija_dodaje_tacnu_kolicinu)
{
    // Ravan teren, zatvoreni rubovi: nakon pola trajanja kiše na terenu je tačno pola padavina.
    Heightmap map = flatTerrain(20, 12, 50.0);
    SimulationConfig cfg;
    cfg.rainfall.totalMillimeters = 100.0;
    cfg.rainfall.durationHours = 1.0;
    cfg.runoffHours = 0.5;
    cfg.flow.maxTimeStepSeconds = 7.0; // namjerno ne dijeli 3600 tačno
    ErosionSimulation sim(map, cfg);
    const double area = 20 * 12 * 100.0; // 240 ćelija po 10 x 10 m
    while (sim.time() < 1800.0 - 1e-9)
        sim.step();
    const double expectedHalf = 0.05 * area * (sim.time() / 1800.0);
    CHECK_NEAR(sim.totalWaterVolume(), expectedHalf, 1e-9 * expectedHalf);
    while (!sim.finished())
        sim.step();
    sim.finalize();
    CHECK_NEAR(sim.time(), 5400.0, 0.0);                                // kiša 1 h + oticanje 0.5 h
    CHECK_NEAR(sim.statistics().rainVolume, 0.1 * area, 1e-9);          // ukupno tačno 100 mm
    CHECK_NEAR(sim.statistics().finalWaterVolume, 0.1 * area, 1e-9);
    CHECK_NEAR(sim.state().water(3, 4), 0.1, 1e-12);                    // ravan teren: 100 mm svuda
}
