// Mjerenje vremena po FAZAMA jednog koraka simulacije (infrastruktura za optimizaciju, ne dira jezgro).
//
// Zašto postoji: profiler (sample, llvm-mca, Instruments) kaže gdje se troši vrijeme, ali za poređenje
// varijanti koda treba ponovljivo mjerenje u nanosekundama po ćeliji. Ovaj program zove iste javne
// funkcije koje zove i ErosionSimulation::step(), istim redoslijedom i s istim adaptivnim Δt, pa su
// brojevi uporedivi sa stvarnom simulacijom.
//
// Prevođenje (iz korijena projekta):
//   make -C bench
// Pokretanje:
//   ./build-prof/bench_faze                          (primjer_3, 50 koraka, 3 ponavljanja)
//   ./build-prof/bench_faze <slika.png> <koraka> <ponavljanja>
//
// Ispis: za svaku fazu ukupno vrijeme, ns po ćeliji po koraku i udio u koraku.
#include "ErosionSimulation.hpp"
#include "HeightmapIO.hpp"
#include "HydraulicErosion.hpp"
#include "VirtualPipesSolver.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace prs;
using Sat = std::chrono::steady_clock;
static double sekunde(Sat::time_point a, Sat::time_point b)
{
    return std::chrono::duration<double>(b - a).count();
}

namespace
{
    constexpr int kFaza = 5;
    const char* kImena[kFaza] = {"1. kisa", "2-4. protoci", "5-6. voda i brzina", "7-8,10. erozija", "9. transport"};

    // Jedan prolaz: isti redoslijed i isti izbor Δt kao u ErosionSimulation::step().
    // Vremena faza se sabiraju u `zbir`.
    void prolaz(SimulationState& s, const VirtualPipesSolver& pipes, const HydraulicErosion& erosion,
                const SimulationConfig& cfg, int koraka, double rainDepth, double zbir[kFaza])
    {
        double waveSpeed = 0.0, dtPrethodni = cfg.flow.maxTimeStepSeconds;
        for (int k = 0; k < koraka; ++k)
        {
            double dt = pipes.stableTimeStep(waveSpeed, s.cellSize);
            dt = std::min(dt, dtPrethodni * 1.25);
            dtPrethodni = dt;

            const auto t0 = Sat::now();
            pipes.addUniformWater(s, rainDepth);
            const auto t1 = Sat::now();
            pipes.computeOutflowFlux(s, dt);
            const auto t2 = Sat::now();
            const WaterStepStats w = pipes.updateWaterAndVelocity(s, dt);
            const auto t3 = Sat::now();
            erosion.erodeAndDeposit(s, dt);
            const auto t4 = Sat::now();
            erosion.transportSediment(s, dt);
            const auto t5 = Sat::now();

            waveSpeed = w.maxWaveSpeed;
            zbir[0] += sekunde(t0, t1);
            zbir[1] += sekunde(t1, t2);
            zbir[2] += sekunde(t2, t3);
            zbir[3] += sekunde(t3, t4);
            zbir[4] += sekunde(t4, t5);
        }
    }
}

int main(int argc, char** argv)
{
    const std::string slika = argc > 1 ? argv[1]
        : "assets/heightmaps/primjer_3/mali-otocic-height-map-16bit-texturemap-app-2026-11-08.png";
    const int koraka = argc > 2 ? std::atoi(argv[2]) : 50;
    const int ponavljanja = argc > 3 ? std::atoi(argv[3]) : 3;

    TerrainScale skala; // zadano: celija 30 m, 0..100 m (mjerenje ne zavisi od stvarne geografije)
    Heightmap mapa;
    try
    {
        mapa = loadHeightmap(slika, skala);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "GRESKA: %s\n", e.what());
        return 1;
    }

    SimulationConfig cfg;
    cfg.rainfall.totalMillimeters = 100.0;
    cfg.rainfall.durationHours = 1.0;

    // Zagrijavanje: pravim stvarno stanje (voda, brzine, sediment) kroz pravu simulaciju, pa ga kopiram.
    // Bez toga bi teren bio suh, grane bi se uvijek jednako ponasale i mjerenje ne bi bilo realno.
    ErosionSimulation sim(mapa, cfg);
    for (int k = 0; k < 40; ++k)
        sim.step();
    const SimulationState pocetno = sim.state();

    const VirtualPipesSolver pipes(cfg.flow);
    const HydraulicErosion erosion(cfg.erosion);
    const double rainDepth = cfg.rainfall.intensityMetersPerSecond() * 1.0; // kisa za ~1 s koraka

    const std::size_t celija = pocetno.cellCount();
    std::printf("Mreza: %d x %d = %zu celija | koraka po ponavljanju: %d | ponavljanja: %d\n",
                pocetno.width, pocetno.height, celija, koraka, ponavljanja);
    std::printf("Ulaz: %s\n\n", slika.c_str());

    // Uzima se NAJBOLJE PONAVLJANJE U CJELINI, a ne najbolje vrijeme svake faze posebno. Minimum po fazama
    // nezavisno dao bi zbir manji od ijednog stvarno izmjerenog koraka, dakle broj koji se nikad nije desio.
    double najbolje[kFaza] = {0, 0, 0, 0, 0};
    double ukupno = 0.0;
    for (int r = 0; r < ponavljanja; ++r)
    {
        SimulationState s = pocetno; // svako ponavljanje krece iz istog stanja
        double zbir[kFaza] = {0, 0, 0, 0, 0};
        prolaz(s, pipes, erosion, cfg, koraka, rainDepth, zbir);

        double zbirUkupno = 0.0;
        for (double v : zbir)
            zbirUkupno += v;
        if (r == 0 || zbirUkupno < ukupno)
        {
            ukupno = zbirUkupno;
            for (int i = 0; i < kFaza; ++i)
                najbolje[i] = zbir[i];
        }
    }

    std::printf("%-22s %12s %16s %8s\n", "faza", "ukupno [ms]", "ns/celiji/koraku", "udio");
    std::printf("%-22s %12s %16s %8s\n", "----------------------", "------------", "----------------", "--------");
    for (int i = 0; i < kFaza; ++i)
        std::printf("%-22s %12.2f %16.2f %7.1f%%\n", kImena[i], najbolje[i] * 1e3,
                    najbolje[i] * 1e9 / (double(celija) * koraka), 100.0 * najbolje[i] / ukupno);
    std::printf("%-22s %12.2f %16.2f %7.1f%%\n", "UKUPNO korak", ukupno * 1e3,
                ukupno * 1e9 / (double(celija) * koraka), 100.0);
    return 0;
}
