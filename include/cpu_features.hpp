#pragma once

#include <string>

namespace sw
{
    // Runtime provjera AVX2 (CPUID + XGETBV), kao avx2_supported() u bench.cpp sa vježbi.
    bool avx2_supported();

    // Naziv procesora iz CPUID (brand string); prazan string ako nije dostupno.
    std::string cpu_brand();

    // Opis builda (kompajler, zastavice, OpenMP) generisan iz CMake-a.
    std::string build_description();
}
