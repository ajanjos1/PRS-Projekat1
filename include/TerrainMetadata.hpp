#pragma once
// TerrainMetadata – metapodaci terena iz birača terena (tools/terrain_picker.html).
//
// Birač uz height mapu (<ime>.png) pravi i <ime>.terrain.json sa stvarnom skalom: veličinom ćelije u metrima te
// visinama koje odgovaraju crnom (0) i bijelom (65535) pikselu. Program ih učita umjesto ručnog unosa.
//
// JSON je plitak (brojevi i stringovi), pa ga čita mali vlastiti parser: traži ključ po imenu, bez vanjskih
// biblioteka. Nepoznati ključevi se ignorišu, što dopušta nova polja bez promjene programa.

#include "Heightmap.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace prs
{
    struct TerrainMetadata
    {
        TerrainScale scale;          // veličina ćelije [m], visina crnog i bijelog piksela [m]
        int width = 0;               // dimenzije slike u trenutku izvoza (0 = nepoznato)
        int height = 0;
        std::string source;          // npr. "AWS Terrain Tiles (Mapzen), Terrarium, zoom 14"
        std::string heightmapFile;   // ime PNG-a pri izvozu
        bool hasBounds = false;      // geografske granice (stepeni), ako ih JSON sadrži
        double south = 0.0, west = 0.0, north = 0.0, east = 0.0;
        std::string jsonPath;        // odakle je učitano (samo za ispis)
    };

    // Parsira tekst JSON-a. Baca std::runtime_error s razlogom (nedostaje ključ, neispravna skala...).
    TerrainMetadata parseTerrainMetadata(const std::string& jsonText);

    // Čita fajl (UTF-8 bez ili sa BOM-om). Baca std::runtime_error.
    TerrainMetadata loadTerrainMetadata(const std::filesystem::path& file);

    // <folder slike>/<ime slike bez nastavka>.terrain.json
    std::filesystem::path terrainMetadataSidecar(const std::filesystem::path& image);

    // Ako je slika kasnije smanjena/povećana (druge dimenzije od izvoznih, ali isti omjer stranica), preračuna
    // veličinu ćelije tako da pokriva isto područje. `rescaled` (opciono) postaje true ako je do toga došlo.
    // Vraća prazan string ako je sve u redu, a inače razlog odbijanja (npr. promijenjen omjer stranica).
    std::string adaptMetadataToImage(TerrainMetadata& metadata, int imageWidth, int imageHeight, bool* rescaled = nullptr);

    struct TerrainExport
    {
        std::filesystem::path json;
        std::filesystem::path png;
        std::filesystem::file_time_type modified;
    };

    // Najnoviji par <ime>.png + <ime>.terrain.json u folderu (npr. Preuzimanja); prazno ako ga nema.
    std::optional<TerrainExport> findLatestTerrainExport(const std::filesystem::path& folder);
}
