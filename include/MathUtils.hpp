#pragma once
// Male numeričke pomoćne funkcije.

#include <bit>
#include <cstdint>
#include <limits>

namespace prs
{
    static_assert(std::numeric_limits<double>::is_iec559, "Potreban je IEEE-754 double");

    // Kubni korijen za x > 0 (normalni brojevi). Koristi se u Manningovom trenju (h^(7/3) = h² · ∛h), koje se
    // računa za svaku aktivnu cijev u svakom koraku – std::cbrt je tu bio najskuplja operacija cijele simulacije.
    //
    // Početna procjena: eksponent IEEE-754 broja podijeljen sa 3 (cjelobrojno dijeljenje bitova, relativna
    // greška < 4 %), zatim dvije Halleyeve iteracije  y ← y (y³ + 2x) / (2y³ + x)  s kubnom konvergencijom:
    // 4·10⁻² → ~10⁻⁵ → ~10⁻¹⁵. Rezultat je tačan do zaokruživanja (test: relativna greška < 1e-13).
    inline double cubeRootPositive(double x)
    {
        const std::uint64_t bits = std::bit_cast<std::uint64_t>(x) / 3u + 0x2A9F7893782DA1CEull;
        double y = std::bit_cast<double>(bits);
        double y3 = y * y * y;
        y = y * (y3 + 2.0 * x) / (2.0 * y3 + x);
        y3 = y * y * y;
        y = y * (y3 + 2.0 * x) / (2.0 * y3 + x);
        return y;
    }
}
