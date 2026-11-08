#include "Heightmap.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace prs
{
    std::string TerrainScale::validate() const
    {
        std::ostringstream err;
        if (!std::isfinite(cellSizeMeters) || cellSizeMeters <= 0.0)
            err << "Veličina ćelije mora biti pozitivan konačan broj (unijeto: " << cellSizeMeters << " m). ";
        if (!std::isfinite(minElevationMeters) || !std::isfinite(maxElevationMeters))
            err << "Minimalna i maksimalna visina moraju biti konačni brojevi. ";
        else if (maxElevationMeters <= minElevationMeters)
            err << "Maksimalna visina mora biti veća od minimalne (" << minElevationMeters << " >= "
                << maxElevationMeters << "). ";
        return err.str();
    }

    Heightmap makeHeightmap(const Grid2D<double>& normalized, const TerrainScale& scale, bool invert,
                            std::string sourceName)
    {
        const std::string problem = scale.validate();
        if (!problem.empty())
            throw std::invalid_argument(problem);
        if (normalized.empty())
            throw std::invalid_argument("Prazna height mapa");

        Heightmap map;
        map.scale = scale;
        map.sourceName = std::move(sourceName);
        map.elevation.resize(normalized.width(), normalized.height());
        for (std::size_t i = 0; i < normalized.size(); ++i)
        {
            double v = normalized[i];
            if (!std::isfinite(v))
                throw std::invalid_argument("Height mapa sadrzi nekonacnu vrijednost");
            if (invert)
                v = 1.0 - v;
            map.elevation[i] = scale.toMeters(v);
        }
        return map;
    }
}
