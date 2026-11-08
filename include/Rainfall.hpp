#pragma once
// Model padavina: korisnik zadaje UKUPNU količinu kiše (L/m²) i TRAJANJE (h); kiša je ravnomjerna u vremenu
// i prostoru (konstantan intenzitet nad cijelom mapom).
//
// Jedinice:   1 L/m² = 1 mm vodenog stuba = 0.001 m
//             intenzitet r = ukupno / trajanje            [mm/h]
//             r [m/s] = r [mm/h] * 0.001 / 3600
// U svakom koraku dodaje se SAMO dio koji pada u tom koraku:  Δd = r * (preklop [t, t+dt] s [0, T_kiše]).
// Tako je zbir po svim koracima tačno jednak ukupnoj količini, bez obzira na (adaptivni) dt.

#include <optional>
#include <string>

namespace prs
{
    struct RainfallEvent
    {
        static constexpr double kMetersPerMillimeter = 1.0e-3;
        static constexpr double kSecondsPerHour = 3600.0;

        double totalMillimeters = 0.0; // ukupne padavine [mm] = [L/m²]
        double durationHours = 1.0;    // trajanje padavina [h] (fizičko simulirano vrijeme, ne vrijeme izvršavanja)

        double durationSeconds() const { return durationHours * kSecondsPerHour; }
        double totalMeters() const { return totalMillimeters * kMetersPerMillimeter; }
        double intensityMillimetersPerHour() const { return totalMillimeters / durationHours; }
        double intensityMetersPerSecond() const { return totalMeters() / durationSeconds(); }

        // Dubina vode [m] koja padne u intervalu [t0, t1] (sekunde od početka simulacije).
        double depthAddedBetween(double t0, double t1) const;

        // Prazan string = ispravno; inače opis greške. (Negativna kiša, nepozitivno trajanje, NaN, Inf...)
        std::string validate() const;
    };

    // Provjera korisničkog unosa prije pravljenja RainfallEvent (vraća poruku greške ili nullopt).
    std::optional<std::string> validateRainfallInput(double totalMillimeters, double durationHours);
}
