#pragma once
// Zajednički makroi za sve implementacije (prilagođeno iz bench.hpp sa vježbi).

#include <cstddef>

#if defined(_MSC_VER)
#define SW_RESTRICT __restrict
#define SW_NOINLINE __declspec(noinline)
#else
#define SW_RESTRICT __restrict__
#define SW_NOINLINE __attribute__((noinline))
#endif

// Isključivanje vektorizacije pojedinačne petlje (isti princip kao PERF_NO_VECTORIZE).
// Napomena: GCC < 14 nema "GCC novector", pa se za skalarni fajl u CMake-u dodaje
// -fno-tree-vectorize -fno-tree-loop-vectorize -fno-tree-slp-vectorize.
#if defined(__clang__)
#define SW_NO_VECTORIZE _Pragma("clang loop vectorize(disable) interleave(disable)")
#elif defined(_MSC_VER)
#define SW_NO_VECTORIZE __pragma(loop(no_vector))
#elif defined(__GNUC__) && __GNUC__ >= 14
#define SW_NO_VECTORIZE _Pragma("GCC novector")
#else
#define SW_NO_VECTORIZE
#endif

// OpenMP SIMD direktiva (radi i bez OpenMP runtime-a uz -fopenmp-simd).
#if defined(PRS_HAS_OMP_SIMD)
#if defined(_MSC_VER)
#define SW_OMP_SIMD __pragma(omp simd)
#else
#define SW_OMP_SIMD _Pragma("omp simd")
#endif
#else
#define SW_OMP_SIMD
#endif
