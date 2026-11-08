#pragma once
// Zapis slika: PNG (8-bitni sivi, 16-bitni sivi, 8-bitni RGB) i JPEG (za testove uvoza).
//
// stb_image_write zna pisati samo 8-bitni PNG, a za izlaznu height mapu želimo 16 bita (65536 nivoa visine).
// Zato je PNG koder ovdje vlastit (IHDR/IDAT/IEND + CRC32 + PNG filteri), a od stb se koristi samo zlib
// kompresija. Putanje se primaju kao UTF-8 (std::filesystem), pa rade i dijakritici u Windows putanjama.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace prs
{
    // UTF-8 string -> std::filesystem::path (na Windowsu se ispravno pretvara u UTF-16).
    std::filesystem::path pathFromUtf8(const std::string& utf8);
    std::string pathToUtf8(const std::filesystem::path& path);

    // Svi zapisi bacaju std::runtime_error ako zapis ne uspije.
    void writePngGray16(const std::string& path, int width, int height, const std::vector<std::uint16_t>& pixels);
    void writePngGray8(const std::string& path, int width, int height, const std::vector<std::uint8_t>& pixels);
    void writePngRgb8(const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgb);
    // channels: 1 (sivo) ili 3 (RGB); quality 1–100.
    void writeJpeg(const std::string& path, int width, int height, int channels, const std::vector<std::uint8_t>& data,
                   int quality);

    // Kodiranje PNG-a u memoriju (bitDepth 8 ili 16; channels 1 = sivo, 2 = sivo+alfa, 3 = RGB, 4 = RGBA).
    // Podaci su redovi odozgo nadolje,
    // 16-bitne vrijednosti u nativnom redoslijedu bajtova (koder ih pretvara u big-endian kako PNG traži).
    std::vector<std::uint8_t> encodePng(int width, int height, int channels, int bitDepth, const void* pixels);
}
