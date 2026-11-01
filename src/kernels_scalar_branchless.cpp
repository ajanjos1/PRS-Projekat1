// A2) Ablacija: ISTI izvorni kod kao B (kernels_branchless.inl, bez grananja, __restrict), ali preveden
// BEZ vektorizacije (iste zastavice kao B + -fno-tree-vectorize). Razlika A -> A2 mjeri efekat uklanjanja
// grananja (pogrešna predviđanja skokova), a razlika A2 -> B čisti efekat SIMD vektorizacije.

#include "simulation.hpp"

#include <algorithm>
#include <cmath>

namespace sw
{
    namespace
    {
#define SW_BRANCHLESS_LOOP_HINT SW_NO_VECTORIZE
#include "kernels_branchless.inl"
#undef SW_BRANCHLESS_LOOP_HINT
    }

    const KernelSet& kernels_scalar_branchless()
    {
#if defined(PRS_VECTOR_REQUIRES_AVX2)
        static const KernelSet k{"scalar_branchless", flux_row, water_row, sediment_row, true};
#else
        static const KernelSet k{"scalar_branchless", flux_row, water_row, sediment_row, false};
#endif
        return k;
    }
}
