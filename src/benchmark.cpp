#include "benchmark.hpp"
#include "sw_common.hpp"

#include <cstdint>
#include <cstring>
#include <numeric>

namespace sw::bench
{
    namespace
    {
        std::atomic<std::uint64_t> observed_bits{0};
    }

    SW_NOINLINE void consume(double value)
    {
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&bits, &value, sizeof(bits));
        observed_bits.fetch_xor(bits, std::memory_order_relaxed);
    }

    Stats summarize(std::vector<double> runs)
    {
        Stats s;
        s.runs_s = runs;
        std::vector<double> sorted = std::move(runs);
        std::sort(sorted.begin(), sorted.end());
        const std::size_t n = sorted.size();
        const std::size_t mid = n / 2;
        s.median_s = (n % 2) ? sorted[mid] : 0.5 * (sorted[mid - 1] + sorted[mid]);
        s.min_s = sorted.front();
        s.max_s = sorted.back();
        s.mean_s = std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(n);
        double var = 0;
        for (double v : sorted)
            var += (v - s.mean_s) * (v - s.mean_s);
        s.stddev_s = n > 1 ? std::sqrt(var / static_cast<double>(n - 1)) : 0.0;
        return s;
    }
}
