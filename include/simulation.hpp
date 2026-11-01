#pragma once
// Model: virtuelne cijevi (Mei, Decaudin, Hu 2007) + erozija/depozicija zasnovana na kapacitetu
// transporta + konzervativni (upwind) transport sedimenta kroz iste cijevi.
//
// Raspored podataka: Structure of Arrays (SoA), float, row-major, sa halo okvirom širine 1 ćelije.
// Unutrašnja ćelija (x, y), 0 <= x < W, 0 <= y < H, nalazi se na indeksu (y + 1) * pitch + kPadLeft + x.
// pitch je višekratnik od 8, a kPadLeft = 8, pa svaki unutrašnji red počinje na 32-bajtnoj granici.

#include "aligned_buffer.hpp"
#include "sw_common.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sw
{
    inline constexpr int kPadLeft = 8;    // unutrašnjost počinje u koloni 8; kolona 7 je lijevi halo
    inline constexpr int kSimdFloats = 8; // AVX2: 8 float vrijednosti u __m256

    // ------------------------------------------------------------------ parametri modela
    struct SimParams
    {
        float cell_size = 1.0f;     // l_x = l_y = L [m]
        float pipe_area = 1.0f;     // A, presjek virtuelne cijevi [m^2]
        float gravity = 9.81f;      // g [m/s^2]
        float dt = 0.05f;           // traženi vremenski korak [s]
        float cfl = 0.9f;           // C u uslovu dt <= C * L / sqrt(2 g A / L)
        float rain_rate = 0.002f;   // intenzitet kiše [m/s] (uniformno po domeni)
        float evaporation = 0.01f;  // K_e [1/s]
        float capacity = 0.6f;      // K_c, konstanta kapaciteta transporta sedimenta
        float dissolve = 0.6f;      // K_s [1/s], brzina erozije
        float deposit = 0.6f;       // K_d [1/s], brzina depozicije
        float min_tilt = 0.05f;     // donja granica za sin(alpha) (erozija i na ravnim dijelovima)
        float min_depth = 1e-3f;    // [m] ispod ove srednje dubine brzina se ne računa (u = v = 0)
        float erosion_depth = 0.1f; // [m] kapacitet linearno raste do ove dubine (ograničenje za vrlo plitku vodu)
        float sediment_eps = 1e-6f; // [m] ćelije s manjom dubinom ne šalju sediment
    };

    // Izvedene konstante koje koriste SVE implementacije (identičan redoslijed operacija).
    struct StepConstants
    {
        float dt;
        float rain_dt;        // r * dt
        float flux_coef;      // dt * A * g / l
        float cell_area;      // l_x * l_y
        float dt_over_area;   // dt / (l_x * l_y)
        float cell_size;      // L
        float inv_2L;         // 1 / (2 L), centralne razlike za nagib terena
        float half;           // 0.5
        float dt_ks;          // dt * K_s
        float dt_kd;          // dt * K_d
        float kc;             // K_c
        float min_tilt;
        float min_depth;
        float inv_erosion_depth;
        float sediment_eps;
        float evap_factor;    // 1 - K_e * dt
        float tiny;           // zaštita od dijeljenja nulom u K-faktoru
    };

    float cfl_dt_limit(const SimParams& p);           // stabilnosni limit eksplicitne šeme
    StepConstants make_step_constants(const SimParams& p); // dt = min(p.dt, CFL limit)

    // ------------------------------------------------------------------ polja (pokazivači)
    struct Fields
    {
        float* d;  // dubina vode
        float* s;  // suspendovani sediment (visina)
        float* sr; // pomoćno: sediment po jedinici izlaznog volumena (dt * s / V)
        float* fL; // izlazni protok kroz lijevu cijev
        float* fR; // ... desnu
        float* fT; // ... gornju (y - 1)
        float* fB; // ... donju (y + 1)
        const float* openL; // 0/1 maska po koloni: 0 za x = 0 (zatvoren zid)
        const float* openR; // 0/1 maska po koloni: 0 za x = W - 1
        std::size_t pitch;
        int width;
        int height;

        std::size_t row_offset(int y) const
        {
            return static_cast<std::size_t>(y + 1) * pitch + kPadLeft;
        }
    };

    // ------------------------------------------------------------------ stanje simulacije
    class Simulation
    {
    public:
        Simulation(int width, int height);

        int width() const { return _w; }
        int height() const { return _h; }
        std::size_t pitch() const { return _pitch; }
        std::size_t rows() const { return _rows; }
        std::size_t cells() const { return static_cast<std::size_t>(_w) * static_cast<std::size_t>(_h); }
        std::size_t index(int x, int y) const
        {
            return static_cast<std::size_t>(y + 1) * _pitch + kPadLeft + static_cast<std::size_t>(x);
        }

        // Teren je dvostruko baferovan (double buffering): faza 2 čita b_src, piše b_dst.
        float* terrain() { return _b[_cur].data(); }
        const float* terrain() const { return _b[_cur].data(); }
        float* terrain_next() { return _b[1 - _cur].data(); }
        void swap_terrain() { _cur = 1 - _cur; }

        float* water() { return _d.data(); }
        const float* water() const { return _d.data(); }
        float* sediment() { return _s.data(); }
        const float* sediment() const { return _s.data(); }
        const float* flux_left() const { return _fL.data(); }
        const float* flux_right() const { return _fR.data(); }
        const float* flux_top() const { return _fT.data(); }
        const float* flux_bottom() const { return _fB.data(); }
        const float* sediment_ratio() const { return _sr.data(); }

        Fields fields();

        // Halo terena: replikacija rubnih vrijednosti (Neumann), potrebna za nagib na rubu.
        void refresh_terrain_halo() { refresh_halo(terrain(), _w, _h, _pitch); }
        static void refresh_halo(float* b, int width, int height, std::size_t pitch);

        void copy_state_from(const Simulation& other);
        void clear_dynamic_state(); // voda, sediment i protoci na nulu

    private:
        int _w;
        int _h;
        std::size_t _pitch;
        std::size_t _rows;
        AlignedBuffer _b[2];
        int _cur = 0;
        AlignedBuffer _d, _s, _sr, _fL, _fR, _fT, _fB;
        AlignedBuffer _openL, _openR;
    };

    // ------------------------------------------------------------------ kerneli (po redu)
    // Svaka faza jednog vremenskog koraka je jedna potpuna petlja po mreži. Unutar faze ćelije su
    // nezavisne (svaka ćelija piše samo svoje vrijednosti), pa se redovi mogu dodijeliti dretvama,
    // a ćelije unutar reda SIMD trakama. Između faza je potrebna sinhronizacija (barijera).
    using FluxRowFn = void (*)(const Fields&, const StepConstants&, const float* b, int y);
    using WaterRowFn = void (*)(const Fields&, const StepConstants&, const float* b_src, float* b_dst, int y);
    using SedimentRowFn = void (*)(const Fields&, const StepConstants&, int y);

    struct KernelSet
    {
        const char* name;
        FluxRowFn flux;         // faza 1: kiša + izlazni protoci (virtuelne cijevi)
        WaterRowFn water;       // faza 2: dubina vode, brzina, erozija/depozicija, isparavanje
        SedimentRowFn sediment; // faza 3: transport sedimenta
        bool requires_avx2;     // fajl je preveden sa -mavx2 -> obavezna runtime provjera CPU-a
    };

    const KernelSet& kernels_scalar();
    const KernelSet& kernels_scalar_branchless(); // ablacija A2
    const KernelSet& kernels_auto();
    const KernelSet& kernels_omp_simd();
    const KernelSet& kernels_avx2();

    // ------------------------------------------------------------------ driveri
    enum class Driver
    {
        Serial,         // jedna dretva
        OmpParallelFor, // "#pragma omp parallel for schedule(static)" za svaku fazu
        OmpRegion       // jedna paralelna regija po pozivu, "#pragma omp for" + barijere
    };

    void run_serial(Simulation& sim, const StepConstants& c, int steps, const KernelSet& k);
    void run_omp_parallel_for(Simulation& sim, const StepConstants& c, int steps, const KernelSet& k, int threads);
    void run_omp_region(Simulation& sim, const StepConstants& c, int steps, const KernelSet& k, int threads);
    void run_steps(Simulation& sim, const StepConstants& c, int steps, const KernelSet& k, Driver driver, int threads);
    bool openmp_available();
    int openmp_max_threads();

    struct Variant
    {
        std::string id;    // npr. "F_omp_simd"
        std::string label; // opis za izvještaje
        const KernelSet* kernels;
        Driver driver;
        bool multithreaded;
    };

    std::vector<Variant> main_variants();     // A-G
    std::vector<Variant> ablation_variants(); // dodatne varijante (npr. OmpRegion)
    bool variant_supported(const Variant& v);  // AVX2 dostupnost na ovom CPU-u

    // ------------------------------------------------------------------ inicijalizacija i dijagnostika
    struct TerrainSpec
    {
        std::uint32_t seed = 20251u;
        double relief = 0.12; // visinska amplituda kao udio širine domene
        int octaves = 6;
    };

    void init_terrain(Simulation& sim, const SimParams& p, const TerrainSpec& spec = {});

    struct Totals
    {
        double water = 0;
        double terrain = 0;
        double sediment = 0;
        float min_water = 0;
        float max_water = 0;
        float min_sediment = 0;
        bool finite = true;
    };

    Totals compute_totals(const Simulation& sim);
    double state_checksum(const Simulation& sim);

    struct FieldDiff
    {
        double max_abs = 0;
        double max_rel = 0;
        std::size_t violations = 0;
        bool bitwise_equal = true;
    };

    struct StateDiff
    {
        FieldDiff terrain, water, sediment, flux;
        bool ok() const { return !terrain.violations && !water.violations && !sediment.violations && !flux.violations; }
        bool bitwise() const { return terrain.bitwise_equal && water.bitwise_equal && sediment.bitwise_equal && flux.bitwise_equal; }
        double max_abs() const;
    };

    StateDiff compare_states(const Simulation& ref, const Simulation& other, double abs_tol, double rel_tol);

    // Brzina toka za vizualizaciju (iz protoka i trenutne dubine).
    std::vector<float> velocity_magnitude(const Simulation& sim, const SimParams& p);
    std::vector<float> extract(const Simulation& sim, const float* field);

    // Podešavanje FTZ/DAZ (subnormalni brojevi -> 0) za tekuću dretvu; vidi cpu_features.cpp.
    void enable_flush_denormals();
}
