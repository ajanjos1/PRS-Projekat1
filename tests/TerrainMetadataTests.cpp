// Metapodaci terena iz birača terena (tools/terrain_picker.html): parsiranje, provjere, preračun razmjere
// kad se slika smanji, traženje najnovijeg izvoza i kompatibilnost PNG-a koji je napravio preglednik.

#include "HeightmapIO.hpp"
#include "ImageWriter.hpp"
#include "TerrainMetadata.hpp"
#include "TestFramework.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace prs;
using namespace prs::test;
namespace fs = std::filesystem;

namespace
{
    const char* kValidJson = R"({
  "formatVersion": 1,
  "heightmapFile": "teren_N43.707_E018.266_512x512.png",
  "width": 512,
  "height": 256,
  "cellSizeMeters": 11.316229,
  "minElevationMeters": 1002.5,
  "maxElevationMeters": 1901.25,
  "bbox": { "south": 43.68, "west": 18.23, "north": 43.73, "east": 18.30 },
  "source": "AWS Terrain Tiles \"Terrarium\", zoom 14"
})";

    bool throwsRuntimeError(const std::string& json)
    {
        try
        {
            parseTerrainMetadata(json);
        }
        catch (const std::runtime_error&)
        {
            return true;
        }
        return false;
    }

    void writeText(const fs::path& p, const std::string& text)
    {
        std::ofstream(p, std::ios::binary) << text;
    }
}

PRS_TEST("TerrainMetadata", parsiranje_ispravnog_jsona)
{
    const TerrainMetadata m = parseTerrainMetadata(kValidJson);
    CHECK_NEAR(m.scale.cellSizeMeters, 11.316229, 1e-12);
    CHECK_NEAR(m.scale.minElevationMeters, 1002.5, 0.0);
    CHECK_NEAR(m.scale.maxElevationMeters, 1901.25, 0.0);
    CHECK(m.width == 512);
    CHECK(m.height == 256);
    CHECK(m.heightmapFile == "teren_N43.707_E018.266_512x512.png");
    CHECK(m.source == "AWS Terrain Tiles \"Terrarium\", zoom 14"); // escape \" se pravilno čita
    CHECK(m.hasBounds);
    CHECK_NEAR(m.south, 43.68, 1e-12);
    CHECK_NEAR(m.east, 18.30, 1e-12);
}

PRS_TEST("TerrainMetadata", nedostajuci_ili_neispravni_podaci_se_odbijaju)
{
    CHECK(throwsRuntimeError(R"({"minElevationMeters": 0, "maxElevationMeters": 100})"));       // nema cellSizeMeters
    CHECK(throwsRuntimeError(R"({"cellSizeMeters": 10, "maxElevationMeters": 100})"));          // nema min
    CHECK(throwsRuntimeError(R"({"cellSizeMeters": 10, "minElevationMeters": 0})"));            // nema max
    CHECK(throwsRuntimeError(R"({"cellSizeMeters": 0, "minElevationMeters": 0, "maxElevationMeters": 100})"));
    CHECK(throwsRuntimeError(R"({"cellSizeMeters": -5, "minElevationMeters": 0, "maxElevationMeters": 100})"));
    CHECK(throwsRuntimeError(R"({"cellSizeMeters": 10, "minElevationMeters": 100, "maxElevationMeters": 100})")); // max <= min
    CHECK(throwsRuntimeError(R"({"cellSizeMeters": "abc", "minElevationMeters": 0, "maxElevationMeters": 100})"));
    CHECK(throwsRuntimeError(R"({"formatVersion": 2, "cellSizeMeters": 10, "minElevationMeters": 0, "maxElevationMeters": 100})"));
    CHECK(throwsRuntimeError(""));
    // Bez opcionih polja je ispravno.
    const TerrainMetadata m = parseTerrainMetadata(R"({"cellSizeMeters": 10, "minElevationMeters": 0, "maxElevationMeters": 100})");
    CHECK(m.width == 0 && !m.hasBounds);
}

PRS_TEST("TerrainMetadata", tekst_u_stringu_nije_kljuc)
{
    // "cellSizeMeters" u vrijednosti stringa ne smije zamijeniti pravi ključ.
    const TerrainMetadata m = parseTerrainMetadata(
        R"({"note": "\"cellSizeMeters\": 999", "cellSizeMeters": 7.5, "minElevationMeters": -10, "maxElevationMeters": 90})");
    CHECK_NEAR(m.scale.cellSizeMeters, 7.5, 0.0);
    CHECK_NEAR(m.scale.minElevationMeters, -10.0, 0.0);
}

PRS_TEST("TerrainMetadata", ime_pratecih_fajlova)
{
    CHECK(terrainMetadataSidecar("a/b/teren_x.png").generic_string() == "a/b/teren_x.terrain.json");
    CHECK(terrainMetadataSidecar("teren.v2.png").generic_string() == "teren.v2.terrain.json"); // samo posljednji nastavak
}

