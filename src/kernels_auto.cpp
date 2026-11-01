// B) Implementacija prilagođena automatskoj vektorizaciji kompajlera.
//  - SoA raspored i jednostavne unutrašnje petlje sa jediničnim korakom (x -> x + 1),
//  - pokazivači na redove (centar, gore, dolje) prosljeđuju se kao __restrict PARAMETRI funkcije:
//    GCC ne koristi pouzdano restrict na lokalnim pokazivačima izvedenim iz drugih pokazivača, pa je
//    prva verzija tražila previše runtime provjera preklapanja ("versioning for alias required") i
//    odustala; vidi results/vectorization/gcc_report_v1_initial.txt,
//  - uslovi zapisani kao min/max/ternarni operatori (if-conversion u SIMD maske),
//  - dijeljenja i korijeni se računaju bezuslovno (sa sigurnim imeniocem), a rezultat se zatim bira,
//  - sqrt se vektorizuje samo uz -fno-math-errno, a if-conversion petlji sa min/max i množenjem
//    rezultata zahtijeva -fno-trapping-math (fajl se tako prevodi; vrijednosti se NE mijenjaju,
//    jer program ne koristi FP izuzetke - testovi potvrđuju bit-identične rezultate sa referencom).
// Aritmetika je identična referenci iz cell_ops.hpp (isti redoslijed operacija).

#include "simulation.hpp"

#include <algorithm>
#include <cmath>

namespace sw
{
    namespace
    {
#define SW_BRANCHLESS_LOOP_HINT
#include "kernels_branchless.inl"
#undef SW_BRANCHLESS_LOOP_HINT
    }

    const KernelSet& kernels_auto()
    {
#if defined(PRS_VECTOR_REQUIRES_AVX2)
        static const KernelSet k{"auto", flux_row, water_row, sediment_row, true};
#else
        static const KernelSet k{"auto", flux_row, water_row, sediment_row, false};
#endif
        return k;
    }
}
