#pragma once
// Referentne (skalarne) operacije nad jednom ćelijom. Ovo je "specifikacija" numeričkog modela:
// sve ostale implementacije (auto, OpenMP SIMD, AVX2) moraju izvršiti ISTE aritmetičke operacije
// istim redoslijedom, pa se rezultati mogu porediti bit-po-bit (uz -ffp-contract=off, bez -ffast-math).
//
// Oznake (Mei i dr. 2007): b - teren, d - voda, s - sediment, f^{L,R,T,B} - izlazni protok,
// h = b + d - ukupna visina, l - dužina cijevi (= L), A - presjek cijevi.

#include "simulation.hpp"

#include <cmath>
#include <cstddef>

namespace sw::ref
{
    // ---------------------------------------------------------------- Faza 1
    // d1 = d + r dt
    // f_new = max(0, f + dt A g (h - h_susjed) / l)      (za svaki od 4 smjera; zid -> 0)
    // K = min(1, d1 lx ly / ((fL+fR+fT+fB) dt))           (izlaz ne smije premašiti zapreminu vode)
    inline void flux_cell(const Fields& F, const StepConstants& c, const float* b, std::size_t i, int x,
                          float open_t, float open_b)
    {
        const std::size_t P = F.pitch;
        const float* d = F.d;
        const float d1 = d[i] + c.rain_dt;
        const float h = b[i] + d1;
        const float hL = b[i - 1] + (d[i - 1] + c.rain_dt);
        const float hR = b[i + 1] + (d[i + 1] + c.rain_dt);
        const float hT = b[i - P] + (d[i - P] + c.rain_dt);
        const float hB = b[i + P] + (d[i + P] + c.rain_dt);

        float nl = F.fL[i] + c.flux_coef * (h - hL);
        float nr = F.fR[i] + c.flux_coef * (h - hR);
        float nt = F.fT[i] + c.flux_coef * (h - hT);
        float nb = F.fB[i] + c.flux_coef * (h - hB);
        if (nl < 0.0f) nl = 0.0f;
        if (nr < 0.0f) nr = 0.0f;
        if (nt < 0.0f) nt = 0.0f;
        if (nb < 0.0f) nb = 0.0f;
        nl *= F.openL[x];
        nr *= F.openR[x];
        nt *= open_t;
        nb *= open_b;

        const float out = (((nl + nr) + nt) + nb) * c.dt;
        const float vol = d1 * c.cell_area;
        float denom = out;
        if (denom < c.tiny)
            denom = c.tiny;
        float k = vol / denom;
        if (k > 1.0f)
            k = 1.0f;

        F.fL[i] = nl * k;
        F.fR[i] = nr * k;
        F.fT[i] = nt * k;
        F.fB[i] = nb * k;
    }

    // ---------------------------------------------------------------- Faza 2
    // d2 = d1 + dt (sum f_in - sum f_out) / (lx ly)
    // u = dWx / (ly d_mean),  v = dWy / (lx d_mean),  d_mean = (d1 + d2) / 2
    // C = K_c * max(sin(alpha), min_tilt) * |v| * min(1, d_mean / d_ero)
    // C > s: erozija  (b -= dt K_s (C - s), s += dt K_s (C - s))
    // C <= s: depozicija (b += dt K_d (s - C), s -= dt K_d (s - C))
    // d3 = d2 (1 - K_e dt)  (isparavanje)
    inline void water_cell(const Fields& F, const StepConstants& c, const float* bs, float* bd, std::size_t i)
    {
        const std::size_t P = F.pitch;
        const float d1 = F.d[i] + c.rain_dt;
        const float fl = F.fL[i];
        const float fr = F.fR[i];
        const float ft = F.fT[i];
        const float fb = F.fB[i];
        const float in_l = F.fR[i - 1]; // lijevi susjed šalje desno
        const float in_r = F.fL[i + 1]; // desni susjed šalje lijevo
        const float in_t = F.fB[i - P]; // gornji susjed šalje dolje
        const float in_b = F.fT[i + P]; // donji susjed šalje gore
        const float fin = ((in_l + in_r) + in_t) + in_b;
        const float fout = ((fl + fr) + ft) + fb;

        float d2 = d1 + (fin - fout) * c.dt_over_area;
        if (d2 < 0.0f)
            d2 = 0.0f;
        const float dmean = c.half * (d1 + d2);
        const float wx = c.half * ((in_l - fl) + (fr - in_r));
        const float wy = c.half * ((in_t - ft) + (fb - in_b));

        float inv = 0.0f;
        if (dmean > c.min_depth)
            inv = 1.0f / (c.cell_size * dmean);
        const float u = wx * inv;
        const float v = wy * inv;
        const float speed = std::sqrt(u * u + v * v);

        const float gx = (bs[i + 1] - bs[i - 1]) * c.inv_2L;
        const float gy = (bs[i + P] - bs[i - P]) * c.inv_2L;
        const float g2 = gx * gx + gy * gy;
        float sin_a = std::sqrt(g2 / (1.0f + g2));
        if (sin_a < c.min_tilt)
            sin_a = c.min_tilt;
        float depth_factor = dmean * c.inv_erosion_depth;
        if (depth_factor > 1.0f)
            depth_factor = 1.0f;
        const float capacity = ((c.kc * sin_a) * speed) * depth_factor;

        const float s0 = F.s[i];
        const float diff = capacity - s0;
        float amount;
        if (diff > 0.0f)
            amount = c.dt_ks * diff; // erozija
        else
            amount = c.dt_kd * diff; // depozicija (amount <= 0)

        bd[i] = bs[i] - amount;
        const float s1 = s0 + amount;
        F.s[i] = s1;

        float ratio = 0.0f;
        if (d1 > c.sediment_eps)
            ratio = (s1 * c.dt) / (d1 * c.cell_area);
        F.sr[i] = ratio;
        F.d[i] = d2 * c.evap_factor;
    }

    // ---------------------------------------------------------------- Faza 3
    // Konzervativni upwind transport: ćelija šalje udio sedimenta jednak udjelu vode koji je napustio
    // ćeliju (s * dt * f / V), a prima isti iznos koji je susjed poslao prema njoj.
    inline void sediment_cell(const Fields& F, std::size_t i)
    {
        const std::size_t P = F.pitch;
        const float fout = ((F.fL[i] + F.fR[i]) + F.fT[i]) + F.fB[i];
        const float a = F.sr[i - 1] * F.fR[i - 1];
        const float b = F.sr[i + 1] * F.fL[i + 1];
        const float t = F.sr[i - P] * F.fB[i - P];
        const float u = F.sr[i + P] * F.fT[i + P];
        const float sin = ((a + b) + t) + u;
        float s2 = (F.s[i] - F.sr[i] * fout) + sin;
        if (s2 < 0.0f)
            s2 = 0.0f;
        F.s[i] = s2;
    }
}
