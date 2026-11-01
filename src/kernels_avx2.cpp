// D) Eksplicitna vektorizacija AVX2 intrinsics funkcijama (8 x float u __m256).
//  - centralni element i susjedi gore/dolje: poravnati load (_mm256_load_ps), jer red počinje na
//    32-bajtnoj granici, a pitch je višekratnik od 8,
//  - susjedi lijevo/desno (x - 1, x + 1): neporavnati load (_mm256_loadu_ps),
//  - uslovi -> SIMD maske (_mm256_cmp_ps) i _mm256_blendv_ps / _mm256_and_ps / min / max,
//  - ostatak reda (W % 8 != 0) obrađuje se skalarnom referencom iz cell_ops.hpp,
//  - fajl se prevodi sa -mavx2; poziva se samo ako avx2_supported() vrati true (runtime provjera),
//    a ako AVX2 build nije dostupan (npr. ARM), koristi se automatski vektorizovana verzija.

#include "cell_ops.hpp"
#include "simulation.hpp"

#if defined(PRS_HAS_AVX2_BUILD)
#include <immintrin.h>
#endif

namespace sw
{
#if defined(PRS_HAS_AVX2_BUILD)
    namespace
    {
        void flux_row(const Fields& F, const StepConstants& c, const float* b_base, int y)
        {
            const std::size_t P = F.pitch;
            const std::size_t row = F.row_offset(y);
            const int W = F.width;
            const float* b = b_base + row;
            const float* d = F.d + row;
            float* fl = F.fL + row;
            float* fr = F.fR + row;
            float* ft = F.fT + row;
            float* fb = F.fB + row;

            const float open_t = (y > 0) ? 1.0f : 0.0f;
            const float open_b = (y < F.height - 1) ? 1.0f : 0.0f;

            const __m256 zero = _mm256_setzero_ps();
            const __m256 one = _mm256_set1_ps(1.0f);
            const __m256 rain = _mm256_set1_ps(c.rain_dt);
            const __m256 kf = _mm256_set1_ps(c.flux_coef);
            const __m256 dt = _mm256_set1_ps(c.dt);
            const __m256 area = _mm256_set1_ps(c.cell_area);
            const __m256 tiny = _mm256_set1_ps(c.tiny);
            const __m256 ot = _mm256_set1_ps(open_t);
            const __m256 ob = _mm256_set1_ps(open_b);

            int x = 0;
            for (; x + kSimdFloats <= W; x += kSimdFloats)
            {
                const __m256 d1 = _mm256_add_ps(_mm256_load_ps(d + x), rain);
                const __m256 h = _mm256_add_ps(_mm256_load_ps(b + x), d1);
                const __m256 hl = _mm256_add_ps(_mm256_loadu_ps(b + x - 1), _mm256_add_ps(_mm256_loadu_ps(d + x - 1), rain));
                const __m256 hr = _mm256_add_ps(_mm256_loadu_ps(b + x + 1), _mm256_add_ps(_mm256_loadu_ps(d + x + 1), rain));
                const __m256 ht = _mm256_add_ps(_mm256_load_ps(b + x - P), _mm256_add_ps(_mm256_load_ps(d + x - P), rain));
                const __m256 hb = _mm256_add_ps(_mm256_load_ps(b + x + P), _mm256_add_ps(_mm256_load_ps(d + x + P), rain));

                // max(f + kf * (h - h_n), 0): _mm256_max_ps(0, a) vraća a kad 0 > a nije tačno -> isto kao std::max(a, 0)
                const __m256 nl = _mm256_mul_ps(_mm256_max_ps(zero, _mm256_add_ps(_mm256_load_ps(fl + x), _mm256_mul_ps(kf, _mm256_sub_ps(h, hl)))), _mm256_load_ps(F.openL + x));
                const __m256 nr = _mm256_mul_ps(_mm256_max_ps(zero, _mm256_add_ps(_mm256_load_ps(fr + x), _mm256_mul_ps(kf, _mm256_sub_ps(h, hr)))), _mm256_load_ps(F.openR + x));
                const __m256 nt = _mm256_mul_ps(_mm256_max_ps(zero, _mm256_add_ps(_mm256_load_ps(ft + x), _mm256_mul_ps(kf, _mm256_sub_ps(h, ht)))), ot);
                const __m256 nb = _mm256_mul_ps(_mm256_max_ps(zero, _mm256_add_ps(_mm256_load_ps(fb + x), _mm256_mul_ps(kf, _mm256_sub_ps(h, hb)))), ob);

                const __m256 out = _mm256_mul_ps(_mm256_add_ps(_mm256_add_ps(_mm256_add_ps(nl, nr), nt), nb), dt);
                const __m256 vol = _mm256_mul_ps(d1, area);
                // K = min(vol / max(out, tiny), 1); min/max operandi poredani kao std::min/std::max
                const __m256 k = _mm256_min_ps(one, _mm256_div_ps(vol, _mm256_max_ps(tiny, out)));

                _mm256_store_ps(fl + x, _mm256_mul_ps(nl, k));
                _mm256_store_ps(fr + x, _mm256_mul_ps(nr, k));
                _mm256_store_ps(ft + x, _mm256_mul_ps(nt, k));
                _mm256_store_ps(fb + x, _mm256_mul_ps(nb, k));
            }
            for (; x < W; ++x) // ostatak reda: skalarna referenca
                ref::flux_cell(F, c, b_base, row + static_cast<std::size_t>(x), x, open_t, open_b);
        }

