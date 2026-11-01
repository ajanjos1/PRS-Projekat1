#include "simulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

// Deterministički početni teren: fBm (fractional Brownian motion) od "value noise" + centralni masiv.
// Računa se u double preciznosti i zavisi samo od normalizovanih koordinata (u, v) u [0, 1],
// pa sve rezolucije (128..1024) prikazuju isti pejzaž, a sve varijante dobijaju identične ulaze.

namespace sw
{
    namespace
    {
        std::uint32_t hash2(std::int32_t x, std::int32_t y, std::uint32_t seed)
        {
            std::uint32_t h = seed;
            h ^= static_cast<std::uint32_t>(x) * 0x8da6b343u;
            h ^= static_cast<std::uint32_t>(y) * 0xd8163841u;
            h = (h ^ (h >> 13)) * 0x5bd1e995u;
            h ^= h >> 15;
            h *= 0x27d4eb2du;
            h ^= h >> 16;
            return h;
        }

        double lattice(std::int32_t x, std::int32_t y, std::uint32_t seed)
        {
            return static_cast<double>(hash2(x, y, seed) & 0xffffffu) / static_cast<double>(0xffffffu);
        }

        double smooth(double t) { return t * t * (3.0 - 2.0 * t); }

        double value_noise(double u, double v, std::uint32_t seed)
        {
            const double fx = std::floor(u);
            const double fy = std::floor(v);
            const auto ix = static_cast<std::int32_t>(fx);
            const auto iy = static_cast<std::int32_t>(fy);
            const double tx = smooth(u - fx);
            const double ty = smooth(v - fy);
            const double a = lattice(ix, iy, seed);
            const double b = lattice(ix + 1, iy, seed);
            const double c = lattice(ix, iy + 1, seed);
            const double d = lattice(ix + 1, iy + 1, seed);
            const double top = a + (b - a) * tx;
            const double bot = c + (d - c) * tx;
            return top + (bot - top) * ty;
        }

        double fbm(double u, double v, std::uint32_t seed, int octaves)
        {
            double sum = 0, amp = 0.5, freq = 3.0, norm = 0;
            for (int o = 0; o < octaves; ++o)
            {
                sum += amp * value_noise(u * freq, v * freq, seed + 1013u * static_cast<std::uint32_t>(o));
                norm += amp;
                amp *= 0.5;
                freq *= 2.0;
            }
            return sum / norm; // [0, 1]
        }
    }

    void init_terrain(Simulation& sim, const SimParams& p, const TerrainSpec& spec)
    {
        const int W = sim.width();
        const int H = sim.height();
        const double amplitude = spec.relief * W * p.cell_size;
        float* b = sim.terrain();

        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
            {
                const double u = (x + 0.5) / W;
                const double v = (y + 0.5) / H;
                // centralni masiv koji se spušta prema rubovima + "grebeni" (1 - |2n - 1|)
                const double r = std::sqrt((u - 0.5) * (u - 0.5) + (v - 0.5) * (v - 0.5)) / 0.7071;
                const double massif = std::max(0.0, 1.0 - r);
                const double n = fbm(u, v, spec.seed, spec.octaves);
                const double ridge = 1.0 - std::fabs(2.0 * fbm(u + 7.3, v + 3.1, spec.seed ^ 0x9e3779b9u, spec.octaves) - 1.0);
                const double h = 0.45 * massif * massif + 0.35 * n + 0.20 * ridge * massif;
                b[sim.index(x, y)] = static_cast<float>(amplitude * h);
            }

        sim.clear_dynamic_state();
        sim.refresh_terrain_halo();
        // drugi bafer terena dobija iste vrijednosti (uključujući halo)
        float* bn = sim.terrain_next();
        std::copy(b, b + sim.pitch() * sim.rows(), bn);
    }
}
