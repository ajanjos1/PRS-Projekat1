#include "HeightmapIO.hpp"

#include "ImageWriter.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

// Deklaracije iz stb_image.h (implementacija je u src/stb_image_impl.cpp).
#define STBI_NO_STDIO
#include "../third_party/stb/stb_image.h"

namespace prs
{
    const char* toString(ImageFormat f)
    {
        return f == ImageFormat::Png ? "PNG" : "JPEG";
    }

    namespace
    {
        std::string lowerExtension(const std::string& path)
        {
            std::string ext = pathToUtf8(pathFromUtf8(path).extension());
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return ext;
        }

        std::vector<unsigned char> readFileBytes(const std::string& path)
        {
            const std::filesystem::path p = pathFromUtf8(path);
            std::error_code ec;
            if (!std::filesystem::exists(p, ec))
                throw std::runtime_error("Datoteka ne postoji: " + path);
            if (std::filesystem::is_directory(p, ec))
                throw std::runtime_error("Putanja je folder, a ne datoteka: " + path);
            std::ifstream in(p, std::ios::binary);
            if (!in)
                throw std::runtime_error("Ne mogu otvoriti datoteku: " + path);
            std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (bytes.empty())
                throw std::runtime_error("Datoteka je prazna: " + path);
            if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                throw std::runtime_error("Datoteka je prevelika: " + path);
            return bytes;
        }

        // Format se određuje iz sadržaja (potpis datoteke), ne samo iz ekstenzije.
        bool isPngSignature(const std::vector<unsigned char>& b)
        {
            static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
            return b.size() >= 8 && std::equal(sig, sig + 8, b.begin());
        }

        bool isJpegSignature(const std::vector<unsigned char>& b)
        {
            return b.size() >= 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF;
        }

        struct StbFree
        {
            void operator()(stbi_us* p) const { stbi_image_free(p); }
        };
    }

    bool hasSupportedExtension(const std::string& path)
    {
        const std::string ext = lowerExtension(path);
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg";
    }

    LoadedRaster loadRaster(const std::string& path)
    {
        if (!hasSupportedExtension(path))
            throw std::runtime_error("Nepodržan format datoteke (dozvoljeno: .png, .jpg, .jpeg): " + path);

        const std::vector<unsigned char> bytes = readFileBytes(path);
        LoadedRaster out;
        RasterInfo& info = out.info;
        info.path = path;
        if (isPngSignature(bytes))
            info.format = ImageFormat::Png;
        else if (isJpegSignature(bytes))
            info.format = ImageFormat::Jpeg;
        else
            throw std::runtime_error("Sadržaj datoteke nije PNG ni JPEG slika: " + path);

        const int length = static_cast<int>(bytes.size());
        int w = 0, h = 0, comp = 0;
        if (!stbi_info_from_memory(bytes.data(), length, &w, &h, &comp))
            throw std::runtime_error(std::string("Ne mogu procitati zaglavlje slike: ") + stbi_failure_reason());
        info.bitsPerChannel = stbi_is_16_bit_from_memory(bytes.data(), length) ? 16 : 8;

        // Uvijek se učitava kao 16 bita po kanalu: 16-bitni PNG bez gubitka, 8-bitne vrijednosti v se
        // pretvaraju u v·257, pa je v16 / 65535 tačno jednako v8 / 255.
        std::unique_ptr<stbi_us, StbFree> pixels(stbi_load_16_from_memory(bytes.data(), length, &w, &h, &comp, 0));
        if (!pixels)
            throw std::runtime_error(std::string("Greska pri dekodiranju slike: ") + stbi_failure_reason());
        if (w <= 0 || h <= 0 || comp < 1 || comp > 4)
            throw std::runtime_error("Neispravne dimenzije ili broj kanala slike: " + path);

        info.width = w;
        info.height = h;
        info.channels = comp;
        info.alphaIgnored = (comp == 2 || comp == 4);
        out.normalized.resize(w, h);

        const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        const stbi_us* p = pixels.get();
        const double maxValue = 65535.0; // dijeli se (a ne množi recipročnom vrijednošću) radi tačnog zaokruživanja

        if (comp <= 2)
        {
            info.grayscaleMethod = "sivi kanal (bez konverzije)";
            for (std::size_t i = 0; i < n; ++i)
                out.normalized[i] = p[i * static_cast<std::size_t>(comp)] / maxValue;
        }
        else
        {
            bool equalChannels = true;
            for (std::size_t i = 0; i < n && equalChannels; ++i)
            {
                const stbi_us* px = p + i * static_cast<std::size_t>(comp);
                equalChannels = (px[0] == px[1] && px[1] == px[2]);
            }
            if (equalChannels)
            {
                info.grayscaleMethod = "R = G = B, uzet kanal R (bez gubitka)";
                for (std::size_t i = 0; i < n; ++i)
                    out.normalized[i] = p[i * static_cast<std::size_t>(comp)] / maxValue;
            }
            else
            {
                info.convertedFromColor = true;
                info.grayscaleMethod = "luma ITU-R BT.709 (0.2126 R + 0.7152 G + 0.0722 B)";
                for (std::size_t i = 0; i < n; ++i)
                {
                    const stbi_us* px = p + i * static_cast<std::size_t>(comp);
                    out.normalized[i] = (0.2126 * px[0] + 0.7152 * px[1] + 0.0722 * px[2]) / maxValue;
                }
            }
        }
        if (info.alphaIgnored)
            info.grayscaleMethod += "; alfa kanal zanemaren";
        return out;
    }

    Heightmap loadHeightmap(const std::string& path, const TerrainScale& scale, bool invert, RasterInfo* infoOut)
    {
        LoadedRaster raster = loadRaster(path);
        const std::string name = pathToUtf8(pathFromUtf8(path).filename());
        Heightmap map = makeHeightmap(raster.normalized, scale, invert, name);
        if (infoOut)
            *infoOut = raster.info;
        return map;
    }

    std::vector<std::uint16_t> quantizeTo16Bit(const Grid2D<double>& elevationMeters, const TerrainScale& scale,
                                               bool invert, QuantizationReport* report)
    {
        QuantizationReport rep;
        rep.lowestMeters = std::numeric_limits<double>::infinity();
        rep.highestMeters = -std::numeric_limits<double>::infinity();
        std::vector<std::uint16_t> out(elevationMeters.size());
        for (std::size_t i = 0; i < elevationMeters.size(); ++i)
        {
            const double h = elevationMeters[i];
            rep.lowestMeters = std::min(rep.lowestMeters, h);
            rep.highestMeters = std::max(rep.highestMeters, h);
            double v = scale.toNormalized(h);
            if (!(v >= 0.0)) // uključuje i NaN
            {
                ++rep.belowMinimum;
                v = 0.0;
            }
            else if (v > 1.0)
            {
                ++rep.aboveMaximum;
                v = 1.0;
            }
            if (invert)
                v = 1.0 - v;
            out[i] = static_cast<std::uint16_t>(std::lround(v * 65535.0));
        }
        if (report)
            *report = rep;
        return out;
    }

    QuantizationReport writeHeightmapPng16(const std::string& path, const Grid2D<double>& elevationMeters,
                                           const TerrainScale& scale, bool invert)
    {
        QuantizationReport rep;
        const std::vector<std::uint16_t> pixels = quantizeTo16Bit(elevationMeters, scale, invert, &rep);
        writePngGray16(path, elevationMeters.width(), elevationMeters.height(), pixels);
        return rep;
    }
}
