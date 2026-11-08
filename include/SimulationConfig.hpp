#pragma once
// Parametri simulacije: kiša, tok vode (virtuelne cijevi) i erozija. Sve vrijednosti su u SI jedinicama,
// osim gdje je izričito navedeno (mm, mm/h, h – jedinice koje korisnik unosi).

#include "Rainfall.hpp"

#include <string>

namespace prs
{
    enum class BoundaryCondition
    {
        Closed, // zid: kroz rub mreže nema protoka (zatvoren sistem, bilans vode je tačan bez izlaza)
        Open    // slobodan izliv: voda i sediment mogu napustiti mrežu; izlazna zapremina se evidentira
    };

    const char* toString(BoundaryCondition b);

    struct WaterFlowParameters
    {
        double gravity = 9.81; // g [m/s²]

        // Manningov koeficijent hrapavosti n [s/m^(1/3)] (trenje o podlogu, Bates i dr. 2010).
        // 0.03–0.05 je tipično za travu/obradivo tlo. n = 0 isključuje trenje (izvorni Mei model bez trenja).
        double manningN = 0.04;

        // Adaptivni korak (CFL uslov eksplicitne šeme): dt = α · l / max(2·√(g d) + max(|u|, |v|)).
        // 2·√(g d): granica stabilnosti Mei modela s dvije izlazne cijevi po paru ćelija (vidi
        // VirtualPipesSolver.cpp); |u|: voda ne smije u jednom koraku preći više od dijela ćelije, inače
        // rezultat zavisi od Δt. Testovi: stabilnost duboke bare i konvergencija (α = 0.7 naspram 0.35).
        double courantNumber = 0.7;
        double maxTimeStepSeconds = 10.0; // gornja granica koraka [s] (i kad je teren suh)
        double minTimeStepSeconds = 1.0e-4; // ako stabilan korak padne ispod ovoga -> simulacija se prekida

        double velocityMinDepth = 1.0e-4; // [m] ispod ove srednje dubine brzina se ne računa (u = v = 0)
        double pipeMinDepth = 1.0e-7;     // [m] cijev s manjom dubinom toka se smatra suhom (protok 0)

        BoundaryCondition boundary = BoundaryCondition::Closed;

        // Gubici vode (podrazumijevano isključeni; tada je bilans vode zatvoren):
        double infiltrationMillimetersPerHour = 0.0; // konstantna brzina upijanja u tlo [mm/h]
        double evaporationMillimetersPerHour = 0.0;  // konstantna brzina isparavanja [mm/h]
    };

    struct ErosionParameters
    {
        bool enabled = true;

        // Kapacitet transporta (Mei 2007: C = Kc * sin(alpha) * |v|), ovdje kao zapreminska koncentracija
        // pomnožena dubinom vode:  C = min(Kc * max(sin(alpha), minSlope) * |v|, maxConcentration) * d   [m]
        double capacityCoefficient = 0.5;  // Kc [s/m]
        double minimumSlope = 0.0;         // donja granica sin(alpha) (0 = na ravnom nema erozije)
        double maxConcentration = 0.1;     // gornja granica zapreminske koncentracije sedimenta [-]

        double dissolvingRate = 0.02;  // Ks [1/s] – brzina podizanja materijala (erozija)
        double depositionRate = 0.02;  // Kd [1/s] – brzina taloženja (depozicija)

        // Debljina erodibilnog sloja iznad "stijene" [m]: teren ne može pasti ispod (početna visina - debljina).
        // <= 0 znači bez ograničenja.
        double erodibleDepthMeters = 5.0;

        // Na kraju simulacije sediment koji je još u vodi se taloži na mjestu gdje se nalazi
        // (inače bi taj materijal "nestao" iz izlazne height mape).
        bool settleSuspendedSedimentAtEnd = true;
    };

    struct SimulationConfig
    {
        RainfallEvent rainfall;
        double runoffHours = 0.0; // period oticanja NAKON kiše (bez padavina) [h] – zaseban parametar
        WaterFlowParameters flow;
        ErosionParameters erosion;

        double rainSeconds() const { return rainfall.durationSeconds(); }
        double runoffSeconds() const { return runoffHours * RainfallEvent::kSecondsPerHour; }
        double totalSeconds() const { return rainSeconds() + runoffSeconds(); }

        // Prazan string = ispravno; inače opis svih grešaka.
        std::string validate() const;
    };
}
