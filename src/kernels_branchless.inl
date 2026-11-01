// Zajedničko tijelo "branchless" kernela (bez if naredbi u petlji, sa __restrict parametrima).
// Uključuje se u dva prevodna fajla sa različitim zastavicama:
//   kernels_auto.cpp               -> B: kompajler vektorizuje petlje (AVX2, 8 traka)
//   kernels_scalar_branchless.cpp  -> A2 (ablacija): isti izvor, vektorizacija isključena
// Prije uključivanja treba definisati SW_BRANCHLESS_LOOP_HINT (prazno ili SW_NO_VECTORIZE).
// Napomena: fajl namjerno nema include guard i uključuje se unutar anonimnog namespace-a.

        void flux_row_impl(int W, const float* SW_RESTRICT b, const float* SW_RESTRICT b_up,
                           const float* SW_RESTRICT b_dn, const float* SW_RESTRICT d, const float* SW_RESTRICT d_up,
                           const float* SW_RESTRICT d_dn, float* SW_RESTRICT fl, float* SW_RESTRICT fr,
                           float* SW_RESTRICT ft, float* SW_RESTRICT fb, const float* SW_RESTRICT open_l,
                           const float* SW_RESTRICT open_r, const StepConstants& c, float open_t, float open_b)
        {
            const float rain = c.rain_dt, kf = c.flux_coef, dt = c.dt, area = c.cell_area, tiny = c.tiny;

            SW_BRANCHLESS_LOOP_HINT
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

        void flux_row(const Fields& F, const StepConstants& c, const float* b_base, int y)
        {
            const std::size_t P = F.pitch;
            const std::size_t row = F.row_offset(y);
            const float* b = b_base + row;
            const float* d = F.d + row;
            flux_row_impl(F.width, b, b - P, b + P, d, d - P, d + P, F.fL + row, F.fR + row, F.fT + row, F.fB + row,
                          F.openL, F.openR, c, (y > 0) ? 1.0f : 0.0f, (y < F.height - 1) ? 1.0f : 0.0f);
        }

        void water_row_impl(int W, const float* SW_RESTRICT bs, const float* SW_RESTRICT bs_up,
                            const float* SW_RESTRICT bs_dn, float* SW_RESTRICT bd, float* SW_RESTRICT d,
                            float* SW_RESTRICT s, float* SW_RESTRICT sr, const float* SW_RESTRICT fl,
                            const float* SW_RESTRICT fr, const float* SW_RESTRICT ft, const float* SW_RESTRICT fb,
                            const float* SW_RESTRICT fb_up, const float* SW_RESTRICT ft_dn, const StepConstants& c)
        {
            const float rain = c.rain_dt, dt_area = c.dt_over_area, half = c.half, L = c.cell_size;
            const float min_depth = c.min_depth, inv2l = c.inv_2L, min_tilt = c.min_tilt;
            const float inv_ero = c.inv_erosion_depth, kc = c.kc, dtks = c.dt_ks, dtkd = c.dt_kd;
            const float dt = c.dt, area = c.cell_area, seps = c.sediment_eps, evap = c.evap_factor;

            SW_BRANCHLESS_LOOP_HINT
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

        void water_row(const Fields& F, const StepConstants& c, const float* bs_base, float* bd_base, int y)
        {
            const std::size_t P = F.pitch;
            const std::size_t row = F.row_offset(y);
            const float* bs = bs_base + row;
            water_row_impl(F.width, bs, bs - P, bs + P, bd_base + row, F.d + row, F.s + row, F.sr + row, F.fL + row,
                           F.fR + row, F.fT + row, F.fB + row, F.fB + row - P, F.fT + row + P, c);
        }

        void sediment_row_impl(int W, float* SW_RESTRICT s, const float* SW_RESTRICT sr,
                               const float* SW_RESTRICT sr_up, const float* SW_RESTRICT sr_dn,
                               const float* SW_RESTRICT fl, const float* SW_RESTRICT fr, const float* SW_RESTRICT ft,
                               const float* SW_RESTRICT fb, const float* SW_RESTRICT fb_up,
                               const float* SW_RESTRICT ft_dn)
        {
            SW_BRANCHLESS_LOOP_HINT
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

        void sediment_row(const Fields& F, const StepConstants&, int y)
        {
            const std::size_t P = F.pitch;
            const std::size_t row = F.row_offset(y);
            const float* sr = F.sr + row;
            sediment_row_impl(F.width, F.s + row, sr, sr - P, sr + P, F.fL + row, F.fR + row, F.fT + row, F.fB + row,
                              F.fB + row - P, F.fT + row + P);
        }
