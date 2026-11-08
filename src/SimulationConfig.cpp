#include "SimulationConfig.hpp"

#include <cmath>
#include <sstream>

namespace prs
{
    const char* toString(BoundaryCondition b)
    {
        return b == BoundaryCondition::Closed ? "closed" : "open";
    }

    namespace
    {
        bool finite(double v) { return std::isfinite(v); }
        bool positive(double v) { return std::isfinite(v) && v > 0.0; }
        bool nonNegative(double v) { return std::isfinite(v) && v >= 0.0; }
    }

    std::string SimulationConfig::validate() const
    {
        std::ostringstream err;
        const std::string rain = rainfall.validate();
        if (!rain.empty())
            err << rain << ' ';
        if (!nonNegative(runoffHours) || runoffHours > 1.0e4)
            err << "Period oticanja mora biti broj u intervalu [0, 10000] h. ";

        if (!positive(flow.gravity))
            err << "g mora biti pozitivno. ";
        if (!nonNegative(flow.manningN) || flow.manningN > 1.0)
            err << "Manningov koeficijent mora biti u intervalu [0, 1]. ";
        if (!positive(flow.courantNumber) || flow.courantNumber > 1.0)
            err << "Courantov broj mora biti u intervalu (0, 1]. ";
        if (!positive(flow.maxTimeStepSeconds))
            err << "Maksimalni korak mora biti pozitivan. ";
        if (!positive(flow.minTimeStepSeconds) || flow.minTimeStepSeconds > flow.maxTimeStepSeconds)
            err << "Minimalni korak mora biti pozitivan i manji od maksimalnog. ";
        if (!positive(flow.velocityMinDepth) || !positive(flow.pipeMinDepth))
            err << "Pragovi dubine moraju biti pozitivni. ";
        if (!nonNegative(flow.infiltrationMillimetersPerHour) || !nonNegative(flow.evaporationMillimetersPerHour))
            err << "Infiltracija i isparavanje ne mogu biti negativni. ";

        if (!nonNegative(erosion.capacityCoefficient))
            err << "Kc ne može biti negativan. ";
        if (!nonNegative(erosion.minimumSlope) || erosion.minimumSlope > 1.0)
            err << "Minimalni nagib (sin) mora biti u [0, 1]. ";
        if (!positive(erosion.maxConcentration) || erosion.maxConcentration > 1.0)
            err << "Maksimalna koncentracija mora biti u (0, 1]. ";
        if (!nonNegative(erosion.dissolvingRate) || !nonNegative(erosion.depositionRate))
            err << "Ks i Kd ne mogu biti negativni. ";
        if (!finite(erosion.erodibleDepthMeters))
            err << "Debljina erodibilnog sloja mora biti konačan broj. ";
        return err.str();
    }
}
