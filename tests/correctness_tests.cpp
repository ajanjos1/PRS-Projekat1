// Testovi numeričke ispravnosti. Svaka optimizovana varijanta poredi se sa skalarnom referencom (A),
// a referenca se provjerava fizikalnim testovima (očuvanje mase, mirovanje jezera, simetrija, rubovi).
// Pokretanje: ./prs_tests [izlazni_csv]   (ctest takođe pokreće ovaj program)

#include "cpu_features.hpp"
#include "simulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace sw;

namespace
{
    int g_failed = 0;
    int g_passed = 0;
    std::ofstream g_csv;

    void report(const std::string& test, const std::string& detail, bool ok, double value = 0)
    {
        (ok ? g_passed : g_failed)++;
        std::cout << (ok ? "[ OK ] " : "[FAIL] ") << test << " :: " << detail << "\n";
        if (g_csv)
            g_csv << test << ",\"" << detail << "\"," << (ok ? "PASS" : "FAIL") << ',' << value << '\n';
    }

    std::vector<Variant> all_variants()
    {
        std::vector<Variant> v = main_variants();
        for (auto& a : ablation_variants())
            v.push_back(a);
        return v;
    }

    // ---------------------------------------------------------------- 1. ekvivalencija varijanti
    void test_equivalence()
    {
        const SimParams p;
        const StepConstants c = make_step_constants(p);
        const int sizes[][2] = {{8, 8}, {13, 7}, {37, 29}, {64, 64}, {100, 75}, {127, 129}, {250, 250}, {257, 255}};
        const int steps = 300;
        for (const auto& sz : sizes)
        {
            const int W = sz[0], H = sz[1];
            Simulation init(W, H);
            init_terrain(init, p);
            Simulation ref(W, H);
            ref.copy_state_from(init);
            run_serial(ref, c, steps, kernels_scalar());

            for (const auto& v : all_variants())
            {
                if (!variant_supported(v))
                {
                    std::cout << "       (preskoceno, nije podrzano) " << v.id << "\n";
                    continue;
                }
                const std::vector<int> threads = v.multithreaded ? std::vector<int>{1, 2, 3, 4, 8} : std::vector<int>{1};
                for (int t : threads)
                {
                    Simulation s(W, H);
                    s.copy_state_from(init);
                    run_steps(s, c, steps, *v.kernels, v.driver, t);
                    const StateDiff d = compare_states(ref, s, 1e-5, 1e-4);
                    char buf[200];
                    std::snprintf(buf, sizeof(buf), "%dx%d %s p=%d max_abs=%.3g %s", W, H, v.id.c_str(), t, d.max_abs(),
                                  d.bitwise() ? "bit-identicno" : "u toleranciji");
                    report("ekvivalencija_sa_referencom", buf, d.ok(), d.max_abs());
                }
            }
        }
    }

    // ---------------------------------------------------------------- 2. padding i halo ostaju netaknuti
    void test_padding()
    {
        const SimParams p;
        const StepConstants c = make_step_constants(p);
        for (const auto& v : all_variants())
        {
            if (!variant_supported(v))
                continue;
            const int W = 29, H = 11; // W % 8 != 0 -> skalarni ostatak u SIMD varijantama
            Simulation s(W, H);
            init_terrain(s, p);
            run_steps(s, c, 200, *v.kernels, v.driver, v.multithreaded ? 3 : 1);
            const float* arrays[] = {s.water(), s.sediment(), s.sediment_ratio(), s.flux_left(), s.flux_right(),
                                     s.flux_top(), s.flux_bottom()};
            std::size_t bad = 0;
            for (const float* a : arrays)
                for (std::size_t r = 0; r < s.rows(); ++r)
                    for (std::size_t col = 0; col < s.pitch(); ++col)
                    {
                        const bool interior = r >= 1 && r <= static_cast<std::size_t>(H) && col >= kPadLeft &&
                                              col < static_cast<std::size_t>(kPadLeft + W);
                        if (!interior && a[r * s.pitch() + col] != 0.0f)
                            ++bad;
                    }
            report("halo_i_padding_netaknuti", v.id + " (29x11)", bad == 0, static_cast<double>(bad));
        }
    }

