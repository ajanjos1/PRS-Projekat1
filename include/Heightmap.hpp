#pragma once
// Apstraktni model terena koji prima numerički solver.
//
// Solver NE zna ništa o slikama, putanjama ni bibliotekama za slike: dobija samo mrežu visina u metrima i
// fizičku skalu. Danas Heightmap nastaje iz PNG/JPEG datoteke (HeightmapIO), a u narednoj fazi ga može
// napraviti i generator terena – dovoljno je da popuni iste podatke (normalizovane visine + TerrainScale).

#include "Grid2D.hpp"

#include <string>

namespace prs
{
    // Fizička skala rastera. Slika sama po sebi ne zna koliko metara je jedan piksel niti koliki je visinski
    // raspon, pa korisnik to zadaje (ili prihvata podrazumijevane vrijednosti). To NISU stvarne geografske
    // dimenzije ako ih korisnik ne zna – samo pretpostavka modela.
    struct TerrainScale
    {
        static constexpr double kDefaultCellSizeMeters = 30.0;
        static constexpr double kDefaultMinElevationMeters = 0.0;
        static constexpr double kDefaultMaxElevationMeters = 100.0;

        double cellSizeMeters = kDefaultCellSizeMeters;         // horizontalna veličina ćelije l = lx = ly [m]
        double minElevationMeters = kDefaultMinElevationMeters; // visina crnog piksela (vrijednost 0) [m]
        double maxElevationMeters = kDefaultMaxElevationMeters; // visina bijelog piksela (vrijednost 1) [m]

        double elevationRange() const { return maxElevationMeters - minElevationMeters; }

        // Prazan string = ispravno; inače opis greške.
        std::string validate() const;

        // Linearno preslikavanje: h = min + v * (max - min), v u [0, 1].
        double toMeters(double normalized) const { return minElevationMeters + normalized * elevationRange(); }
        // Inverzno preslikavanje (isti raspon za ulaz i izlaz -> izlaz se NE normalizuje nezavisno).
        double toNormalized(double meters) const { return (meters - minElevationMeters) / elevationRange(); }
    };

    struct Heightmap
    {
        Grid2D<double> elevation; // visina terena b(x, y) [m]
        TerrainScale scale;
        std::string sourceName;   // npr. naziv datoteke ili "generator"

        int width() const { return elevation.width(); }
        int height() const { return elevation.height(); }
    };

    // Gradi Heightmap iz normalizovanih vrijednosti [0, 1] (0 = najniže, 1 = najviše).
    // invert = true zamjenjuje značenje (bijelo = najniže), za mape koje koriste obrnutu konvenciju.
    Heightmap makeHeightmap(const Grid2D<double>& normalized, const TerrainScale& scale, bool invert,
                            std::string sourceName);
}
