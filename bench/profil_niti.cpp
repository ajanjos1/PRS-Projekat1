// Snima koliko je svaka nit radila, a koliko čekala na barijeri – mjera balansa opterećenja.
//
// Podatak se mjeri iz samog programa: svaka nit zabilježi kada je počela i kada je završila svoj dio
// petlje (ThreadProfile.hpp), a čekanje je razlika do kraja posljednje niti. Iz odnosa rada i čekanja
// vidi se isplati li se promjena rasporeda posla (schedule).
//
// Prevođenje i pokretanje (iz korijena projekta):
//   make -C bench profil        (gradi poseban build s -DPRS_PROFIL_NITI=1)
//   ./build-profil/profil_niti [slika.png] [koraka]
// Izlaz: tabela na ekranu + bench/rezultati/profil_niti.csv
#include "ErosionSimulation.hpp"
#include "HeightmapIO.hpp"
#include "HydraulicErosion.hpp"
#include "ThreadProfile.hpp"
#include "VirtualPipesSolver.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <system_error>
#include <omp.h>
#include <string>
#include <vector>

using namespace prs;

int main(int argc, char** argv)
{
    const std::string slika = argc > 1 ? argv[1]
        : "assets/heightmaps/primjer_3/mali-otocic-height-map-16bit-texturemap-app-2026-11-08.png";
    const int koraka = argc > 2 ? std::atoi(argv[2]) : 50;

    Heightmap mapa;
    try
    {
        mapa = loadHeightmap(slika, TerrainScale{});
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "GRESKA: %s\n", e.what());
        return 1;
    }

    SimulationConfig cfg;
    cfg.rainfall.totalMillimeters = 100.0;
    cfg.rainfall.durationHours = 1.0;

    ErosionSimulation sim(mapa, cfg);
    for (int k = 0; k < 40; ++k) // zagrijavanje: realno stanje s vodom i sedimentom
        sim.step();
    SimulationState s = sim.state();

    const VirtualPipesSolver pipes(cfg.flow);
    const HydraulicErosion erosion(cfg.erosion);
    const double rainDepth = cfg.rainfall.intensityMetersPerSecond();

    std::vector<double> rad(profil::kMaxNiti, 0.0), cekanje(profil::kMaxNiti, 0.0);
    double waveSpeed = 0.0, dtPrethodni = cfg.flow.maxTimeStepSeconds;
    int niti = 0;

    for (int k = 0; k < koraka; ++k)
    {
        double dt = std::min(pipes.stableTimeStep(waveSpeed, s.cellSize), dtPrethodni * 1.25);
        dtPrethodni = dt;

        pipes.addUniformWater(s, rainDepth);
        profil::ocisti();
        pipes.computeOutflowFlux(s, dt); // jedina faza koja je instrumentirana
        const int nitiSada = profil::brojNiti.load();   // atomičan, pa se čita sa .load()
        niti = std::max(niti, nitiSada);

        double posljednjiKraj = 0.0;
        for (int t = 0; t < nitiSada; ++t)
            posljednjiKraj = std::max(posljednjiKraj, profil::zadnjaFaza[static_cast<std::size_t>(t)].kraj);
        for (int t = 0; t < nitiSada; ++t)
        {
            const auto& u = profil::zadnjaFaza[static_cast<std::size_t>(t)];
            rad[static_cast<std::size_t>(t)] += u.kraj - u.pocetak;
            cekanje[static_cast<std::size_t>(t)] += posljednjiKraj - u.kraj;
        }

        const WaterStepStats w = pipes.updateWaterAndVelocity(s, dt);
        erosion.erodeAndDeposit(s, dt);
        erosion.transportSediment(s, dt);
        waveSpeed = w.maxWaveSpeed;
    }

    double ukupnoRad = 0.0, ukupnoCekanje = 0.0;
    for (int t = 0; t < niti; ++t)
    {
        ukupnoRad += rad[static_cast<std::size_t>(t)];
        ukupnoCekanje += cekanje[static_cast<std::size_t>(t)];
    }

    std::printf("Faza protoka, %d koraka, %d niti (mreza %d x %d)\n\n", koraka, niti, s.width, s.height);
    std::printf("%-5s %12s %12s %10s\n", "nit", "rad [ms]", "cekanje [ms]", "iskoriste.");
    std::printf("%-5s %12s %12s %10s\n", "-----", "------------", "------------", "----------");
    for (int t = 0; t < niti; ++t)
    {
        const double r = rad[static_cast<std::size_t>(t)] * 1e3, c = cekanje[static_cast<std::size_t>(t)] * 1e3;
        std::printf("%-5d %12.2f %12.2f %9.1f%%\n", t, r, c, 100.0 * r / (r + c));
    }
    std::printf("\nUkupno: rad %.1f ms, cekanje %.1f ms -> iskoristenost niti %.1f %%\n",
                ukupnoRad * 1e3, ukupnoCekanje * 1e3, 100.0 * ukupnoRad / (ukupnoRad + ukupnoCekanje));

    // Trake u terminalu: zeleno = nit racuna, crveno = nit ceka na barijeri.
    // Isto sto i slika bench/rezultati/traka_niti.png, samo odmah vidljivo pri pokretanju.
    double najduze = 0.0;
    for (int t = 0; t < niti; ++t)
        najduze = std::max(najduze, rad[static_cast<std::size_t>(t)] + cekanje[static_cast<std::size_t>(t)]);
    const int sirina = 56;
    std::printf("\n  \033[42m  \033[0m rad      \033[41m  \033[0m cekanje na barijeri\n\n");
    for (int t = 0; t < niti; ++t)
    {
        const double r = rad[static_cast<std::size_t>(t)], c = cekanje[static_cast<std::size_t>(t)];
        const int nr = static_cast<int>(r / najduze * sirina + 0.5);
        const int nc = std::max(c > 0.0 ? 1 : 0, static_cast<int>(c / najduze * sirina + 0.5));
        std::printf("  nit %d  \033[42m", t);
        for (int k = 0; k < nr; ++k) std::printf(" ");
        std::printf("\033[41m");
        for (int k = 0; k < nc; ++k) std::printf(" ");
        std::printf("\033[0m  %5.1f%%\n", 100.0 * r / (r + c));
    }

    std::error_code greska;
    std::filesystem::create_directories("bench/rezultati", greska); // na cistom klonu folder ne postoji
    if (FILE* f = std::fopen("bench/rezultati/profil_niti.csv", "w"))
    {
        // Broj koraka ide u zaglavlje: grafovi.py poredi guided s zapisanim static mjerenjem,
        // pa trake smiju doci iz CSV-a samo ako je mjereno istim brojem koraka.
        std::fprintf(f, "nit,rad_ms,cekanje_ms,koraka=%d\n", koraka);
        for (int t = 0; t < niti; ++t)
            std::fprintf(f, "%d,%.4f,%.4f\n", t, rad[static_cast<std::size_t>(t)] * 1e3,
                         cekanje[static_cast<std::size_t>(t)] * 1e3);
        std::fclose(f);
        std::printf("Zapisano: bench/rezultati/profil_niti.csv\n");
    }
    return 0;
}
