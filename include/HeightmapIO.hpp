#pragma once
// Učitavanje height mape iz PNG/JPEG datoteke i izvoz visina u 16-bitni PNG.
//
// Tumačenje piksela (podrazumijevano): crno = najniže, bijelo = najviše, sivo = linearno između.
//  - 16-bitni PNG: vrijednost / 65535 (puna preciznost se čuva)
//  - 8-bitni PNG/JPEG: vrijednost / 255
//  - siva + alfa: koristi se sivi kanal, alfa se zanemaruje
//  - RGB(A): ako su R = G = B u svim pikselima (siva slika snimljena kao RGB) uzima se R bez gubitka;
//            inače luma po ITU-R BT.709: Y = 0.2126 R + 0.7152 G + 0.0722 B (na sačuvanim vrijednostima).
// JPEG je kompresija s gubicima: blokovi 8×8 i "zvonjenje" oko oštrih ivica postaju lažne neravnine
// terena, a 8 bita daje samo 256 nivoa visine. Za ozbiljnu upotrebu preporučuje se 16-bitni PNG.

#include "Grid2D.hpp"
#include "Heightmap.hpp"

#include <cstdint>
#include <string>

namespace prs
{
    enum class ImageFormat
    {
        Png,
        Jpeg
    };

    const char* toString(ImageFormat f);

    struct RasterInfo
    {
        std::string path;
        ImageFormat format = ImageFormat::Png;
        int width = 0;
        int height = 0;
        int channels = 0;          // kanala u datoteci: 1 sivo, 2 sivo+alfa, 3 RGB, 4 RGBA
        int bitsPerChannel = 8;    // 8 ili 16
        bool convertedFromColor = false; // RGB sa različitim kanalima -> luma BT.709
        bool alphaIgnored = false;       // datoteka ima alfa kanal koji je zanemaren
        std::string grayscaleMethod;     // opis kako je dobijena siva vrijednost
    };

    struct LoadedRaster
    {
        RasterInfo info;
        Grid2D<double> normalized; // [0, 1]
    };

    // Dozvoljene ekstenzije: .png, .jpg, .jpeg (bez obzira na velika/mala slova).
    bool hasSupportedExtension(const std::string& path);

    // Baca std::runtime_error s jasnom porukom (nepostojeća datoteka, nepodržan format, oštećena slika...).
    LoadedRaster loadRaster(const std::string& path);

    // Učita sliku i pretvori je u teren u metrima prema zadatoj skali.
    Heightmap loadHeightmap(const std::string& path, const TerrainScale& scale, bool invert = false,
                            RasterInfo* infoOut = nullptr);

    // Rezultat pretvaranja visina [m] u 16-bitne vrijednosti (ista skala kao ulaz!).
    struct QuantizationReport
    {
        std::int64_t belowMinimum = 0; // vrijednosti ispod minElevation (odsječene na 0)
        std::int64_t aboveMaximum = 0; // vrijednosti iznad maxElevation (odsječene na 65535)
        double lowestMeters = 0.0;
        double highestMeters = 0.0;
        bool clipped() const { return belowMinimum > 0 || aboveMaximum > 0; }
    };

    // v16 = round(clamp((h − min) / (max − min), 0, 1) · 65535). invert = true vraća konvenciju ulaza.
    std::vector<std::uint16_t> quantizeTo16Bit(const Grid2D<double>& elevationMeters, const TerrainScale& scale,
                                               bool invert, QuantizationReport* report = nullptr);

    // Zapis terena kao 16-bitni sivi PNG u ISTOJ visinskoj skali kao ulaz (bez nezavisne normalizacije).
    QuantizationReport writeHeightmapPng16(const std::string& path, const Grid2D<double>& elevationMeters,
                                           const TerrainScale& scale, bool invert = false);
}
