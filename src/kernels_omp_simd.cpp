// C) Vektorizacija pomoću OpenMP SIMD direktiva (i osnova za F - OpenMP dretve + SIMD).
// "#pragma omp simd" je obećanje programera da su iteracije unutrašnje petlje nezavisne:
// kompajler tada vektorizuje petlju bez analize preklapanja pokazivača (zato ovdje NEMA __restrict)
// i bez cost-modela koji bi mogao odbiti vektorizaciju.
//
// Zašto je to ovdje sigurno: u svakoj fazi iteracija x piše ISKLJUČIVO elemente sa indeksom x
// (fL/fR/fT/fB u fazi 1; d, s, sr i b_dst u fazi 2; s u fazi 3), a čita samo nizove koje ta faza ne
// mijenja ili vlastiti element prije upisa. Nema prenesenih zavisnosti (loop-carried dependencies)
// niti redukcija. Napomena: ova direktiva NE pokreće više dretvi - to radi tek "omp parallel for".
//
// Važno zapažanje (GCC 13): "omp simd" nije bio dovoljan - uz podrazumijevani -ftrapping-math
// kompajler je za fazu 1 i 2 prijavio "not vectorized: control flow in loop" (množenje rezultatom
// min/max izraza može izazvati FP izuzetak, pa se ne smije spekulativno izvršiti). Fajl se zato
// prevodi sa -fno-trapping-math (isto kao B), što je potvrđeno izvještajem i bit-identičnim testovima.

#include "simulation.hpp"

#include <algorithm>
#include <cmath>

namespace sw
{
    namespace
    {
        void flux_row(const Fields& F, const StepConstants& c, const float* b_base, int y)
        {
            const std::size_t P = F.pitch;
            const std::size_t row = F.row_offset(y);
            const int W = F.width;

            const float* b = b_base + row;
            const float* b_up = b - P;
            const float* b_dn = b + P;
            const float* d = F.d + row;
            const float* d_up = d - P;
            const float* d_dn = d + P;
            float* fl = F.fL + row;
            float* fr = F.fR + row;
            float* ft = F.fT + row;
            float* fb = F.fB + row;
            const float* open_l = F.openL;
            const float* open_r = F.openR;

            const float rain = c.rain_dt, kf = c.flux_coef, dt = c.dt, area = c.cell_area, tiny = c.tiny;
            const float open_t = (y > 0) ? 1.0f : 0.0f;
            const float open_b = (y < F.height - 1) ? 1.0f : 0.0f;

            SW_OMP_SIMD
            for (int x = 0; x < W; ++x)
            {
                const float d1 = d[x] + rain;
                const float h = b[x] + d1;
                const float hl = b[x - 1] + (d[x - 1] + rain);
                const float hr = b[x + 1] + (d[x + 1] + rain);
                const float ht = b_up[x] + (d_up[x] + rain);
                const float hb = b_dn[x] + (d_dn[x] + rain);

                const float nl = std::max(fl[x] + kf * (h - hl), 0.0f) * open_l[x];
                const float nr = std::max(fr[x] + kf * (h - hr), 0.0f) * open_r[x];
                const float nt = std::max(ft[x] + kf * (h - ht), 0.0f) * open_t;
                const float nb = std::max(fb[x] + kf * (h - hb), 0.0f) * open_b;

                const float out = (((nl + nr) + nt) + nb) * dt;
                const float vol = d1 * area;
                const float k = std::min(vol / std::max(out, tiny), 1.0f);

                fl[x] = nl * k;
                fr[x] = nr * k;
                ft[x] = nt * k;
                fb[x] = nb * k;
            }
        }