        void water_row(const Fields& F, const StepConstants& c, const float* bs_base, float* bd_base, int y)
        {
            const std::size_t P = F.pitch;
            const std::size_t row = F.row_offset(y);
            const int W = F.width;
            const float* bs = bs_base + row;
            float* bd = bd_base + row;
            float* d = F.d + row;
            float* s = F.s + row;
            float* sr = F.sr + row;
            const float* fl = F.fL + row;
            const float* fr = F.fR + row;
            const float* ft = F.fT + row;
            const float* fb = F.fB + row;

            const __m256 zero = _mm256_setzero_ps();
            const __m256 one = _mm256_set1_ps(1.0f);
            const __m256 rain = _mm256_set1_ps(c.rain_dt);
            const __m256 dt_area = _mm256_set1_ps(c.dt_over_area);
            const __m256 half = _mm256_set1_ps(c.half);
            const __m256 L = _mm256_set1_ps(c.cell_size);
            const __m256 min_depth = _mm256_set1_ps(c.min_depth);
            const __m256 inv2l = _mm256_set1_ps(c.inv_2L);
            const __m256 min_tilt = _mm256_set1_ps(c.min_tilt);
            const __m256 inv_ero = _mm256_set1_ps(c.inv_erosion_depth);
            const __m256 kc = _mm256_set1_ps(c.kc);
            const __m256 dtks = _mm256_set1_ps(c.dt_ks);
            const __m256 dtkd = _mm256_set1_ps(c.dt_kd);
            const __m256 dt = _mm256_set1_ps(c.dt);
            const __m256 area = _mm256_set1_ps(c.cell_area);
            const __m256 seps = _mm256_set1_ps(c.sediment_eps);
            const __m256 evap = _mm256_set1_ps(c.evap_factor);

            int x = 0;
            for (; x + kSimdFloats <= W; x += kSimdFloats)
            {
                const __m256 d1 = _mm256_add_ps(_mm256_load_ps(d + x), rain);
                const __m256 vfl = _mm256_load_ps(fl + x);
                const __m256 vfr = _mm256_load_ps(fr + x);
                const __m256 vft = _mm256_load_ps(ft + x);
                const __m256 vfb = _mm256_load_ps(fb + x);
                const __m256 in_l = _mm256_loadu_ps(fr + x - 1);
                const __m256 in_r = _mm256_loadu_ps(fl + x + 1);
                const __m256 in_t = _mm256_load_ps(fb + x - P);
                const __m256 in_b = _mm256_load_ps(ft + x + P);
                const __m256 fin = _mm256_add_ps(_mm256_add_ps(_mm256_add_ps(in_l, in_r), in_t), in_b);
                const __m256 fout = _mm256_add_ps(_mm256_add_ps(_mm256_add_ps(vfl, vfr), vft), vfb);

                const __m256 d2 = _mm256_max_ps(zero, _mm256_add_ps(d1, _mm256_mul_ps(_mm256_sub_ps(fin, fout), dt_area)));
                const __m256 dmean = _mm256_mul_ps(half, _mm256_add_ps(d1, d2));
                const __m256 wx = _mm256_mul_ps(half, _mm256_add_ps(_mm256_sub_ps(in_l, vfl), _mm256_sub_ps(vfr, in_r)));
                const __m256 wy = _mm256_mul_ps(half, _mm256_add_ps(_mm256_sub_ps(in_t, vft), _mm256_sub_ps(vfb, in_b)));

                // inv = (dmean > min_depth) ? 1 / (L * dmean) : 0   -> maska + AND
                const __m256 wet = _mm256_cmp_ps(dmean, min_depth, _CMP_GT_OQ);
                const __m256 inv = _mm256_and_ps(wet, _mm256_div_ps(one, _mm256_mul_ps(L, _mm256_max_ps(min_depth, dmean))));
                const __m256 u = _mm256_mul_ps(wx, inv);
                const __m256 v = _mm256_mul_ps(wy, inv);
                const __m256 speed = _mm256_sqrt_ps(_mm256_add_ps(_mm256_mul_ps(u, u), _mm256_mul_ps(v, v)));

                const __m256 gx = _mm256_mul_ps(_mm256_sub_ps(_mm256_loadu_ps(bs + x + 1), _mm256_loadu_ps(bs + x - 1)), inv2l);
                const __m256 gy = _mm256_mul_ps(_mm256_sub_ps(_mm256_load_ps(bs + x + P), _mm256_load_ps(bs + x - P)), inv2l);
                const __m256 g2 = _mm256_add_ps(_mm256_mul_ps(gx, gx), _mm256_mul_ps(gy, gy));
                const __m256 sin_a = _mm256_max_ps(min_tilt, _mm256_sqrt_ps(_mm256_div_ps(g2, _mm256_add_ps(one, g2))));
                const __m256 depth_factor = _mm256_min_ps(one, _mm256_mul_ps(dmean, inv_ero));
                const __m256 capacity = _mm256_mul_ps(_mm256_mul_ps(_mm256_mul_ps(kc, sin_a), speed), depth_factor);

                const __m256 s0 = _mm256_load_ps(s + x);
                const __m256 diff = _mm256_sub_ps(capacity, s0);
                const __m256 erode = _mm256_cmp_ps(diff, zero, _CMP_GT_OQ);
                const __m256 rate = _mm256_blendv_ps(dtkd, dtks, erode); // maska bira K_s ili K_d
                const __m256 amount = _mm256_mul_ps(rate, diff);

                _mm256_store_ps(bd + x, _mm256_sub_ps(_mm256_load_ps(bs + x), amount));
                const __m256 s1 = _mm256_add_ps(s0, amount);
                _mm256_store_ps(s + x, s1);
                const __m256 has_water = _mm256_cmp_ps(d1, seps, _CMP_GT_OQ);
                const __m256 ratio = _mm256_div_ps(_mm256_mul_ps(s1, dt), _mm256_mul_ps(_mm256_max_ps(seps, d1), area));
                _mm256_store_ps(sr + x, _mm256_and_ps(has_water, ratio));
                _mm256_store_ps(d + x, _mm256_mul_ps(d2, evap));
            }
            for (; x < W; ++x)
                ref::water_cell(F, c, bs_base, bd_base, row + static_cast<std::size_t>(x));
        }

