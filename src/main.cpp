// prs_swe - simulacija plitkih voda (model virtuelnih cijevi) i hidrauličke erozije terena na CPU-u.
//
// Načini rada:
//   prs_swe info
//   prs_swe bench    [--sizes 128,256,512,1024] [--threads 1,2,4,8] [--reps 10] [--warmup 1]
//                    [--spinup 200] [--updates 8388608] [--e2e-threads 2] [--e2e-factor 10]
//                    [--no-e2e] [--no-ablation] [--variants A_scalar,G_omp_avx2,...]
//                    [--out results/benchmarks.csv]
//   prs_swe simulate [--size 512] [--steps 4000] [--variant G_omp_avx2] [--threads 2]
//                    [--snap 1000] [--diag 50] [--out results/sim]
//   prs_swe stream   [--threads 1,2,4,8] [--mb 128]

#include "benchmark.hpp"
#include "cpu_features.hpp"
#include "io_npy.hpp"
#include "simulation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(PRS_HAS_OPENMP)
#include <omp.h>
#endif

namespace
{
    using namespace sw;

    // ------------------------------------------------------------------ argumenti
    struct Args
    {
        std::string mode = "info";
        std::map<std::string, std::string> kv;
        bool has(const std::string& k) const { return kv.count(k) != 0; }
        std::string get(const std::string& k, const std::string& def) const
        {
            auto it = kv.find(k);
            return it == kv.end() ? def : it->second;
        }
        int geti(const std::string& k, int def) const { return has(k) ? std::stoi(kv.at(k)) : def; }
    };

