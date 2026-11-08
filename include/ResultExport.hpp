#pragma once
// Izvoz rezultata simulacije: erodirana height mapa (16-bitni PNG, ista visinska skala kao ulaz), kopija
// ulaza u istom formatu, vizualizacije (mapa razlike, hillshade poređenje, maksimalna dubina vode),
// sirovi podaci bez gubitka preciznosti (.npy float64, opciono CSV) i izvještaj (TXT + JSON).
//
// Vizualizacije su ZASEBNE slike i nikad ne mijenjaju izlaznu height mapu.

#include "ErosionSimulation.hpp"
#include "HeightmapIO.hpp"

#include <string>
#include <vector>

namespace prs
{
    struct RunDescription
    {
        std::string inputPath;
        RasterInfo raster;      // kako je ulaz učitan
        TerrainScale scale;
        bool inverted = false;  // korisnik je odabrao "bijelo = najniže"
        SimulationConfig config;
        std::string programVersion;
    };

    struct ExportOptions
    {
        std::string outputDirectory; // npr. "output/mountain"
        std::string baseName;        // npr. "mountain" -> mountain_eroded.png ...
        bool writeCsv = false;       // dodatno CSV (veliko za velike mape)
    };

    struct ExportResult
    {
        std::vector<std::string> files;    // sve zapisane datoteke
        std::vector<std::string> warnings; // npr. vrijednosti izvan visinskog raspona
        std::string erodedPng;
        std::string reportTxt;
        std::string reportJson;
        QuantizationReport erodedRange;
        double differenceColorScale = 0.0; // [m] |Δh| koji odgovara punoj boji na mapi razlike
        double hillshadeZFactor = 1.0;
    };

    // Zapisuje sve izlaze. Baca std::runtime_error ako folder ne može biti napravljen ili ako bi izlaz
    // prepisao ulaznu datoteku.
    ExportResult exportResults(const ExportOptions& options, const RunDescription& run, const ErosionSimulation& sim);

    // NumPy .npy v1.0, little-endian float64, oblik (visina, širina) – za Python (numpy.load).
    void saveNpy(const std::string& path, const Grid2D<double>& grid);

    // Pomoćne vizualizacije (javne radi testova).
    std::vector<std::uint8_t> renderDifference(const Grid2D<double>& difference, double colorScale);
    std::vector<std::uint8_t> renderHillshade(const Grid2D<double>& elevation, double cellSize, double zFactor);
    std::vector<std::uint8_t> renderWaterDepth(const Grid2D<double>& depth);
}