        void sediment_row(const Fields& F, const StepConstants&, int y)
        {
            const std::size_t P = F.pitch;
            const std::size_t row = F.row_offset(y);
            const int W = F.width;
            float* s = F.s + row;
            const float* sr = F.sr + row;
            const float* fl = F.fL + row;
            const float* fr = F.fR + row;
            const float* ft = F.fT + row;
            const float* fb = F.fB + row;
            const __m256 zero = _mm256_setzero_ps();

            int x = 0;
            for (; x + kSimdFloats <= W; x += kSimdFloats)
            {
                const __m256 fout = _mm256_add_ps(_mm256_add_ps(_mm256_add_ps(_mm256_load_ps(fl + x), _mm256_load_ps(fr + x)),
                                                                _mm256_load_ps(ft + x)), _mm256_load_ps(fb + x));
                const __m256 a = _mm256_mul_ps(_mm256_loadu_ps(sr + x - 1), _mm256_loadu_ps(fr + x - 1));
                const __m256 b = _mm256_mul_ps(_mm256_loadu_ps(sr + x + 1), _mm256_loadu_ps(fl + x + 1));
                const __m256 t = _mm256_mul_ps(_mm256_load_ps(sr + x - P), _mm256_load_ps(fb + x - P));
                const __m256 u = _mm256_mul_ps(_mm256_load_ps(sr + x + P), _mm256_load_ps(ft + x + P));
                const __m256 sin = _mm256_add_ps(_mm256_add_ps(_mm256_add_ps(a, b), t), u);
                const __m256 s2 = _mm256_add_ps(_mm256_sub_ps(_mm256_load_ps(s + x), _mm256_mul_ps(_mm256_load_ps(sr + x), fout)), sin);
                _mm256_store_ps(s + x, _mm256_max_ps(zero, s2));
            }
            for (; x < W; ++x)
                ref::sediment_cell(F, row + static_cast<std::size_t>(x));
        }
    }

    const KernelSet& kernels_avx2()
    {
        static const KernelSet k{"avx2", flux_row, water_row, sediment_row, true};
        return k;
    }
#else
    const KernelSet& kernels_avx2()
    {
        // AVX2 build nije dostupan (npr. ne-x86 arhitektura): sigurna zamjena bez nepodržanih instrukcija.
        static const KernelSet k{"avx2(fallback:auto)", kernels_auto().flux, kernels_auto().water,
                                 kernels_auto().sediment, kernels_auto().requires_avx2};
        return k;
    }
#endif
}
