#pragma once
// Minimalni zapis NumPy .npy (verzija 1.0, little-endian float32), za vizualizaciju u Pythonu.

#include <string>
#include <vector>

namespace sw
{
    void save_npy(const std::string& path, const std::vector<float>& data, int rows, int cols);
}