    // ---------------------------------------------------------------- 3. jezero u mirovanju (well-balanced)
    void test_lake_at_rest()
    {
        SimParams p;
        p.rain_rate = 0.0f;
        p.evaporation = 0.0f;
        const StepConstants c = make_step_constants(p);
        for (const auto& v : main_variants())
        {
            if (!variant_supported(v))
                continue;
            const int W = 61, H = 45;
            Simulation s(W, H);
            init_terrain(s, p);
            // ravna površina vode na nivou eta iznad dijela terena (dio ćelija je suh)
            double sum_b = 0;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                    sum_b += s.terrain()[s.index(x, y)];
            const float eta = static_cast<float>(sum_b / (W * H));
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                {
                    const std::size_t i = s.index(x, y);
                    s.water()[i] = std::max(0.0f, eta - s.terrain()[i]);
                }
            Simulation before(W, H);
            before.copy_state_from(s);
            run_steps(s, c, 1000, *v.kernels, v.driver, v.multithreaded ? 2 : 1);

            double max_flux = 0, max_dd = 0, max_db = 0;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                {
                    const std::size_t i = s.index(x, y);
                    max_flux = std::max({max_flux, (double)s.flux_left()[i], (double)s.flux_right()[i],
                                         (double)s.flux_top()[i], (double)s.flux_bottom()[i]});
                    max_dd = std::max(max_dd, (double)std::fabs(s.water()[i] - before.water()[i]));
                    max_db = std::max(max_db, (double)std::fabs(s.terrain()[i] - before.terrain()[i]));
                }
            char buf[200];
            std::snprintf(buf, sizeof(buf), "%s: max|f|=%.3g max|dd|=%.3g max|db|=%.3g", v.id.c_str(), max_flux, max_dd, max_db);
            report("jezero_u_mirovanju", buf, max_flux == 0.0 && max_dd == 0.0 && max_db == 0.0, max_flux);
        }
    }

    // ---------------------------------------------------------------- 4. bilans vode (kiša + isparavanje)
    void test_water_balance()
    {
        const SimParams p;
        const StepConstants c = make_step_constants(p);
        for (const auto& v : main_variants())
        {
            if (!variant_supported(v))
                continue;
            const int W = 100, H = 75;
            Simulation s(W, H);
            init_terrain(s, p);
            double worst = 0;
            Totals prev = compute_totals(s);
            for (int step = 0; step < 500; ++step)
            {
                run_steps(s, c, 1, *v.kernels, v.driver, v.multithreaded ? 2 : 1);
                const Totals t = compute_totals(s);
                // tok je konzervativan (zatvoreni rubovi), pa: W_{n+1} = (W_n + N r dt) (1 - K_e dt)
                const double expected = (prev.water + static_cast<double>(c.rain_dt) * W * H) * c.evap_factor;
                worst = std::max(worst, std::fabs(t.water - expected) / expected);
                prev = t;
            }
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s: max relativna greska po koraku = %.3g", v.id.c_str(), worst);
            report("bilans_vode", buf, worst < 1e-5, worst);
        }
    }

    // ---------------------------------------------------------------- 5. očuvanje teren + sediment
    void test_solid_conservation()
    {
        const SimParams p;
        const StepConstants c = make_step_constants(p);
        for (const auto& v : main_variants())
        {
            if (!variant_supported(v))
                continue;
            const int W = 127, H = 129;
            Simulation s(W, H);
            init_terrain(s, p);
            const Totals t0 = compute_totals(s);
            run_steps(s, c, 1500, *v.kernels, v.driver, v.multithreaded ? 2 : 1);
            const Totals t1 = compute_totals(s);
            const double m0 = t0.terrain + t0.sediment;
            const double m1 = t1.terrain + t1.sediment;
            const double rel = std::fabs(m1 - m0) / m0;
            char buf[200];
            std::snprintf(buf, sizeof(buf), "%s: |d(b+s)|/(b+s) = %.3g, suspendovano = %.4g m", v.id.c_str(), rel, t1.sediment);
            report("ocuvanje_teren_plus_sediment", buf, rel < 1e-6 && t1.sediment > 0, rel);
        }
    }

    // ---------------------------------------------------------------- 6. nenegativnost i stabilnost
    void test_nonnegative_stable()
    {
        const SimParams p;
        const StepConstants c = make_step_constants(p);
        for (const auto& v : main_variants())
        {
            if (!variant_supported(v))
                continue;
            const int W = 100, H = 75;
            Simulation s(W, H);
            init_terrain(s, p);
            float min_d = 1e30f, min_s = 1e30f;
            bool finite = true;
            for (int k = 0; k < 20; ++k)
            {
                run_steps(s, c, 150, *v.kernels, v.driver, v.multithreaded ? 2 : 1);
                const Totals t = compute_totals(s);
                min_d = std::min(min_d, t.min_water);
                min_s = std::min(min_s, t.min_sediment);
                finite = finite && t.finite;
            }
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s: 3000 koraka, min d=%.3g, min s=%.3g, konacno=%d", v.id.c_str(), min_d, min_s, finite);
            report("nenegativnost_i_stabilnost", buf, finite && min_d >= 0.0f && min_s >= 0.0f, min_d);
        }
    }

