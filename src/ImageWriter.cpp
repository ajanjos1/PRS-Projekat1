#include "ImageWriter.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>

// stb_image_write: implementacija samo u ovom fajlu, sa statičkim povezivanjem (ne izvozi simbole).
#if defined(_MSC_VER)
#pragma warning(push, 0)
#pragma warning(disable : 4996)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#include "../third_party/stb/stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace prs
{
    std::filesystem::path pathFromUtf8(const std::string& utf8)
    {
        const std::u8string u8(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
        return std::filesystem::path(u8);
    }

    std::string pathToUtf8(const std::filesystem::path& path)
    {
        const std::u8string u8 = path.u8string();
        return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
    }

    namespace
    {
        // ---------------------------------------------------------------- CRC32 (PNG specifikacija, Annex D)
        std::uint32_t crc32(const std::uint8_t* data, std::size_t length, std::uint32_t crc = 0xFFFFFFFFu)
        {
            static const std::array<std::uint32_t, 256> table = [] {
                std::array<std::uint32_t, 256> t{};
                for (std::uint32_t n = 0; n < 256; ++n)
                {
                    std::uint32_t c = n;
                    for (int k = 0; k < 8; ++k)
                        c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                    t[n] = c;
                }
                return t;
            }();
            for (std::size_t i = 0; i < length; ++i)
                crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
            return crc;
        }

        void putBigEndian32(std::vector<std::uint8_t>& out, std::uint32_t v)
        {
            out.push_back(static_cast<std::uint8_t>(v >> 24));
            out.push_back(static_cast<std::uint8_t>(v >> 16));
            out.push_back(static_cast<std::uint8_t>(v >> 8));
            out.push_back(static_cast<std::uint8_t>(v));
        }

        void appendChunk(std::vector<std::uint8_t>& out, const char type[4], const std::uint8_t* data,
                         std::size_t length)
        {
            putBigEndian32(out, static_cast<std::uint32_t>(length));
            const std::size_t typePos = out.size();
            out.insert(out.end(), type, type + 4);
            if (length > 0)
                out.insert(out.end(), data, data + length);
            const std::uint32_t crc = crc32(out.data() + typePos, length + 4) ^ 0xFFFFFFFFu;
            putBigEndian32(out, crc);
        }

        int paeth(int a, int b, int c)
        {
            const int p = a + b - c;
            const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
            if (pa <= pb && pa <= pc)
                return a;
            return pb <= pc ? b : c;
        }

        void writeFile(const std::string& path, const std::vector<std::uint8_t>& bytes)
        {
            std::ofstream out(pathFromUtf8(path), std::ios::binary | std::ios::trunc);
            if (!out)
                throw std::runtime_error("Ne mogu otvoriti datoteku za pisanje: " + path);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!out)
                throw std::runtime_error("Greska pri pisanju datoteke: " + path);
        }

        void appendBytes(void* context, void* data, int size)
        {
            auto* out = static_cast<std::vector<std::uint8_t>*>(context);
            const auto* p = static_cast<const std::uint8_t*>(data);
            out->insert(out->end(), p, p + size);
        }
    }

    std::vector<std::uint8_t> encodePng(int width, int height, int channels, int bitDepth, const void* pixels)
    {
        if (width <= 0 || height <= 0 || channels < 1 || channels > 4 || (bitDepth != 8 && bitDepth != 16))
            throw std::invalid_argument("encodePng: nepodrzan format");

        const std::size_t bytesPerSample = static_cast<std::size_t>(bitDepth / 8);
        const std::size_t bpp = bytesPerSample * static_cast<std::size_t>(channels); // bajtova po pikselu
        const std::size_t rowBytes = bpp * static_cast<std::size_t>(width);

        // Sirovi redovi u PNG redoslijedu bajtova (16 bita: big-endian).
        std::vector<std::uint8_t> raw(rowBytes * static_cast<std::size_t>(height));
        if (bitDepth == 8)
            std::memcpy(raw.data(), pixels, raw.size());
        else
        {
            const auto* src = static_cast<const std::uint16_t*>(pixels);
            for (std::size_t i = 0; i < raw.size() / 2; ++i)
            {
                raw[2 * i] = static_cast<std::uint8_t>(src[i] >> 8);
                raw[2 * i + 1] = static_cast<std::uint8_t>(src[i] & 0xFFu);
            }
        }

        // PNG filteri (None, Sub, Up, Average, Paeth); za svaki red bira se onaj s najmanjim zbirom
        // apsolutnih vrijednosti (uobičajena heuristika) – samo radi bolje kompresije, bez gubitka.
        std::vector<std::uint8_t> filtered((rowBytes + 1) * static_cast<std::size_t>(height));
        std::vector<std::uint8_t> candidate(rowBytes);
        for (int y = 0; y < height; ++y)
        {
            const std::uint8_t* row = raw.data() + rowBytes * static_cast<std::size_t>(y);
            const std::uint8_t* prev = y > 0 ? row - rowBytes : nullptr;
            std::uint8_t* dst = filtered.data() + (rowBytes + 1) * static_cast<std::size_t>(y);
            long bestScore = -1;
            for (int type = 0; type < 5; ++type)
            {
                long score = 0;
                for (std::size_t i = 0; i < rowBytes; ++i)
                {
                    const int a = i >= bpp ? row[i - bpp] : 0;
                    const int b = prev ? prev[i] : 0;
                    const int c = (prev && i >= bpp) ? prev[i - bpp] : 0;
                    int predictor = 0;
                    switch (type)
                    {
                    case 1: predictor = a; break;
                    case 2: predictor = b; break;
                    case 3: predictor = (a + b) / 2; break;
                    case 4: predictor = paeth(a, b, c); break;
                    default: break;
                    }
                    const auto value = static_cast<std::uint8_t>(row[i] - predictor);
                    candidate[i] = value;
                    score += value < 128 ? value : 256 - value;
                }
                if (bestScore < 0 || score < bestScore)
                {
                    bestScore = score;
                    dst[0] = static_cast<std::uint8_t>(type);
                    std::memcpy(dst + 1, candidate.data(), rowBytes);
                }
            }
        }

        int compressedLength = 0;
        unsigned char* compressed =
            stbi_zlib_compress(filtered.data(), static_cast<int>(filtered.size()), &compressedLength, 8);
        if (!compressed)
            throw std::runtime_error("encodePng: zlib kompresija nije uspjela");

        std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        std::vector<std::uint8_t> ihdr;
        putBigEndian32(ihdr, static_cast<std::uint32_t>(width));
        putBigEndian32(ihdr, static_cast<std::uint32_t>(height));
        ihdr.push_back(static_cast<std::uint8_t>(bitDepth));
        static const std::uint8_t colorType[5] = {0, 0, 4, 2, 6}; // 1 sivo, 2 sivo+alfa, 3 RGB, 4 RGBA
        ihdr.push_back(colorType[channels]);
        ihdr.push_back(0);                     // kompresija: deflate
        ihdr.push_back(0);                     // filter metoda 0
        ihdr.push_back(0);                     // bez interlace-a
        appendChunk(png, "IHDR", ihdr.data(), ihdr.size());
        appendChunk(png, "IDAT", compressed, static_cast<std::size_t>(compressedLength));
        appendChunk(png, "IEND", nullptr, 0);
        STBIW_FREE(compressed);
        return png;
    }

    void writePngGray16(const std::string& path, int width, int height, const std::vector<std::uint16_t>& pixels)
    {
        if (pixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
            throw std::invalid_argument("writePngGray16: pogresna velicina podataka");
        writeFile(path, encodePng(width, height, 1, 16, pixels.data()));
    }

    void writePngGray8(const std::string& path, int width, int height, const std::vector<std::uint8_t>& pixels)
    {
        if (pixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
            throw std::invalid_argument("writePngGray8: pogresna velicina podataka");
        writeFile(path, encodePng(width, height, 1, 8, pixels.data()));
    }

    void writePngRgb8(const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgb)
    {
        if (rgb.size() != 3u * static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
            throw std::invalid_argument("writePngRgb8: pogresna velicina podataka");
        writeFile(path, encodePng(width, height, 3, 8, rgb.data()));
    }

    void writeJpeg(const std::string& path, int width, int height, int channels, const std::vector<std::uint8_t>& data,
                   int quality)
    {
        if ((channels != 1 && channels != 3) ||
            data.size() != static_cast<std::size_t>(channels) * static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
            throw std::invalid_argument("writeJpeg: pogresni parametri");
        std::vector<std::uint8_t> bytes;
        if (!stbi_write_jpg_to_func(appendBytes, &bytes, width, height, channels, data.data(), quality))
            throw std::runtime_error("writeJpeg: kodiranje nije uspjelo");
        writeFile(path, bytes);
    }
}