    Args parse(int argc, char** argv)
    {
        Args a;
        if (argc > 1)
            a.mode = argv[1];
        for (int i = 2; i < argc; ++i)
        {
            std::string key = argv[i];
            if (key.rfind("--", 0) != 0)
                throw std::invalid_argument("Neočekivan argument: " + key);
            key = key.substr(2);
            if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0)
                a.kv[key] = argv[++i];
            else
                a.kv[key] = "1";
        }
        return a;
    }

    std::vector<int> parse_list(const std::string& s)
    {
        std::vector<int> out;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, ','))
            if (!item.empty())
                out.push_back(std::stoi(item));
        return out;
    }

    // "512" -> 512x512, "250x130" -> 250x130
    std::pair<int, int> parse_size(const std::string& s)
    {
        const auto pos = s.find('x');
        if (pos == std::string::npos)
            return {std::stoi(s), std::stoi(s)};
        return {std::stoi(s.substr(0, pos)), std::stoi(s.substr(pos + 1))};
    }

    std::vector<std::string> split(const std::string& s)
    {
        std::vector<std::string> out;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, ','))
            if (!item.empty())
                out.push_back(item);
        return out;
    }

    const char* driver_name(Driver d)
    {
        switch (d)
        {
        case Driver::Serial: return "serial";
        case Driver::OmpParallelFor: return "omp_parallel_for";
        case Driver::OmpRegion: return "omp_region";
        }
        return "?";
    }

    // Minimalni model saobraćaja prema memoriji po ćeliji i koraku (float = 4 B, susjedi iz keša):
    // faza 1: 6 čitanja + 4 upisa, faza 2: 7 čitanja + 4 upisa, faza 3: 6 čitanja + 1 upis -> 28 * 4 B.
    constexpr double kBytesPerCellStep = 112.0;

    // ------------------------------------------------------------------ info
    void print_info()
    {
        std::cout << "CPU: " << cpu_brand() << "\n";
        std::cout << "Logicke jezgre (std::thread::hardware_concurrency): " << std::thread::hardware_concurrency() << "\n";
        std::cout << "AVX2 (runtime): " << (avx2_supported() ? "da" : "ne") << "\n";
        std::cout << "OpenMP: " << (openmp_available() ? "da" : "ne") << ", max dretvi: " << openmp_max_threads() << "\n";
        std::cout << "Build: " << build_description() << "\n";
        SimParams p;
        const StepConstants c = make_step_constants(p);
        std::cout << "dt (trazeni) = " << p.dt << " s, CFL limit = " << cfl_dt_limit(p) << " s, koristi se dt = " << c.dt << " s\n";
        std::cout << "Varijante:\n";
        for (const auto& v : main_variants())
            std::cout << "  " << std::setw(20) << std::left << v.id << v.label << (variant_supported(v) ? "" : "  [NIJE PODRZANO]") << "\n";
        for (const auto& v : ablation_variants())
            std::cout << "  " << std::setw(20) << std::left << v.id << v.label << (variant_supported(v) ? "" : "  [NIJE PODRZANO]") << "\n";
    }

    // ------------------------------------------------------------------ bench
    struct Row
    {
        std::string grid;
        int W = 0, H = 0;
        std::string variant, label, kernels, driver, measure;
        int threads = 1, steps = 0, reps = 0, warmups = 0;
        bench::Stats st;
        double speedup_vs_scalar = 0, speedup_threads = 0, efficiency = 0;
        bool bitwise = false;
        double max_abs_diff = 0;
        double checksum = 0;
    };

    void write_csv(const std::string& path, const std::vector<Row>& rows, const std::string& runs_path)
    {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream out(path);
        out << "grid,W,H,cells,variant,label,kernels,driver,measure,threads,steps,reps,warmups,"
               "median_ms,min_ms,max_ms,mean_ms,stddev_ms,cv_percent,mcells_per_s,ns_per_cell_step,est_GBps,"
               "speedup_vs_scalar,speedup_vs_1thread_same_kernels,parallel_efficiency,bitwise_equal_to_scalar,max_abs_diff,checksum\n";
        out << std::setprecision(10);
        for (const auto& r : rows)
        {
            const double cells = static_cast<double>(r.W) * r.H;
            const double updates = cells * r.steps;
            const double med = r.st.median_s;
            out << r.grid << ',' << r.W << ',' << r.H << ',' << static_cast<long long>(cells) << ',' << r.variant << ",\""
                << r.label << "\"," << r.kernels << ',' << r.driver << ',' << r.measure << ',' << r.threads << ','
                << r.steps << ',' << r.reps << ',' << r.warmups << ',' << med * 1e3 << ',' << r.st.min_s * 1e3 << ','
                << r.st.max_s * 1e3 << ',' << r.st.mean_s * 1e3 << ',' << r.st.stddev_s * 1e3 << ','
                << (r.st.mean_s > 0 ? 100.0 * r.st.stddev_s / r.st.mean_s : 0.0) << ',' << updates / med / 1e6 << ','
                << med * 1e9 / updates << ',' << kBytesPerCellStep * updates / med / 1e9 << ',' << r.speedup_vs_scalar << ','
                << r.speedup_threads << ',' << r.efficiency << ',' << (r.bitwise ? 1 : 0) << ',' << r.max_abs_diff << ','
                << r.checksum << '\n';
        }

        std::ofstream runs(runs_path);
        runs << "grid,variant,measure,threads,run,time_ms\n";
        for (const auto& r : rows)
            for (std::size_t i = 0; i < r.st.runs_s.size(); ++i)
                runs << r.grid << ',' << r.variant << ',' << r.measure << ',' << r.threads << ',' << i + 1 << ','
                     << std::setprecision(10) << r.st.runs_s[i] * 1e3 << '\n';
    }

    int run_bench(const Args& a)
    {
        std::vector<std::string> size_specs = split(a.get("sizes", "128,256,512,1024"));
        const std::vector<int> thread_list = parse_list(a.get("threads", "1,2,4,8"));
        const int reps = a.geti("reps", 10);
        const int warmups = a.geti("warmup", 1);
        const int spinup = a.geti("spinup", 200);
        const double target_updates = std::stod(a.get("updates", "8388608")); // 2^23 ćelija-koraka po mjerenju
        const int e2e_threads = a.geti("e2e-threads", 2);
        const int e2e_factor = a.geti("e2e-factor", 10); // kompletna simulacija: 10x više koraka od mjerenja jezgra
        const bool do_e2e = !a.has("no-e2e");
        const bool do_ablation = !a.has("no-ablation");
        const std::string out = a.get("out", "results/benchmarks.csv");
        const std::string runs_out = a.get("runs-out", "results/benchmark_runs.csv");

        const SimParams params;
        const StepConstants c = make_step_constants(params);

        std::vector<Variant> variants = main_variants();
        if (do_ablation)
            for (auto& v : ablation_variants())
                variants.push_back(v);
        if (a.has("variants")) // npr. --variants A_scalar,G_omp_avx2 (A_scalar se uvijek mjeri radi ubrzanja)
        {
            const auto wanted = split(a.get("variants", ""));
            std::vector<Variant> filtered;
            for (const auto& v : variants)
                if (v.id == "A_scalar" || std::find(wanted.begin(), wanted.end(), v.id) != wanted.end())
                    filtered.push_back(v);
            variants = filtered;
        }

        std::cout << "Benchmark: reps=" << reps << ", warmup=" << warmups << ", spin-up=" << spinup
                  << " koraka, dt=" << c.dt << " s\n";
        std::cout << build_description() << "\n\n";

        std::vector<Row> rows;
        for (const auto& spec : size_specs)
        {
            const auto sz = parse_size(spec);
            const int W = sz.first;
            const int H = sz.second;
            const double cells = static_cast<double>(W) * H;
            const int steps = std::max(4, static_cast<int>(std::lround(target_updates / cells)));
            const std::string grid = std::to_string(W) + "x" + std::to_string(H);

            // Početno stanje: teren + spin-up (voda već teče), izvan mjerenja; isto za sve varijante.
            Simulation base(W, H);
            init_terrain(base, params);
            run_serial(base, c, spinup, kernels_scalar());

            // Referentni rezultat: skalarna varijanta A iz istog početnog stanja.
            Simulation reference(W, H);
            reference.copy_state_from(base);
            run_serial(reference, c, steps, kernels_scalar());

            Simulation work(W, H);
            std::map<std::string, double> serial_time; // naziv kernela -> medijan serijske varijante
            double t_scalar = 0;

            std::cout << "=== Mreza " << grid << " (" << steps << " koraka po mjerenju) ===\n";
            for (const auto& v : variants)
            {
                if (!variant_supported(v))
                {
                    std::cout << "  preskoceno (nije podrzano): " << v.id << "\n";
                    continue;
                }
                const std::vector<int> tl = v.multithreaded ? thread_list : std::vector<int>{1};
                for (int p : tl)
                {
                    const auto st = bench::measure(
                        warmups, reps, [&] { work.copy_state_from(base); },
                        [&] { run_steps(work, c, steps, *v.kernels, v.driver, p); },
                        [&] { return state_checksum(work); });

                    const StateDiff diff = compare_states(reference, work, 1e-5, 1e-4);
                    if (!diff.ok())
                    {
                        std::cerr << "GRESKA: " << v.id << " (p=" << p << ") odstupa od reference, max_abs="
                                  << diff.max_abs() << "\n";
                        return 2;
                    }

                    Row r;
                    r.grid = grid;
                    r.W = W;
                    r.H = H;
                    r.variant = v.id;
                    r.label = v.label;
                    r.kernels = v.kernels->name;
                    r.driver = driver_name(v.driver);
                    r.measure = "kernel";
                    r.threads = p;
                    r.steps = steps;
                    r.reps = reps;
                    r.warmups = warmups;
                    r.st = st;
                    r.bitwise = diff.bitwise();
                    r.max_abs_diff = diff.max_abs();
                    r.checksum = state_checksum(work);

                    if (v.id == "A_scalar")
                        t_scalar = st.median_s;
                    if (!v.multithreaded)
                        serial_time[v.kernels->name] = st.median_s;
                    r.speedup_vs_scalar = t_scalar > 0 ? t_scalar / st.median_s : 0;
                    const double ts = serial_time.count(v.kernels->name) ? serial_time[v.kernels->name] : 0;
                    r.speedup_threads = ts > 0 ? ts / st.median_s : 0;
                    r.efficiency = v.multithreaded ? r.speedup_threads / p : 0;

                    std::cout << "  " << std::setw(20) << std::left << v.id << " p=" << p << std::right << std::fixed
                              << std::setprecision(3) << "  med " << std::setw(9) << st.median_s * 1e3 << " ms"
                              << "  min " << std::setw(9) << st.min_s * 1e3 << "  max " << std::setw(9) << st.max_s * 1e3
                              << "  sd " << std::setw(7) << st.stddev_s * 1e3 << "  " << std::setw(8)
                              << cells * steps / st.median_s / 1e6 << " Mcel/s  S=" << std::setprecision(2)
                              << r.speedup_vs_scalar << (r.bitwise ? "  [bit-identicno]" : "  [u toleranciji]") << "\n";
                    rows.push_back(std::move(r));
                }
            }

            if (do_e2e)
            {
                // Kompletna simulacija: alokacija + generisanje terena + koraci + dijagnostika.
                double t_scalar_e2e = 0;
                const int e2e_steps = steps * e2e_factor;
                for (const auto& v : main_variants())
                {
                    if (!variant_supported(v))
                        continue;
                    const int p = v.multithreaded ? e2e_threads : 1;
                    std::unique_ptr<Simulation> sim;
                    double last = 0;
                    const auto st = bench::measure(
                        warmups, reps, [&] { sim.reset(); },
                        [&] {
                            sim = std::make_unique<Simulation>(W, H);
                            init_terrain(*sim, params);
                            run_steps(*sim, c, e2e_steps, *v.kernels, v.driver, p);
                            const Totals t = compute_totals(*sim);
                            last = t.water + t.terrain + t.sediment;
                        },
                        [&] { return last; });
                    Row r;
                    r.grid = grid;
                    r.W = W;
                    r.H = H;
                    r.variant = v.id;
                    r.label = v.label;
                    r.kernels = v.kernels->name;
                    r.driver = driver_name(v.driver);
                    r.measure = "e2e";
                    r.threads = p;
                    r.steps = e2e_steps;
                    r.reps = reps;
                    r.warmups = warmups;
                    r.st = st;
                    r.checksum = last;
                    if (v.id == "A_scalar")
                        t_scalar_e2e = st.median_s;
                    r.speedup_vs_scalar = t_scalar_e2e > 0 ? t_scalar_e2e / st.median_s : 0;
                    r.bitwise = true;
                    std::cout << "  e2e " << std::setw(16) << std::left << v.id << " p=" << p << std::right << std::fixed
                              << std::setprecision(3) << "  med " << std::setw(9) << st.median_s * 1e3
                              << " ms  S=" << std::setprecision(2) << r.speedup_vs_scalar << "\n";
                    rows.push_back(std::move(r));
                }
            }
            std::cout << "\n";
            write_csv(out, rows, runs_out); // nakon svake mreže (djelimični rezultati se ne gube)
        }
        std::cout << "Rezultati: " << out << " i " << runs_out << "\n";
        return 0;
    }

    // ------------------------------------------------------------------ simulate
    int run_simulate(const Args& a)
    {
        const auto sz = parse_size(a.get("size", "512"));
        const int W = sz.first;
        const int H = sz.second;
        const int steps = a.geti("steps", 4000);
        const int threads = a.geti("threads", 2);
        const int snap = a.geti("snap", 1000);
        const int diag = a.geti("diag", 50);
        const std::string out = a.get("out", "results/sim");
        const std::string vid = a.get("variant", "G_omp_avx2");

        SimParams params;
        if (a.has("rain")) params.rain_rate = std::stof(a.get("rain", "0"));
        if (a.has("kc")) params.capacity = std::stof(a.get("kc", "0"));
        if (a.has("ks")) params.dissolve = std::stof(a.get("ks", "0"));
        if (a.has("kd")) params.deposit = std::stof(a.get("kd", "0"));
        if (a.has("ke")) params.evaporation = std::stof(a.get("ke", "0"));
        if (a.has("dt")) params.dt = std::stof(a.get("dt", "0"));
        const StepConstants c = make_step_constants(params);

        const Variant* chosen = nullptr;
        std::vector<Variant> all = main_variants();
        for (auto& v : ablation_variants())
            all.push_back(v);
        for (const auto& v : all)
            if (v.id == vid)
                chosen = &v;
        if (!chosen)
            throw std::invalid_argument("Nepoznata varijanta: " + vid);
        if (!variant_supported(*chosen))
            throw std::runtime_error("Varijanta nije podrzana na ovom racunaru: " + vid);

        std::filesystem::create_directories(out);
        Simulation sim(W, H);
        init_terrain(sim, params);
        save_npy(out + "/terrain_initial.npy", extract(sim, sim.terrain()), H, W);

        std::ofstream diagf(out + "/diagnostics.csv");
        diagf << "step,time_s,water_m3,terrain_m3,sediment_m3,terrain_plus_sediment_m3,max_depth_m,min_depth_m,min_sediment_m,rain_added_m3\n";
        diagf << std::setprecision(12);
        const double cell_area = static_cast<double>(params.cell_size) * params.cell_size;
        auto log = [&](int step) {
            const Totals t = compute_totals(sim);
            diagf << step << ',' << step * static_cast<double>(c.dt) << ',' << t.water * cell_area << ','
                  << t.terrain * cell_area << ',' << t.sediment * cell_area << ',' << (t.terrain + t.sediment) * cell_area
                  << ',' << t.max_water << ',' << t.min_water << ',' << t.min_sediment << ','
                  << static_cast<double>(c.rain_dt) * step * sim.cells() * cell_area << '\n';
            if (!t.finite)
                throw std::runtime_error("Simulacija je postala nestabilna (NaN/Inf)");
        };

        std::cout << "Simulacija " << W << "x" << H << ", " << steps << " koraka, dt=" << c.dt << " s, varijanta "
                  << chosen->id << " (p=" << threads << ")\n";
        log(0);
        const auto t0 = std::chrono::steady_clock::now();
        int done = 0;
        while (done < steps)
        {
            const int chunk = std::min(diag, steps - done);
            run_steps(sim, c, chunk, *chosen->kernels, chosen->driver, threads);
            done += chunk;
            log(done);
            if (snap > 0 && done % snap == 0)
            {
                char name[64];
                std::snprintf(name, sizeof(name), "/water_%06d.npy", done);
                save_npy(out + name, extract(sim, sim.water()), H, W);
                std::snprintf(name, sizeof(name), "/terrain_%06d.npy", done);
                save_npy(out + name, extract(sim, sim.terrain()), H, W);
            }
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        save_npy(out + "/terrain_final.npy", extract(sim, sim.terrain()), H, W);
        save_npy(out + "/water_final.npy", extract(sim, sim.water()), H, W);
        save_npy(out + "/sediment_final.npy", extract(sim, sim.sediment()), H, W);
        save_npy(out + "/speed_final.npy", velocity_magnitude(sim, params), H, W);

        std::ofstream meta(out + "/meta.txt");
        meta << "W=" << W << "\nH=" << H << "\nsteps=" << steps << "\ndt=" << c.dt << "\ncell_size=" << params.cell_size
             << "\nvariant=" << chosen->id << "\nthreads=" << threads << "\nwall_time_s=" << secs
             << "\nrain_rate=" << params.rain_rate << "\nevaporation=" << params.evaporation
             << "\nKc=" << params.capacity << "\nKs=" << params.dissolve << "\nKd=" << params.deposit
             << "\nmin_tilt=" << params.min_tilt << "\nerosion_depth=" << params.erosion_depth << "\n";
        std::cout << "Gotovo za " << secs << " s (" << static_cast<double>(sim.cells()) * steps / secs / 1e6
                  << " Mcel/s, ukljucujuci dijagnostiku). Izlaz: " << out << "\n";
        return 0;
    }

    // ------------------------------------------------------------------ stream (propusnost memorije)
    int run_stream(const Args& a)
    {
        const std::vector<int> thread_list = parse_list(a.get("threads", "1,2,4,8"));
        const std::size_t mb = static_cast<std::size_t>(a.geti("mb", 128));
        const std::size_t n = mb * 1024 * 1024 / sizeof(float);
        AlignedBuffer A(n), B(n), C(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            B.data()[i] = 1.0f + static_cast<float>(i % 7);
            C.data()[i] = 2.0f;
        }
        const float s = 0.5f;
        std::filesystem::create_directories("results");
        std::ofstream out(a.get("out", "results/stream.csv"));
        out << "threads,array_mb,median_ms,min_ms,GBps_median,GBps_best\n";
        for (int p : thread_list)
        {
            float* pa = A.data();
            const float* pb = B.data();
            const float* pc = C.data();
            const long long nn = static_cast<long long>(n);
            const auto st = bench::measure(
                1, 10, [] {},
                [&] {
#if defined(PRS_HAS_OPENMP)
                    // MSVC (OpenMP 2.0 + /openmp:experimental) ne podržava kombinovani "parallel for simd"
                    // i ne vidi uhvaćenu varijablu u num_threads() unutar lambde -> lokalna kopija.
                    const int nt = p;
#if defined(_MSC_VER)
#pragma omp parallel for schedule(static) num_threads(nt)
#else
#pragma omp parallel for simd schedule(static) num_threads(nt)
#endif
#endif
                    for (long long i = 0; i < nn; ++i)
                        pa[i] = pb[i] + s * pc[i];
                },
                [&] { return static_cast<double>(pa[n / 2]); });
            const double bytes = 3.0 * sizeof(float) * static_cast<double>(n);
            std::cout << "Triad p=" << p << ": " << bytes / st.median_s / 1e9 << " GB/s (medijan), "
                      << bytes / st.min_s / 1e9 << " GB/s (najbolje)\n";
            out << p << ',' << mb << ',' << st.median_s * 1e3 << ',' << st.min_s * 1e3 << ','
                << bytes / st.median_s / 1e9 << ',' << bytes / st.min_s / 1e9 << '\n';
        }
        return 0;
    }
}

int main(int argc, char** argv)
{
    try
    {
        const Args a = parse(argc, argv);
        if (a.mode == "info")
        {
            print_info();
            return 0;
        }
        if (a.mode == "bench")
            return run_bench(a);
        if (a.mode == "simulate")
            return run_simulate(a);
        if (a.mode == "stream")
            return run_stream(a);
        std::cerr << "Nepoznat nacin rada: " << a.mode << " (info | bench | simulate | stream)\n";
        return 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Greska: " << e.what() << "\n";
        return 1;
    }
}
