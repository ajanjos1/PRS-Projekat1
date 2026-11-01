// Registar varijanti: varijanta = skup kernela (kako se računa red) + driver (ko računa koje redove).

#include "cpu_features.hpp"
#include "simulation.hpp"

#include <stdexcept>

namespace sw
{
    std::vector<Variant> main_variants()
    {
        return {
            {"A_scalar", "A: Scalar (serijski, bez SIMD)", &kernels_scalar(), Driver::Serial, false},
            {"B_auto", "B: Auto-vektorizacija", &kernels_auto(), Driver::Serial, false},
            {"C_omp_simd", "C: OpenMP SIMD", &kernels_omp_simd(), Driver::Serial, false},
            {"D_avx2", "D: AVX2 intrinsics", &kernels_avx2(), Driver::Serial, false},
            {"E_omp", "E: OpenMP parallel for (skalarno)", &kernels_scalar(), Driver::OmpParallelFor, true},
            {"F_omp_simd", "F: OpenMP parallel for + omp simd", &kernels_omp_simd(), Driver::OmpParallelFor, true},
            {"G_omp_avx2", "G: OpenMP parallel for + AVX2", &kernels_avx2(), Driver::OmpParallelFor, true},
        };
    }

    std::vector<Variant> ablation_variants()
    {
        return {
            {"A2_scalar_branchless", "A2: Skalarno bez grananja (izvor B, bez SIMD)", &kernels_scalar_branchless(), Driver::Serial, false},
            {"E2_omp_region", "E2: OpenMP regija + omp for (skalarno)", &kernels_scalar(), Driver::OmpRegion, true},
            {"F2_omp_simd_region", "F2: OpenMP regija + omp for + omp simd", &kernels_omp_simd(), Driver::OmpRegion, true},
            {"H_omp_auto", "H: OpenMP parallel for + auto-vektorizacija", &kernels_auto(), Driver::OmpParallelFor, true},
        };
    }

    bool variant_supported(const Variant& v)
    {
        if (v.kernels->requires_avx2 && !avx2_supported())
            return false;
        if (v.multithreaded && !openmp_available())
            return false;
        return true;
    }

    void run_steps(Simulation& sim, const StepConstants& c, int steps, const KernelSet& k, Driver driver, int threads)
    {
        if (k.requires_avx2 && !avx2_supported())
            throw std::runtime_error(std::string("CPU ne podržava AVX2, varijanta nije dozvoljena: ") + k.name);
        switch (driver)
        {
        case Driver::Serial:
            run_serial(sim, c, steps, k);
            break;
        case Driver::OmpParallelFor:
            run_omp_parallel_for(sim, c, steps, k, threads);
            break;
        case Driver::OmpRegion:
            run_omp_region(sim, c, steps, k, threads);
            break;
        }
    }
}
