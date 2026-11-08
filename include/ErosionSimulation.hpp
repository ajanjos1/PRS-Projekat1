#pragma once
// Vremenska simulacija: kiša → tok vode (virtuelne cijevi) → erozija/depozicija → transport sedimenta.
//
// Ova klasa zna samo za apstraktni teren (Heightmap) i parametre (SimulationConfig). Ne zavisi od konzole
// ni od biblioteke za slike; napredak javlja preko povratne funkcije (callback).
//
// Jedan vremenski korak (redoslijed prati Mei i dr. 2007):
//   1. kiša:            d1 = d + r · |[t, t+Δt] ∩ [0, T_kiše]|
//   2.–4. protoci:      f kroz virtuelne cijevi iz razlike η = b + d1, ograničeno dostupnom vodom (K)
//   5.–6. voda:         d2 = d1 + Δt (Σ f_ulaz − Σ f_izlaz) / l²,  brzine u, v
//   7., 8., 10. tlo:    kapacitet C, erozija/depozicija, nova visina terena (double buffer)
//   9. sediment:        transport kroz iste cijevi (double buffer)
//   (gubici):           infiltracija/isparavanje, ako su uključeni
// Δt je adaptivan (CFL uslov) i nikad ne prelazi kraj kiše ni kraj simulacije.

#include "Heightmap.hpp"
#include "HydraulicErosion.hpp"
#include "SimulationConfig.hpp"
#include "SimulationState.hpp"
#include "VirtualPipesSolver.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace prs
{
    struct SimulationProgress
    {
        std::int64_t step = 0;
        double simulatedSeconds = 0.0;
        double totalSeconds = 0.0;
        double lastTimeStep = 0.0;
        double wallSeconds = 0.0;
        bool raining = false;
        // true = prag od 1/reportCount ukupnog vremena (trajni red u ispisu); false = povremeni "otkucaj"
        // (heartbeat) koji samo potvrđuje da program radi, bez obzira na to koliko traje jedan prag.
        bool milestone = true;
        double fraction() const { return totalSeconds > 0.0 ? simulatedSeconds / totalSeconds : 1.0; }
    };

    using ProgressCallback = std::function<void(const SimulationProgress&)>;

    struct SimulationStatistics
    {
        // ---- vrijeme
        std::int64_t steps = 0;
        double simulatedSeconds = 0.0;
        double minTimeStep = 0.0, maxTimeStep = 0.0, meanTimeStep = 0.0; // [s]
        double wallSeconds = 0.0;                                         // vrijeme izvršavanja [s]

        // ---- voda [m³]
        double initialWaterVolume = 0.0;
        double rainVolume = 0.0;
        double boundaryWaterOutflow = 0.0;
        double infiltratedVolume = 0.0;
        double evaporatedVolume = 0.0;
        double negativeDepthCorrection = 0.0;
        double finalWaterVolume = 0.0;
        // greška = konačno − (početno + kiša − izlaz − gubici)
        double waterBalanceError() const;
        double waterBalanceRelativeError() const; // u odnosu na ukupnu ulaznu vodu

        // ---- materijal (teren + sediment) [m³]
        double grossErodedVolume = 0.0;     // zbir svih erozija po koracima
        double grossDepositedVolume = 0.0;  // zbir svih depozicija po koracima (bez taloženja na kraju)
        double settledAtEndVolume = 0.0;    // preostali suspendovani sediment istaložen na kraju
        double suspendedAtEndVolume = 0.0;  // suspendovani sediment na kraju (prije taloženja)
        double sedimentBoundaryOutflow = 0.0;
        double sedimentNegativeCorrection = 0.0;
        double materialBalanceError = 0.0;  // Σ (b − b0)·l² + Σ s·l² + izlaz sedimenta (treba biti ≈ 0)
        double materialBalanceRelativeError() const; // u odnosu na bruto eroziju

        // ---- promjena terena Δh = h_kraj − h_početak
        double netLoweredVolume = 0.0; // Σ max(0, −Δh)·l²  [m³]
        double netRaisedVolume = 0.0;  // Σ max(0, Δh)·l²   [m³]
        double maxLowering = 0.0;      // najveće sniženje [m] (pozitivan broj)
        double maxRaise = 0.0;         // najveće povišenje [m]
        double meanAbsChange = 0.0;    // srednja |Δh| [m]
        std::int64_t cellsChangedMoreThan1mm = 0;

        // ---- voda tokom simulacije
        double peakWaterDepth = 0.0; // [m]
        double peakSpeed = 0.0;      // [m/s]

        // ---- valjanost
        bool allFinite = true;
        double minWaterDepth = 0.0;
        double minSediment = 0.0;
        bool aborted = false;
        std::string abortReason;
    };

    class ErosionSimulation
    {
    public:
        ErosionSimulation(const Heightmap& terrain, const SimulationConfig& config);

        // Cijela simulacija (kiša + oticanje). reportCount = koliko puta javiti napredak (npr. 10 = svakih 10 %).
        // heartbeatSeconds > 0: ako od posljednjeg javljanja prođe toliko sekundi stvarnog vremena, napredak se
        // javlja i prije sljedećeg praga (milestone = false); 0 isključuje otkucaje.
        const SimulationStatistics& run(const ProgressCallback& progress = {}, int reportCount = 10,
                                        double heartbeatSeconds = 0.0);

        // Jedan adaptivni korak (nikad preko kraja kiše ni kraja simulacije). Vraća korišteni Δt (0 = gotovo).
        double step();
        bool finished() const;
        // Završne statistike i (opciono) taloženje preostalog sedimenta. Poziva ga run(); ručno samo u testovima.
        void finalize();

        double time() const { return _time; }
        const SimulationConfig& config() const { return _config; }
        const SimulationState& state() const { return _state; }
        const Grid2D<double>& initialTerrain() const { return _initialTerrain; }
        const SimulationStatistics& statistics() const { return _stats; }

        // Pomoćne sume (za testove i izvještaj)
        double totalWaterVolume() const;
        double totalSuspendedSedimentVolume() const;
        // Σ (b − b0)·l² + Σ s·l² + izlaz sedimenta kroz rubove; 0 u egzaktnoj aritmetici.
        double materialImbalance() const;

    private:
        void updateFinalStatistics();

        SimulationConfig _config;
        VirtualPipesSolver _pipes;
        HydraulicErosion _erosion;
        SimulationState _state;
        Grid2D<double> _initialTerrain;
        SimulationStatistics _stats;

        double _time = 0.0;
        double _lastWaveSpeed = 0.0;
        double _lastTimeStep = 0.0;
        double _sumTimeSteps = 0.0;
        bool _waterPresent = false; // ima li igdje vode nakon prethodnog koraka
        bool _finalized = false;
    };
}