PRS_TEST("TerrainMetadata", preracun_kad_je_slika_smanjena)
{
    TerrainMetadata m = parseTerrainMetadata(kValidJson); // 512 x 256, ćelija 11.316229
    bool rescaled = true;
    CHECK(adaptMetadataToImage(m, 512, 256, &rescaled).empty());
    CHECK(!rescaled);
    CHECK_NEAR(m.scale.cellSizeMeters, 11.316229, 1e-12);

    CHECK(adaptMetadataToImage(m, 256, 128, &rescaled).empty()); // upola manja => ćelija duplo veća
    CHECK(rescaled);
    CHECK_NEAR(m.scale.cellSizeMeters, 2 * 11.316229, 1e-9);
    CHECK(m.width == 256 && m.height == 128);

    TerrainMetadata q = parseTerrainMetadata(kValidJson);
    CHECK(adaptMetadataToImage(q, 2048, 1024, &rescaled).empty()); // uvećanje 4x => ćelija četiri puta manja
    CHECK_NEAR(q.scale.cellSizeMeters, 11.316229 / 4.0, 1e-9);

    TerrainMetadata bad = parseTerrainMetadata(kValidJson); // izrezana/razvučena slika: omjer 1:1 umjesto 2:1
    const std::string reason = adaptMetadataToImage(bad, 512, 512, &rescaled);
    CHECK(!reason.empty());
    CHECK(!rescaled);
    CHECK_NEAR(bad.scale.cellSizeMeters, 11.316229, 1e-12); // nepromijenjeno

    TerrainMetadata unknown = parseTerrainMetadata(R"({"cellSizeMeters": 10, "minElevationMeters": 0, "maxElevationMeters": 100})");
    CHECK(adaptMetadataToImage(unknown, 100, 100).empty()); // dimenzije nepoznate => ne mijenja ništa
    CHECK_NEAR(unknown.scale.cellSizeMeters, 10.0, 0.0);
}

PRS_TEST("TerrainMetadata", najnoviji_izvoz_u_folderu)
{
    const fs::path dir = fs::temp_directory_path() / "prs_terrain_export_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    CHECK(!findLatestTerrainExport(dir).has_value()); // prazan folder
    CHECK(!findLatestTerrainExport(dir / "nema").has_value());

    const std::string json = R"({"cellSizeMeters": 10, "minElevationMeters": 0, "maxElevationMeters": 100})";
    writeText(dir / "stari.png", "x");
    writeText(dir / "stari.terrain.json", json);
    writeText(dir / "novi.png", "x");
    writeText(dir / "novi.terrain.json", json);
    writeText(dir / "bez_slike.terrain.json", json); // nema pripadajućeg PNG-a – ne računa se
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(dir / "stari.terrain.json", now - std::chrono::hours(5));
    fs::last_write_time(dir / "novi.terrain.json", now - std::chrono::minutes(2));
    fs::last_write_time(dir / "bez_slike.terrain.json", now); // najsvježiji, ali bez slike

    const auto found = findLatestTerrainExport(dir);
    CHECK(found.has_value());
    if (found)
    {
        CHECK(found->json.filename().string() == "novi.terrain.json");
        CHECK(found->png.filename().string() == "novi.png");
    }
    fs::remove_all(dir, ec);
}

PRS_TEST("TerrainMetadata", png_iz_preglednika_se_ucitava_sa_skalom)
{
    // Uzorak koji je napravio tools/terrain_picker.html (JS kodiranje 16-bitnog PNG-a + metapodaci).
    const fs::path png = "tests/data/terrain_picker_sample.png";
    if (!fs::exists(png))
    {
        note("tests/data nije pronadjen iz tekuceg foldera - preskoceno (pokrenuti iz korijena projekta)");
        return;
    }
    TerrainMetadata meta = loadTerrainMetadata(terrainMetadataSidecar(png));
    CHECK(meta.width == 512 && meta.height == 512);
    CHECK(meta.hasBounds);

    RasterInfo info;
    const Heightmap map = loadHeightmap(pathToUtf8(png), meta.scale, false, &info);
    CHECK(info.width == 512 && info.height == 512);
    CHECK(info.bitsPerChannel == 16);
    CHECK(info.channels == 1);
    CHECK(adaptMetadataToImage(meta, info.width, info.height).empty());

    double lo = 1e300, hi = -1e300;
    for (std::size_t i = 0; i < map.elevation.size(); ++i)
    {
        lo = std::min(lo, map.elevation[i]);
        hi = std::max(hi, map.elevation[i]);
    }
    // Crni piksel = min, bijeli = max (JS normalizacija na cijeli 16-bitni raspon).
    CHECK_NEAR(lo, meta.scale.minElevationMeters, 1e-6);
    CHECK_NEAR(hi, meta.scale.maxElevationMeters, 1e-6);
    note("uzorak: " + fmt(meta.scale.minElevationMeters, 7) + " .. " + fmt(meta.scale.maxElevationMeters, 7) + " m, ćelija " +
         fmt(meta.scale.cellSizeMeters, 6) + " m");
}
