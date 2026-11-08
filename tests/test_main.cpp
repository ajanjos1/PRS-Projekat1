// prs_erosion_tests – pokreće sve registrovane testove.
//
//   prs_erosion_tests              svi testovi
//   prs_erosion_tests Rainfall     samo testovi čija grupa ili ime sadrži "Rainfall"
//
// Testovi traže primjere height mapa u assets/heightmaps (relativno prema tekućem folderu); CTest ih
// pokreće iz korijena projekta. Ako primjeri nisu dostupni, ti dijelovi se preskaču uz poruku.

#include "TestFramework.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>

namespace prs::test
{
    namespace
    {
        int g_checks = 0;
        int g_failures = 0;
        int g_testFailures = 0;

        // Deterministički generator (LCG) – isti brojevi na svim platformama.
        struct Lcg
        {
            std::uint32_t state;
            double next()
            {
                state = state * 1664525u + 1013904223u;
                return (state >> 8) * (1.0 / 16777216.0); // [0, 1)
            }
        };
    }

    std::vector<TestCase>& registry()
    {
        static std::vector<TestCase> tests;
        return tests;
    }

    Registrar::Registrar(const char* group, const char* name, void (*body)())
    {
        registry().push_back({group, name, body});
    }

    void check(bool condition, const std::string& expression, const char* file, int line)
    {
        ++g_checks;
        if (!condition)
        {
            ++g_failures;
            std::cout << "      FAILED: " << expression << "  (" << file << ":" << line << ")\n";
        }
    }

    void checkNear(double actual, double expected, double tolerance, const std::string& expression, const char* file,
                   int line)
    {
        const bool ok = std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
        ++g_checks;
        if (!ok)
        {
            ++g_failures;
            std::cout << "      FAILED: " << expression << ": " << std::setprecision(17) << actual << " vs " << expected
                      << " (tolerancija " << tolerance << ")  (" << file << ":" << line << ")\n";
        }
    }

    void checkLessEqual(double actual, double limit, const std::string& expression, const char* file, int line)
    {
        const bool ok = std::isfinite(actual) && actual <= limit;
        ++g_checks;
        if (!ok)
        {
            ++g_failures;
            std::cout << "      FAILED: " << expression << ": " << std::setprecision(17) << actual << " > " << limit << "  ("
                      << file << ":" << line << ")\n";
        }
    }

    void note(const std::string& message)
    {
        std::cout << "      " << message << "\n";
    }

    std::string fmt(double value, int precision)
    {
        std::ostringstream os;
        os << std::setprecision(precision) << value;
        return os.str();
    }

    Heightmap flatTerrain(int width, int height, double elevation, double cellSize)
    {
        Heightmap m;
        m.scale.cellSizeMeters = cellSize;
        m.scale.minElevationMeters = 0.0;
        m.scale.maxElevationMeters = 100.0;
        m.elevation.resize(width, height, elevation);
        m.sourceName = "flat";
        return m;
    }

    Heightmap slopeTerrain(int width, int height, double drop, double cellSize, double noise, std::uint32_t seed)
    {
        Heightmap m = flatTerrain(width, height, 0.0, cellSize);
        Lcg rng{seed};
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                m.elevation(x, y) = 10.0 + drop * (1.0 - static_cast<double>(x) / (width - 1)) + noise * (rng.next() - 0.5);
        m.sourceName = "slope";
        return m;
    }

    Heightmap roughTerrain(int width, int height, double relief, double cellSize, std::uint32_t seed)
    {
        Heightmap m = flatTerrain(width, height, 0.0, cellSize);
        Lcg rng{seed};
        const double p1 = rng.next() * 6.28, p2 = rng.next() * 6.28, p3 = rng.next() * 6.28;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const double fx = static_cast<double>(x) / width, fy = static_cast<double>(y) / height;
                const double smooth = 0.5 + 0.25 * std::sin(6.0 * fx + p1) * std::cos(5.0 * fy + p2) +
                                      0.15 * std::sin(17.0 * (fx + fy) + p3);
                m.elevation(x, y) = 10.0 + relief * (smooth + 0.1 * (rng.next() - 0.5));
            }
        m.sourceName = "rough";
        return m;
    }

    Heightmap mountainTerrain(int width, int height, double peak, double cellSize, std::uint32_t seed)
    {
        Heightmap m = flatTerrain(width, height, 0.0, cellSize);
        Lcg rng{seed};
        const double cx = 0.5 * (width - 1), cy = 0.5 * (height - 1);
        const double sigma = 0.3 * std::min(width, height);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const double dx = x - cx, dy = y - cy;
                const double r2 = (dx * dx + dy * dy) / (sigma * sigma);
                const double angle = std::atan2(dy, dx);
                // Gaussovo brdo; kosinus po uglu urezuje 7 radijalnih dolina (voda se u njima skuplja)
                const double valleys = 1.0 - 0.15 * (1.0 + std::cos(7.0 * angle)) * std::min(1.0, std::sqrt(r2));
                m.elevation(x, y) = 5.0 + peak * std::exp(-0.5 * r2) * valleys + 0.02 * peak * (rng.next() - 0.5);
            }
        m.sourceName = "mountain";
        return m;
    }
}

int main(int argc, char** argv)
{
    using namespace prs::test;
    const std::string filter = argc > 1 ? argv[1] : "";
    int run = 0;
    const auto start = std::chrono::steady_clock::now();
    for (const TestCase& t : registry())
    {
        if (!filter.empty() && t.group.find(filter) == std::string::npos && t.name.find(filter) == std::string::npos)
            continue;
        ++run;
        std::cout << "[ RUN  ] " << t.group << " / " << t.name << "\n" << std::flush;
        const int before = g_failures;
        const auto t0 = std::chrono::steady_clock::now();
        try
        {
            t.body();
        }
        catch (const std::exception& e)
        {
            ++g_failures;
            std::cout << "      EXCEPTION: " << e.what() << "\n";
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const bool ok = g_failures == before;
        if (!ok)
            ++g_testFailures;
        std::cout << (ok ? "[  OK  ] " : "[ FAIL ] ") << t.group << " / " << t.name << "  (" << std::fixed
                  << std::setprecision(2) << seconds << " s)\n"
                  << std::defaultfloat << std::flush;
    }
    const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "\nTestova: " << run << ", neuspjelih testova: " << g_testFailures << ", provjera: " << g_checks
              << ", neuspjelih provjera: " << g_failures << ", ukupno " << std::fixed << std::setprecision(1) << total
              << " s\n";
    return (g_failures == 0 && run > 0) ? 0 : 1;
}
