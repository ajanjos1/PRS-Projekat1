#include "simulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace sw
{
    namespace
    {
        std::size_t round_up(std::size_t v, std::size_t m) { return (v + m - 1) / m * m; }
    }

    // ------------------------------------------------------------------ konstante i CFL
    float cfl_dt_limit(const SimParams& p)
    {
        // Linearizacijom modela cijevi dobija se talasna jednačina sa brzinom c = sqrt(g A / L)
        // (efektivna dubina A / L). Za eksplicitnu šemu "naprijed-nazad" na 5-tačkastom stencilu
        // u 2D uslov stabilnosti glasi c * dt / L <= 1 / sqrt(2).
        const double c = std::sqrt(static_cast<double>(p.gravity) * p.pipe_area / p.cell_size);
        return static_cast<float>(p.cfl * p.cell_size / (std::sqrt(2.0) * c));
    }

    StepConstants make_step_constants(const SimParams& p)
    {
        if (p.cell_size <= 0 || p.pipe_area <= 0 || p.gravity <= 0 || p.dt <= 0)
            throw std::invalid_argument("Neispravni parametri simulacije");

        const float dt = std::min(p.dt, cfl_dt_limit(p));
        StepConstants c{};
        c.dt = dt;
        c.rain_dt = p.rain_rate * dt;
        c.flux_coef = dt * p.pipe_area * p.gravity / p.cell_size;
        c.cell_area = p.cell_size * p.cell_size;
        c.dt_over_area = dt / c.cell_area;
        c.cell_size = p.cell_size;
        c.inv_2L = 1.0f / (2.0f * p.cell_size);
        c.half = 0.5f;
        c.dt_ks = dt * p.dissolve;
        c.dt_kd = dt * p.deposit;
        c.kc = p.capacity;
        c.min_tilt = p.min_tilt;
        c.min_depth = p.min_depth;
        c.inv_erosion_depth = 1.0f / p.erosion_depth;
        c.sediment_eps = p.sediment_eps;
        c.evap_factor = 1.0f - p.evaporation * dt;
        c.tiny = 1e-30f;

        // Eksplicitne relaksacije erozije/depozicije moraju biti stabilne i ne smiju "preskočiti" ravnotežu.
        if (c.dt_ks > 1.0f || c.dt_kd > 1.0f || c.evap_factor < 0.0f)
            throw std::invalid_argument("dt * K_s, dt * K_d i dt * K_e moraju biti <= 1");
        return c;
    }

    // ------------------------------------------------------------------ stanje
    Simulation::Simulation(int width, int height)
        : _w(width), _h(height)
    {
        if (width < 1 || height < 1)
            throw std::invalid_argument("Dimenzije mreže moraju biti pozitivne");

        _pitch = round_up(static_cast<std::size_t>(kPadLeft + width + 1), kSimdFloats);
        _rows = static_cast<std::size_t>(height) + 2;
        const std::size_t n = _pitch * _rows;

        _b[0].allocate(n);
        _b[1].allocate(n);
        _d.allocate(n);
        _s.allocate(n);
        _sr.allocate(n);
        _fL.allocate(n);
        _fR.allocate(n);
        _fT.allocate(n);
        _fB.allocate(n);

        const std::size_t mask_len = round_up(static_cast<std::size_t>(width), kSimdFloats) + kSimdFloats;
        _openL.allocate(mask_len);
        _openR.allocate(mask_len);
        for (int x = 0; x < width; ++x)
        {
            _openL.data()[x] = (x > 0) ? 1.0f : 0.0f;
            _openR.data()[x] = (x < width - 1) ? 1.0f : 0.0f;
        }
    }

    Fields Simulation::fields()
    {
        Fields f{};
        f.d = _d.data();
        f.s = _s.data();
        f.sr = _sr.data();
        f.fL = _fL.data();
        f.fR = _fR.data();
        f.fT = _fT.data();
        f.fB = _fB.data();
        f.openL = _openL.data();
        f.openR = _openR.data();
        f.pitch = _pitch;
        f.width = _w;
        f.height = _h;
        return f;
    }

    void Simulation::refresh_halo(float* b, int width, int height, std::size_t pitch)
    {
        for (int y = 0; y < height; ++y)
        {
            float* row = b + static_cast<std::size_t>(y + 1) * pitch + kPadLeft;
            row[-1] = row[0];
            row[width] = row[width - 1];
        }
        float* top = b + kPadLeft;
        float* first = b + pitch + kPadLeft;
        float* last = b + static_cast<std::size_t>(height) * pitch + kPadLeft;
        float* bottom = b + static_cast<std::size_t>(height + 1) * pitch + kPadLeft;
        std::memcpy(top, first, static_cast<std::size_t>(width) * sizeof(float));
        std::memcpy(bottom, last, static_cast<std::size_t>(width) * sizeof(float));
    }

    void Simulation::copy_state_from(const Simulation& o)
    {
        if (o._w != _w || o._h != _h)
            throw std::invalid_argument("copy_state_from: različite dimenzije");
        _b[0].copy_from(o._b[0]);
        _b[1].copy_from(o._b[1]);
        _cur = o._cur;
        _d.copy_from(o._d);
        _s.copy_from(o._s);
        _sr.copy_from(o._sr);
        _fL.copy_from(o._fL);
        _fR.copy_from(o._fR);
        _fT.copy_from(o._fT);
        _fB.copy_from(o._fB);
    }

    void Simulation::clear_dynamic_state()
    {
        for (AlignedBuffer* buf : {&_d, &_s, &_sr, &_fL, &_fR, &_fT, &_fB})
            std::memset(buf->data(), 0, buf->size() * sizeof(float));
    }

    // ------------------------------------------------------------------ dijagnostika
    Totals compute_totals(const Simulation& sim)
    {
        Totals t;
        t.min_water = 3.4e38f;
        t.max_water = -3.4e38f;
        t.min_sediment = 3.4e38f;
        const float* b = sim.terrain();
        const float* d = sim.water();
        const float* s = sim.sediment();
        for (int y = 0; y < sim.height(); ++y)
            for (int x = 0; x < sim.width(); ++x)
            {
                const std::size_t i = sim.index(x, y);
                t.terrain += b[i];
                t.water += d[i];
                t.sediment += s[i];
                t.min_water = std::min(t.min_water, d[i]);
                t.max_water = std::max(t.max_water, d[i]);
                t.min_sediment = std::min(t.min_sediment, s[i]);
                if (!std::isfinite(b[i]) || !std::isfinite(d[i]) || !std::isfinite(s[i]))
                    t.finite = false;
            }
        return t;
    }

    double state_checksum(const Simulation& sim)
    {
        double sum = 0;
        const float* b = sim.terrain();
        const float* d = sim.water();
        const float* s = sim.sediment();
        const float* fl = sim.flux_left();
        const float* fr = sim.flux_right();
        const float* ft = sim.flux_top();
        const float* fb = sim.flux_bottom();
        for (int y = 0; y < sim.height(); ++y)
            for (int x = 0; x < sim.width(); ++x)
            {
                const std::size_t i = sim.index(x, y);
                sum += b[i] + 3.0 * d[i] + 7.0 * s[i] + 0.5 * (fl[i] + fr[i] + ft[i] + fb[i]);
            }
        return sum;
    }

    double StateDiff::max_abs() const
    {
        return std::max({terrain.max_abs, water.max_abs, sediment.max_abs, flux.max_abs});
    }

    namespace
    {
        void diff_field(const Simulation& ref, const Simulation& oth, const float* a, const float* b,
                        double atol, double rtol, FieldDiff& out)
        {
            for (int y = 0; y < ref.height(); ++y)
                for (int x = 0; x < ref.width(); ++x)
                {
                    const std::size_t i = ref.index(x, y);
                    const std::size_t j = oth.index(x, y);
                    const double r = a[i];
                    const double v = b[j];
                    if (a[i] != b[j])
                        out.bitwise_equal = false;
                    const double e = std::fabs(r - v);
                    out.max_abs = std::max(out.max_abs, e);
                    if (std::fabs(r) > atol)
                        out.max_rel = std::max(out.max_rel, e / std::fabs(r));
                    if (!std::isfinite(v) || e > atol + rtol * std::fabs(r))
                        ++out.violations;
                }
        }
    }

    StateDiff compare_states(const Simulation& ref, const Simulation& other, double atol, double rtol)
    {
        if (ref.width() != other.width() || ref.height() != other.height())
            throw std::invalid_argument("compare_states: različite dimenzije");
        StateDiff d;
        diff_field(ref, other, ref.terrain(), other.terrain(), atol, rtol, d.terrain);
        diff_field(ref, other, ref.water(), other.water(), atol, rtol, d.water);
        diff_field(ref, other, ref.sediment(), other.sediment(), atol, rtol, d.sediment);
        diff_field(ref, other, ref.flux_left(), other.flux_left(), atol, rtol, d.flux);
        diff_field(ref, other, ref.flux_right(), other.flux_right(), atol, rtol, d.flux);
        diff_field(ref, other, ref.flux_top(), other.flux_top(), atol, rtol, d.flux);
        diff_field(ref, other, ref.flux_bottom(), other.flux_bottom(), atol, rtol, d.flux);
        return d;
    }

    std::vector<float> extract(const Simulation& sim, const float* field)
    {
        std::vector<float> out(sim.cells());
        for (int y = 0; y < sim.height(); ++y)
            std::memcpy(out.data() + static_cast<std::size_t>(y) * sim.width(), field + sim.index(0, y),
                        static_cast<std::size_t>(sim.width()) * sizeof(float));
        return out;
    }

    std::vector<float> velocity_magnitude(const Simulation& sim, const SimParams& p)
    {
        // Ista formula kao u fazi 2 (Mei i dr.), ali sa trenutnom dubinom umjesto prosjeka d1/d2.
        std::vector<float> out(sim.cells(), 0.0f);
        const std::size_t P = sim.pitch();
        const float* d = sim.water();
        const float* fl = sim.flux_left();
        const float* fr = sim.flux_right();
        const float* ft = sim.flux_top();
        const float* fb = sim.flux_bottom();
        for (int y = 0; y < sim.height(); ++y)
            for (int x = 0; x < sim.width(); ++x)
            {
                const std::size_t i = sim.index(x, y);
                const float wx = 0.5f * ((fr[i - 1] - fl[i]) + (fr[i] - fl[i + 1]));
                const float wy = 0.5f * ((fb[i - P] - ft[i]) + (fb[i] - ft[i + P]));
                const float dm = d[i];
                if (dm > p.min_depth)
                {
                    const float u = wx / (p.cell_size * dm);
                    const float v = wy / (p.cell_size * dm);
                    out[static_cast<std::size_t>(y) * sim.width() + x] = std::sqrt(u * u + v * v);
                }
            }
        return out;
    }
}
