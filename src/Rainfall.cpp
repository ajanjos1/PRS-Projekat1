#include "Rainfall.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace prs
{
    namespace
    {
        // Gornje granice služe samo da odbiju očigledno pogrešan unos (npr. 1e300), ne fizici.
        constexpr double kMaxRainMillimeters = 1.0e5; // 100 m vode
        constexpr double kMaxDurationHours = 1.0e4;   // ~1 godina
    }

    double RainfallEvent::depthAddedBetween(double t0, double t1) const
    {
        if (!(t1 > t0) || totalMillimeters <= 0.0)
            return 0.0;
        const double rainEnd = durationSeconds();
        const double overlap = std::min(t1, rainEnd) - std::max(t0, 0.0);
        if (overlap <= 0.0)
            return 0.0;
        return intensityMetersPerSecond() * overlap;
    }

    std::optional<std::string> validateRainfallInput(double totalMillimeters, double durationHours)
    {
        if (!std::isfinite(totalMillimeters))
            return "Količina kiše mora biti konačan broj.";
        if (totalMillimeters < 0.0)
            return "Količina kiše ne može biti negativna.";
        if (totalMillimeters > kMaxRainMillimeters)
            return "Količina kiše je nerealno velika (max 100000 L/m2).";
        if (!std::isfinite(durationHours))
            return "Trajanje mora biti konačan broj.";
        if (durationHours <= 0.0)
            return "Trajanje padavina mora biti pozitivno (dijeljenje nulom nije dozvoljeno).";
        if (durationHours > kMaxDurationHours)
            return "Trajanje padavina je nerealno dugo (max 10000 h).";
        return std::nullopt;
    }

    std::string RainfallEvent::validate() const
    {
        const auto err = validateRainfallInput(totalMillimeters, durationHours);
        return err ? *err : std::string{};
    }
}