    // ---------------------------------------------------------------- 7. zatvoreni rubovi
    void test_closed_boundaries()
    {
        const SimParams p;
        const StepConstants c = make_step_constants(p);
        for (const auto& v : main_variants())
        {
            if (!variant_supported(v))
                continue;
            const int W = 37, H = 29;
            Simulation s(W, H);
            init_terrain(s, p);
            run_steps(s, c, 400, *v.kernels, v.driver, v.multithreaded ? 2 : 1);
            double edge = 0;
            for (int y = 0; y < H; ++y)
                edge = std::max({edge, (double)s.flux_left()[s.index(0, y)], (double)s.flux_right()[s.index(W - 1, y)]});
            for (int x = 0; x < W; ++x)
                edge = std::max({edge, (double)s.flux_top()[s.index(x, 0)], (double)s.flux_bottom()[s.index(x, H - 1)]});
            report("zatvoreni_rubovi", v.id + ": izlazni protok kroz rub = " + std::to_string(edge), edge == 0.0, edge);
        }
    }

    // ---------------------------------------------------------------- 8. simetrija lijevo-desno
    void test_mirror_symmetry()
    {
        const SimParams p;
        const StepConstants c = make_step_constants(p);
        for (const auto& v : main_variants())
        {
            if (!variant_supported(v))
                continue;
            const int W = 50, H = 31;
            Simulation s(W, H);
            init_terrain(s, p);
            float* b = s.terrain();
            for (int y = 0; y < H; ++y)
                for (int x = W / 2; x < W; ++x)
                    b[s.index(x, y)] = b[s.index(W - 1 - x, y)];
            s.refresh_terrain_halo();
            std::copy(b, b + s.pitch() * s.rows(), s.terrain_next());
            run_steps(s, c, 400, *v.kernels, v.driver, v.multithreaded ? 2 : 1);
            std::size_t mismatches = 0;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                {
                    const std::size_t i = s.index(x, y);
                    const std::size_t m = s.index(W - 1 - x, y);
                    if (s.water()[i] != s.water()[m] || s.terrain()[i] != s.terrain()[m] ||
                        s.sediment()[i] != s.sediment()[m] || s.flux_left()[i] != s.flux_right()[m] ||
                        s.flux_top()[i] != s.flux_top()[m])
                        ++mismatches;
                }
            report("simetrija_lijevo_desno", v.id + ": neslaganja = " + std::to_string(mismatches) + " (ocekivano 0, bit-egzaktno)",
                   mismatches == 0, static_cast<double>(mismatches));
        }
    }

    // ---------------------------------------------------------------- 9. CFL kontrola koraka
    void test_cfl()
    {
        SimParams p;
        p.dt = 10.0f; // namjerno prevelik korak
        p.dissolve = 0.05f;
        p.deposit = 0.05f;
        p.evaporation = 0.05f;
        const StepConstants c = make_step_constants(p);
        const float limit = cfl_dt_limit(p);
        report("cfl_ogranicenje", "trazeno dt=10 s -> koristi se dt=" + std::to_string(c.dt) + " s (limit " + std::to_string(limit) + ")",
               c.dt <= limit && c.dt > 0.0f, c.dt);

        Simulation s(64, 64);
        init_terrain(s, p);
        run_serial(s, c, 3000, kernels_scalar());
        const Totals t = compute_totals(s);
        report("stabilnost_na_cfl_limitu", "3000 koraka sa dt = CFL limit, max dubina = " + std::to_string(t.max_water) + " m",
               t.finite && t.max_water < 1e3f, t.max_water);
    }
}

int main(int argc, char** argv)
{
    const std::string csv = argc > 1 ? argv[1] : "correctness_report.csv";
    g_csv.open(csv);
    if (g_csv)
        g_csv << "test,detail,result,value\n";

    std::cout << "CPU: " << cpu_brand() << " | AVX2: " << (avx2_supported() ? "da" : "ne") << " | "
              << build_description() << "\n\n";

    test_equivalence();
    test_padding();
    test_lake_at_rest();
    test_water_balance();
    test_solid_conservation();
    test_nonnegative_stable();
    test_closed_boundaries();
    test_mirror_symmetry();
    test_cfl();

    std::cout << "\nUkupno: " << g_passed << " uspjesnih, " << g_failed << " neuspjesnih\n";
    return g_failed == 0 ? 0 : 1;
}
