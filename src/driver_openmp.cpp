// OpenMP višedretveni driveri (varijante E, F, G i ablacija).
//
// Dekompozicija domene: mreža se dijeli po redovima (trake redova), schedule(static) dodjeljuje
// svakoj dretvi kontinuirani blok od ~H/p redova. Svaka faza čita susjede (stencil), ali piše samo
// vlastite ćelije, pa unutar faze nema race condition-a; zavisnosti između faza rješavaju implicitne
// barijere na kraju "omp for" / "omp parallel for". Teren je dvostruko baferovan (b_src -> b_dst),
// jer faza 2 i čita susjedne visine terena i mijenja teren.
//
// False sharing: dretve pišu u različite redove; granice blokova dijele najviše jednu liniju keša
// po nizu (redovi su poravnati na 32 B, pitch * 4 B >= 64 B), pa je efekat zanemariv.

#include "simulation.hpp"

#if defined(PRS_HAS_OPENMP)
#include <omp.h>
#endif

namespace sw
{
    bool openmp_available()
    {
#if defined(PRS_HAS_OPENMP)
        return true;
#else
        return false;
#endif
    }

    int openmp_max_threads()
    {
#if defined(PRS_HAS_OPENMP)
        return omp_get_max_threads();
#else
        return 1;
#endif
    }

    // Varijante E/F/G: "#pragma omp parallel for" za svaku fazu (fork-join po fazi).
    void run_omp_parallel_for(Simulation& sim, const StepConstants& c, int steps, const KernelSet& k, int threads)
    {
#if defined(PRS_HAS_OPENMP)
        // FTZ/DAZ se postavlja u svakoj dretvi tima (MXCSR je per-thread registar); libgomp ponovo
        // koristi iste dretve za naredne paralelne regije sa istim brojem dretvi.
#pragma omp parallel num_threads(threads)
        enable_flush_denormals();

        const Fields F = sim.fields();
        const int H = sim.height();

        for (int step = 0; step < steps; ++step)
        {
            sim.refresh_terrain_halo(); // O(W + H), serijski
            const float* b = sim.terrain();
            float* b_next = sim.terrain_next();

#pragma omp parallel for schedule(static) num_threads(threads)
            for (int y = 0; y < H; ++y)
                k.flux(F, c, b, y);

#pragma omp parallel for schedule(static) num_threads(threads)
            for (int y = 0; y < H; ++y)
                k.water(F, c, b, b_next, y);

#pragma omp parallel for schedule(static) num_threads(threads)
            for (int y = 0; y < H; ++y)
                k.sediment(F, c, y);

            sim.swap_terrain();
        }
#else
        (void)threads;
        run_serial(sim, c, steps, k);
#endif
    }

    // Ablacija: jedna paralelna regija za sve korake ("omp parallel" + "omp for"), čime se izbjegava
    // ponovno formiranje tima dretvi u svakoj fazi. Halo terena i zamjenu bafera radi jedna dretva
    // ("omp single", sa implicitnom barijerom).
    void run_omp_region(Simulation& sim, const StepConstants& c, int steps, const KernelSet& k, int threads)
    {
#if defined(PRS_HAS_OPENMP)
        const Fields F = sim.fields();
        const int H = sim.height();

#pragma omp parallel num_threads(threads)
        {
            enable_flush_denormals();
            for (int step = 0; step < steps; ++step)
            {
#pragma omp single
                sim.refresh_terrain_halo();

                const float* b = sim.terrain();
                float* b_next = sim.terrain_next();

#pragma omp for schedule(static)
                for (int y = 0; y < H; ++y)
                    k.flux(F, c, b, y);

#pragma omp for schedule(static)
                for (int y = 0; y < H; ++y)
                    k.water(F, c, b, b_next, y);

#pragma omp for schedule(static)
                for (int y = 0; y < H; ++y)
                    k.sediment(F, c, y);

#pragma omp single
                sim.swap_terrain();
            }
        }
#else
        (void)threads;
        run_serial(sim, c, steps, k);
#endif
    }
}
