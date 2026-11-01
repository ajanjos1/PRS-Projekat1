// Serijski driver: jedna dretva prolazi kroz sve redove, faza po fazu.
// Kombinacijom sa različitim skupovima kernela dobijaju se varijante A, B, C i D.

#include "simulation.hpp"

#include <utility>

namespace sw
{
    void run_serial(Simulation& sim, const StepConstants& c, int steps, const KernelSet& k)
    {
        enable_flush_denormals();
        const Fields F = sim.fields();
        const int H = sim.height();

        for (int step = 0; step < steps; ++step)
        {
            sim.refresh_terrain_halo(); // replikacija rubnih visina terena (za nagib u fazi 2)
            const float* b = sim.terrain();
            float* b_next = sim.terrain_next();

            for (int y = 0; y < H; ++y) // faza 1: protoci
                k.flux(F, c, b, y);
            for (int y = 0; y < H; ++y) // faza 2: voda, brzina, erozija/depozicija
                k.water(F, c, b, b_next, y);
            for (int y = 0; y < H; ++y) // faza 3: transport sedimenta
                k.sediment(F, c, y);

            sim.swap_terrain(); // double buffering terena
        }
    }
}
