#pragma once
// Hidraulička erozija, depozicija i transport sedimenta (Mei i dr. 2007, Šťava i dr. 2008), prilagođeno
// fizičkom vremenu i očuvanju materijala.
//
// 1) Kapacitet transporta (koliko materijala voda u ćeliji može nositi):
//        c_cap = min(Kc · max(sin α, sin_min) · |v|, c_max)        zapreminska koncentracija [-]
//        C     = c_cap · d                                          [m]  (debljina sloja materijala)
//    Mei: C = Kc · sin α · |v|. Proizvod |v| · sin α je "unit stream power" (Yang 1973), od kojeg zavisi
//    koncentracija koju tok može nositi; množenje dubinom d pretvara koncentraciju u količinu materijala,
//    pa tanak film vode nosi malo, a dublji brz tok više. α je nagib VODENE POVRŠINE η = b + d: na padini
//    s tankim slojem vode jednak je nagibu terena (kao kod Mei), a u jezeru je ≈ 0, pa mirna duboka voda
//    ne kopa dno (sa nagibom terena bi duboka jama s vodom imala ogroman, nefizikalan kapacitet).
//
// 2) Erozija / depozicija – relaksacija prema kapacitetu brzinom Ks odnosno Kd [1/s]:
//        C > s:  Δ = (1 − e^(−Ks·Δt)) · (C − s),  b −= Δ, s += Δ   (Δ ograničeno erodibilnim slojem)
//        C < s:  Δ = (1 − e^(−Kd·Δt)) · (s − C),  b += Δ, s −= Δ
//    Faktor 1 − e^(−KΔt) je tačno rješenje ds/dt = K (C − s) za konstantno C u koraku: nikad ne "preskoči"
//    kapacitet, ma koliko Δt bio velik (Mei koristi Ks·(C − s) po koraku bez Δt).
//
// 3) Transport: sediment putuje KROZ ISTE virtuelne cijevi kao voda (konzervativna upwind šema konačnih
//    zapremina). Ćelija kroz cijev šalje udio sedimenta jednak udjelu vode koji je kroz tu cijev otišao:
//        s_kroz_cijev = s · f · Δt / (d1 · l²)
//    Šta jedna ćelija pošalje, susjed tačno primi -> Σ s je očuvana; zbog K faktora udio ≤ 1 -> s ≥ 0.
//    (Mei/Šťava koriste semi-Lagrangeovu advekciju, koja NIJE konzervativna.)

#include "SimulationConfig.hpp"
#include "SimulationState.hpp"

namespace prs
{
    struct ErosionStepStats
    {
        double erodedVolume = 0.0;    // [m³] materijal podignut s terena u ovom koraku
        double depositedVolume = 0.0; // [m³] materijal istaložen na teren u ovom koraku
    };

    struct TransportStepStats
    {
        double boundaryOutflowVolume = 0.0;   // [m³] sediment koji je izašao kroz otvorene rubove
        double negativeCorrection = 0.0;      // [m³] korekcija zaokruživanja (s < 0 -> 0); očekivano ~0
    };

    class HydraulicErosion
    {
    public:
        explicit HydraulicErosion(const ErosionParameters& parameters);

        const ErosionParameters& parameters() const { return _p; }

        // sin α iz gradijenta (gx = ∂η/∂x, gy = ∂η/∂y):  sin α = |∇η| / √(1 + |∇η|²)
        static double sinSlope(double gradientX, double gradientY);

        // Kapacitet C [m] za dati nagib, brzinu [m/s] i dubinu vode [m].
        static double transportCapacity(const ErosionParameters& p, double sinAlpha, double speed, double depth);

        // Koraci 7, 8 i 10: kapacitet, erozija/depozicija, nova visina terena.
        // Čita b (i kod susjeda, za nagib) iz terrain, piše terrainNext, pa zamijeni bafere.
        ErosionStepStats erodeAndDeposit(SimulationState& s, double dt) const;

        // Korak 9: transport sedimenta kroz virtuelne cijevi (čita s susjeda iz sediment, piše sedimentNext).
        TransportStepStats transportSediment(SimulationState& s, double dt) const;

        // Taloži sav preostali suspendovani sediment na mjestu gdje se nalazi (b += s, s = 0). Vraća [m³].
        double settleSuspendedSediment(SimulationState& s) const;

    private:
        ErosionParameters _p;
    };
}