        void water_row(const Fields& F, const StepConstants& c, const float* bs_base, float* bd_base, int y)
        {
            const std::size_t P = F.pitch;
            const std::size_t row = F.row_offset(y);
            const int W = F.width;

            const float* bs = bs_base + row;
            const float* bs_up = bs - P;
            const float* bs_dn = bs + P;
            float* bd = bd_base + row;
            float* d = F.d + row;
            float* s = F.s + row;
            float* sr = F.sr + row;
            const float* fl = F.fL + row;
            const float* fr = F.fR + row;
            const float* ft = F.fT + row;
            const float* fb = F.fB + row;
            const float* fb_up = F.fB + row - P;
            const float* ft_dn = F.fT + row + P;

            const float rain = c.rain_dt, dt_area = c.dt_over_area, half = c.half, L = c.cell_size;
            const float min_depth = c.min_depth, inv2l = c.inv_2L, min_tilt = c.min_tilt;
            const float inv_ero = c.inv_erosion_depth, kc = c.kc, dtks = c.dt_ks, dtkd = c.dt_kd;
            const float dt = c.dt, area = c.cell_area, seps = c.sediment_eps, evap = c.evap_factor;

            SW_OMP_SIMD
            for (int x = 0; x < W; ++x)
            {
                const float d1 = d[x] + rain;
                const float in_l = fr[x - 1];
                const float in_r = fl[x + 1];
                const float in_t = fb_up[x];
                const float in_b = ft_dn[x];
                const float fin = ((in_l + in_r) + in_t) + in_b;
                const float fout = ((fl[x] + fr[x]) + ft[x]) + fb[x];

                const float d2 = std::max(d1 + (fin - fout) * dt_area, 0.0f);
                const float dmean = half * (d1 + d2);
                const float wx = half * ((in_l - fl[x]) + (fr[x] - in_r));
                const float wy = half * ((in_t - ft[x]) + (fb[x] - in_b));

                const float inv_raw = 1.0f / (L * std::max(dmean, min_depth));
                const float inv = (dmean > min_depth) ? inv_raw : 0.0f;
                const float u = wx * inv;
                const float v = wy * inv;
                const float speed = std::sqrt(u * u + v * v);

                const float gx = (bs[x + 1] - bs[x - 1]) * inv2l;
                const float gy = (bs_dn[x] - bs_up[x]) * inv2l;
                const float g2 = gx * gx + gy * gy;
                const float sin_a = std::max(std::sqrt(g2 / (1.0f + g2)), min_tilt);
                const float depth_factor = std::min(dmean * inv_ero, 1.0f);
                const float capacity = ((kc * sin_a) * speed) * depth_factor;

                const float s0 = s[x];
                const float diff = capacity - s0;
                const float amount = ((diff > 0.0f) ? dtks : dtkd) * diff;

                bd[x] = bs[x] - amount;
                const float s1 = s0 + amount;
                s[x] = s1;
                const float ratio_raw = (s1 * dt) / (std::max(d1, seps) * area);
                sr[x] = (d1 > seps) ? ratio_raw : 0.0f;
                d[x] = d2 * evap;
            }
        }

        void sediment_row(const Fields& F, const StepConstants&, int y)
        {
            const std::size_t P = F.pitch;
            const std::size_t row = F.row_offset(y);
            const int W = F.width;

            float* s = F.s + row;
            const float* sr = F.sr + row;
            const float* sr_up = sr - P;
            const float* sr_dn = sr + P;
            const float* fl = F.fL + row;
            const float* fr = F.fR + row;
            const float* ft = F.fT + row;
            const float* fb = F.fB + row;
            const float* fb_up = fb - P;
            const float* ft_dn = ft + P;

            SW_OMP_SIMD
            for (int x = 0; x < W; ++x)
            {
                const float fout = ((fl[x] + fr[x]) + ft[x]) + fb[x];
                const float a = sr[x - 1] * fr[x - 1];
                const float b = sr[x + 1] * fl[x + 1];
                const float t = sr_up[x] * fb_up[x];
                const float u = sr_dn[x] * ft_dn[x];
                const float sin = ((a + b) + t) + u;
                s[x] = std::max((s[x] - sr[x] * fout) + sin, 0.0f);
            }
        }
    }

    const KernelSet& kernels_omp_simd()
    {
#if defined(PRS_VECTOR_REQUIRES_AVX2)
        static const KernelSet k{"omp_simd", flux_row, water_row, sediment_row, true};
#else
        static const KernelSet k{"omp_simd", flux_row, water_row, sediment_row, false};
#endif
        return k;
    }
}
