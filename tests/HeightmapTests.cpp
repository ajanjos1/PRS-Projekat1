// Test 10 – uvoz/izvoz height mapa (AC1, AC5): PNG 8/16 bita, RGB, siva+alfa, JPEG, nekvadratne mape,
// ista visinska skala na ulazu i izlazu, zaštita ulazne datoteke, greške pri učitavanju.

#include "ErosionSimulation.hpp"
#include "HeightmapIO.hpp"
#include "ImageWriter.hpp"
#include "ResultExport.hpp"
#include "TestFramework.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace prs;
using namespace prs::test;
namespace fs = std::filesystem;

namespace
{
    std::string tempDir()
    {
        const fs::path dir = fs::temp_directory_path() / "prs_erosion_tests";
        fs::create_directories(dir);
        return pathToUtf8(dir);
    }

    std::string tempFile(const std::string& name)
    {
        return pathToUtf8(pathFromUtf8(tempDir()) / name);
    }

    std::vector<char> readAll(const std::string& path)
    {
        std::ifstream in(pathFromUtf8(path), std::ios::binary);
        return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    constexpr int kW = 37; // namjerno nekvadratno i ne višekratnik od 8
    constexpr int kH = 23;

    // Gradijent s jedinstvenom vrijednošću po pikselu (provjerava i orijentaciju: gore-lijevo != dolje-desno).
    std::uint16_t pattern16(int x, int y) { return static_cast<std::uint16_t>((x * 1499 + y * 977 + 13) % 65536); }
    std::uint8_t pattern8(int x, int y) { return static_cast<std::uint8_t>((x * 7 + y * 3) % 256); }
}

PRS_TEST("Heightmap", png16_ucitavanje_bez_gubitka)
{
    std::vector<std::uint16_t> px(kW * kH);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            px[y * kW + x] = pattern16(x, y);
    const std::string path = tempFile("gray16.png");
    writePngGray16(path, kW, kH, px);
    const LoadedRaster r = loadRaster(path);
    CHECK(r.info.format == ImageFormat::Png);
    CHECK(r.info.width == kW && r.info.height == kH);
    CHECK(r.info.bitsPerChannel == 16);
    CHECK(r.info.channels == 1);
    bool exact = true;
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            exact &= (r.normalized(x, y) == pattern16(x, y) / 65535.0);
    CHECK(exact);
}

PRS_TEST("Heightmap", png8_rgb_i_alfa)
{
    // 8-bitna siva
    std::vector<std::uint8_t> gray(kW * kH);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            gray[y * kW + x] = pattern8(x, y);
    writePngGray8(tempFile("gray8.png"), kW, kH, gray);
    LoadedRaster r = loadRaster(tempFile("gray8.png"));
    bool exact = r.info.bitsPerChannel == 8 && r.info.channels == 1;
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            exact &= (r.normalized(x, y) == pattern8(x, y) / 255.0);
    CHECK(exact);

    // RGB sa R = G = B (siva slika snimljena kao RGB) -> bez gubitka
    std::vector<std::uint8_t> rgb(kW * kH * 3);
    for (std::size_t i = 0; i < gray.size(); ++i)
        rgb[3 * i] = rgb[3 * i + 1] = rgb[3 * i + 2] = gray[i];
    writePngRgb8(tempFile("rgb_gray.png"), kW, kH, rgb);
    r = loadRaster(tempFile("rgb_gray.png"));
    CHECK(r.info.channels == 3 && !r.info.convertedFromColor);
    CHECK(r.normalized(5, 7) == pattern8(5, 7) / 255.0);

    // Pravi RGB -> luma BT.709
    rgb[0] = 255;
    rgb[1] = 0;
    rgb[2] = 0; // piksel (0,0) crven
    writePngRgb8(tempFile("rgb_color.png"), kW, kH, rgb);
    r = loadRaster(tempFile("rgb_color.png"));
    CHECK(r.info.convertedFromColor);
    CHECK_NEAR(r.normalized(0, 0), 0.2126, 1e-12);

    // Siva + alfa (2 kanala): koristi se sivi kanal, alfa se zanemaruje
    std::vector<std::uint8_t> ga(kW * kH * 2);
    for (std::size_t i = 0; i < gray.size(); ++i)
    {
        ga[2 * i] = gray[i];
        ga[2 * i + 1] = static_cast<std::uint8_t>(i % 256);
    }
    const std::vector<std::uint8_t> png = encodePng(kW, kH, 2, 8, ga.data());
    {
        std::ofstream out(pathFromUtf8(tempFile("gray_alpha.png")), std::ios::binary);
        out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    }
    r = loadRaster(tempFile("gray_alpha.png"));
    CHECK(r.info.channels == 2 && r.info.alphaIgnored);
    CHECK(r.normalized(9, 4) == pattern8(9, 4) / 255.0);
}

PRS_TEST("Heightmap", jpeg_ucitavanje)
{
    // Glatka siva slika kao JPEG (kvalitet 95): vrijednosti blizu originala (gubitak zbog kompresije).
    std::vector<std::uint8_t> gray(kW * kH);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            gray[y * kW + x] = static_cast<std::uint8_t>(40 + 4 * x + 2 * y);
    for (const char* name : {"smooth.jpg", "smooth.jpeg", "SMOOTH.JPG"})
    {
        writeJpeg(tempFile(name), kW, kH, 1, gray, 95);
        const LoadedRaster r = loadRaster(tempFile(name));
        CHECK(r.info.format == ImageFormat::Jpeg);
        CHECK(r.info.width == kW && r.info.height == kH);
        double worst = 0.0;
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                worst = std::max(worst, std::abs(r.normalized(x, y) * 255.0 - gray[y * kW + x]));
        CHECK_LE(worst, 6.0);
    }
}

PRS_TEST("Heightmap", greske_pri_ucitavanju)
{
    bool threw = false;
    try { loadRaster(tempFile("ne_postoji.png")); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    threw = false;
    {
        std::ofstream out(pathFromUtf8(tempFile("slika.bmp")), std::ios::binary);
        out << "BM....";
    }
    try { loadRaster(tempFile("slika.bmp")); } catch (const std::exception&) { threw = true; }
    CHECK(threw); // nepodržana ekstenzija

    threw = false;
    {
        std::ofstream out(pathFromUtf8(tempFile("osteceno.png")), std::ios::binary);
        out << "ovo nije png";
    }
    try { loadRaster(tempFile("osteceno.png")); } catch (const std::exception&) { threw = true; }
    CHECK(threw); // sadržaj nije slika

    threw = false;
    try { loadRaster(tempDir()); } catch (const std::exception&) { threw = true; }
    CHECK(threw); // folder umjesto datoteke

    CHECK(hasSupportedExtension("a.PNG") && hasSupportedExtension("b.jpeg") && hasSupportedExtension("c.Jpg"));
    CHECK(!hasSupportedExtension("d.tif") && !hasSupportedExtension("e"));
}

PRS_TEST("Heightmap", skala_i_inverzija)
{
    Grid2D<double> v(3, 2);
    v(0, 0) = 0.0;
    v(1, 0) = 0.5;
    v(2, 0) = 1.0;
    TerrainScale scale;
    scale.minElevationMeters = 100.0;
    scale.maxElevationMeters = 300.0;
    Heightmap m = makeHeightmap(v, scale, false, "t");
    CHECK(m.elevation(0, 0) == 100.0 && m.elevation(1, 0) == 200.0 && m.elevation(2, 0) == 300.0);
    Heightmap inv = makeHeightmap(v, scale, true, "t");
    CHECK(inv.elevation(0, 0) == 300.0 && inv.elevation(2, 0) == 100.0);
    TerrainScale bad;
    bad.maxElevationMeters = bad.minElevationMeters;
    CHECK(!bad.validate().empty());
    bad = TerrainScale{};
    bad.cellSizeMeters = 0.0;
    CHECK(!bad.validate().empty());
}

PRS_TEST("Heightmap", izvoz_ista_skala_dimenzije_orijentacija)
{
    // Visine u metrima -> 16-bitni PNG -> ponovno učitavanje: iste dimenzije, ista orijentacija, greška
    // najviše pola nivoa kvantizacije (raspon / 65535 / 2).
    TerrainScale scale;
    scale.minElevationMeters = -20.0;
    scale.maxElevationMeters = 180.0;
    Grid2D<double> h(kW, kH);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            h(x, y) = -20.0 + 200.0 * (0.1 + 0.8 * x / (kW - 1.0)) * (0.5 + 0.5 * y / (kH - 1.0));
    const std::string path = tempFile("export16.png");
    const QuantizationReport rep = writeHeightmapPng16(path, h, scale);
    CHECK(!rep.clipped());
    const LoadedRaster r = loadRaster(path);
    CHECK(r.info.width == kW && r.info.height == kH && r.info.bitsPerChannel == 16);
    double worst = 0.0;
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            worst = std::max(worst, std::abs(scale.toMeters(r.normalized(x, y)) - h(x, y)));
    CHECK_LE(worst, 200.0 / 65535.0 / 2.0 + 1e-9);
    CHECK(r.normalized(0, 0) < r.normalized(kW - 1, kH - 1)); // orijentacija sačuvana

    // 8-bitni ulaz -> 16-bitni izlaz bez promjene: v8/255 = v16/65535 tačno (v16 = 257·v8)
    std::vector<std::uint8_t> gray(kW * kH);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            gray[y * kW + x] = pattern8(x, y);
    writePngGray8(tempFile("in8.png"), kW, kH, gray);
    const Heightmap m = loadHeightmap(tempFile("in8.png"), scale);
    writeHeightmapPng16(tempFile("out16.png"), m.elevation, scale);
    const LoadedRaster back = loadRaster(tempFile("out16.png"));
    bool exact = true;
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            exact &= std::abs(back.normalized(x, y) - pattern8(x, y) / 255.0) < 1e-12;
    CHECK(exact);
}

PRS_TEST("Heightmap", vrijednosti_izvan_raspona_upozorenje)
{
    TerrainScale scale; // 0..100 m
    Grid2D<double> h(4, 1);
    h[0] = -0.5;  // ispod minimuma (erozija na najnižoj tački)
    h[1] = 50.0;
    h[2] = 100.25; // iznad maksimuma (depozicija na vrhu)
    h[3] = 101.0;
    QuantizationReport rep;
    const std::vector<std::uint16_t> q = quantizeTo16Bit(h, scale, false, &rep);
    CHECK(rep.clipped());
    CHECK(rep.belowMinimum == 1 && rep.aboveMaximum == 2);
    CHECK(q[0] == 0 && q[2] == 65535 && q[3] == 65535);
    CHECK_NEAR(rep.lowestMeters, -0.5, 0.0);
    CHECK_NEAR(rep.highestMeters, 101.0, 0.0);
}

PRS_TEST("Heightmap", cijeli_tok_nekvadratna_mapa_i_ulaz_netaknut)
{
    // Test 10: PNG ulaz (nekvadratan) -> simulacija -> izvoz. Ulazna datoteka ostaje bajt-identična.
    const int W = 45, H = 28;
    std::vector<std::uint16_t> px(W * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            px[y * W + x] = static_cast<std::uint16_t>(60000 - 1000 * x - 300 * std::abs(y - H / 2));
    const std::string input = tempFile("nekvadratna.png");
    writePngGray16(input, W, H, px);
    const std::vector<char> inputBytes = readAll(input);

    TerrainScale scale;
    scale.cellSizeMeters = 10.0;
    RasterInfo info;
    const Heightmap map = loadHeightmap(input, scale, false, &info);
    SimulationConfig cfg;
    cfg.rainfall.totalMillimeters = 40.0;
    cfg.rainfall.durationHours = 0.5;
    ErosionSimulation sim(map, cfg);
    sim.run();

    RunDescription run;
    run.inputPath = input;
    run.raster = info;
    run.scale = scale;
    run.config = cfg;
    run.programVersion = "test";
    ExportOptions opt;
    opt.outputDirectory = tempFile("izlaz");
    opt.baseName = "nekvadratna";
    opt.writeCsv = true;
    const ExportResult ex = exportResults(opt, run, sim);
    for (const auto& f : ex.files)
        CHECK(fs::exists(pathFromUtf8(f)));
    CHECK(fs::exists(pathFromUtf8(ex.reportTxt)) && fs::exists(pathFromUtf8(ex.reportJson)));
    const LoadedRaster eroded = loadRaster(ex.erodedPng);
    CHECK(eroded.info.width == W && eroded.info.height == H && eroded.info.bitsPerChannel == 16);
    // Izlazna mapa odgovara simuliranim visinama (do pola nivoa kvantizacije)
    double worst = 0.0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            worst = std::max(worst, std::abs(scale.toMeters(eroded.normalized(x, y)) - sim.state().terrain(x, y)));
    CHECK_LE(worst, 100.0 / 65535.0 / 2.0 + 1e-9);
    CHECK(readAll(input) == inputBytes); // original nepromijenjen
    CHECK(sim.statistics().grossErodedVolume > 0.0);

    // Izlaz u isti folder i s istim imenom kao ulaz bi prepisao ulaz -> mora biti odbijen.
    ExportOptions clash;
    clash.outputDirectory = tempDir();
    clash.baseName = "nekvadratna";
    run.inputPath = tempFile("nekvadratna_eroded.png");
    writePngGray16(run.inputPath, W, H, px);
    bool threw = false;
    try { exportResults(clash, run, sim); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

PRS_TEST("Heightmap", primjeri_iz_assets)
{
    // Stvarne height mape iz priloga (assets/heightmaps), ako su dostupne iz tekućeg foldera.
    struct Sample
    {
        const char* path;
        int width, height, bits;
    };
    const Sample samples[] = {
        {"assets/heightmaps/primjer_1/Height_map_input_try_1_height.png", 412, 221, 8},
        {"assets/heightmaps/primjer_1/height-map-input-try-1-height-map-16bit-texturemap-app-2026-11-08.png", 412, 221, 16},
        {"assets/heightmaps/primjer_2/mocvarno_tlo_height.png", 554, 554, 8},
        {"assets/heightmaps/primjer_2/mocvarno_tlo.jpeg", 554, 554, 8},
        {"assets/heightmaps/primjer_3/mali-otocic-height-map-16bit-texturemap-app-2026-11-08.png", 736, 736, 16},
        {"assets/heightmaps/primjer_3/mali_otocic.jpg", 736, 736, 8},
    };
    int found = 0;
    for (const Sample& s : samples)
    {
        if (!fs::exists(s.path))
            continue;
        ++found;
        const LoadedRaster r = loadRaster(s.path);
        double lo = 1.0, hi = 0.0;
        for (double v : r.normalized.values())
            lo = std::min(lo, v), hi = std::max(hi, v);
        note(std::string(s.path) + ": " + std::to_string(r.info.width) + "x" + std::to_string(r.info.height) + ", " +
             std::to_string(r.info.bitsPerChannel) + " bit, " + std::to_string(r.info.channels) + " kanal(a), " +
             r.info.grayscaleMethod + ", raspon [" + fmt(lo) + ", " + fmt(hi) + "]");
        CHECK(r.info.width == s.width && r.info.height == s.height && r.info.bitsPerChannel == s.bits);
        CHECK(lo >= 0.0 && hi <= 1.0 && hi > lo);
    }
    if (found == 0)
        note("assets/heightmaps nije pronadjen iz tekuceg foldera - preskoceno (pokrenuti iz korijena projekta)");
}
