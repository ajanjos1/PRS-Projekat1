// A) Skalarna referentna implementacija (i osnova za E - OpenMP dretve bez SIMD-a).
// Fajl se prevodi sa -fno-tree-vectorize (GCC) / -fno-vectorize -fno-slp-vectorize (Clang),
// a petlje su dodatno označene sa SW_NO_VECTORIZE. Provjera: izvještaj -fopt-info-vec mora biti prazan,
// a disasembler ne smije sadržavati pakovane instrukcije (mulps/addps/...).

#include "cell_ops.hpp"
#include "simulation.hpp"

namespace sw
{
    namespace
    {
        void flux_row(const Fields& F, const StepConstants& c, const float* b, int y)
        {
            const std::size_t row = F.row_offset(y);
            const float open_t = (y > 0) ? 1.0f : 0.0f;
            const float open_b = (y < F.height - 1) ? 1.0f : 0.0f;
            SW_NO_VECTORIZE
            for (int x = 0; x < F.width; ++x)
                ref::flux_cell(F, c, b, row + static_cast<std::size_t>(x), x, open_t, open_b);
        }

        void water_row(const Fields& F, const StepConstants& c, const float* bs, float* bd, int y)
        {
            const std::size_t row = F.row_offset(y);
            SW_NO_VECTORIZE
            for (int x = 0; x < F.width; ++x)
                ref::water_cell(F, c, bs, bd, row + static_cast<std::size_t>(x));
        }

        void sediment_row(const Fields& F, const StepConstants&, int y)
        {
            const std::size_t row = F.row_offset(y);
            SW_NO_VECTORIZE
            for (int x = 0; x < F.width; ++x)
                ref::sediment_cell(F, row + static_cast<std::size_t>(x));
        }
    }

    const KernelSet& kernels_scalar()
    {
        static const KernelSet k{"scalar", flux_row, water_row, sediment_row, false};
        return k;
    }
}
