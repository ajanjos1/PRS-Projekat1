#include "io_npy.hpp"

#include <cstdint>
#include <fstream>
#include <stdexcept>

namespace sw
{
    void save_npy(const std::string& path, const std::vector<float>& data, int rows, int cols)
    {
        if (data.size() != static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols))
            throw std::invalid_argument("save_npy: neispravne dimenzije");

        std::string header = "{'descr': '<f4', 'fortran_order': False, 'shape': (" + std::to_string(rows) + ", " +
                             std::to_string(cols) + "), }";
        // magic (6) + verzija (2) + dužina zaglavlja (2) + zaglavlje + '\n' mora biti višekratnik od 64
        const std::size_t base = 10 + header.size() + 1;
        header.append((64 - base % 64) % 64, ' ');
        header.push_back('\n');

        std::ofstream out(path, std::ios::binary);
        if (!out)
            throw std::runtime_error("Ne mogu otvoriti " + path);
        out.write("\x93NUMPY", 6);
        const char version[2] = {1, 0};
        out.write(version, 2);
        const auto len = static_cast<std::uint16_t>(header.size());
        const char len_bytes[2] = {static_cast<char>(len & 0xff), static_cast<char>(len >> 8)};
        out.write(len_bytes, 2);
        out.write(header.data(), static_cast<std::streamsize>(header.size()));
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size() * sizeof(float)));
    }
}
