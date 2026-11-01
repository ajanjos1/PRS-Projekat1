#pragma once
// Mjerni modul (proširenje measure() iz bench.hpp sa vježbi):
//  - prepare() se izvršava PRIJE svakog mjerenja i NIJE uključen u vrijeme (vraća početno stanje),
//  - warm-up izvršavanja se ne mjere,
//  - std::chrono::steady_clock + compiler fence oko mjerenog dijela,
//  - observe() + consume() sprečavaju da kompajler ukloni "neiskorišten" rad,
//  - statistika: medijan, minimum, maksimum, srednja vrijednost, standardna devijacija.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sw::bench
{
    struct Stats
    {
        double median_s = 0;
        double min_s = 0;
        double max_s = 0;
        double mean_s = 0;
        double stddev_s = 0; // uzoračka standardna devijacija (n - 1)
        std::vector<double> runs_s;
    };

    Stats summarize(std::vector<double> runs);
    void consume(double value);

    template <class Prepare, class Run, class Observe>
    Stats measure(int warmups, int repeats, Prepare&& prepare, Run&& run, Observe&& observe)
    {
        using clock = std::chrono::steady_clock;
        if (repeats <= 0 || warmups < 0)
            throw std::invalid_argument("Neispravan broj ponavljanja/warm-up izvršavanja");

        for (int i = 0; i < warmups; ++i)
        {
            prepare();
            run();
            consume(observe());
        }

        std::vector<double> seconds;
        seconds.reserve(static_cast<std::size_t>(repeats));
        for (int i = 0; i < repeats; ++i)
        {
            prepare();
            std::atomic_signal_fence(std::memory_order_seq_cst);
            const auto start = clock::now();
            run();
            const auto end = clock::now();
            std::atomic_signal_fence(std::memory_order_seq_cst);
            consume(observe());
            seconds.push_back(std::chrono::duration<double>(end - start).count());
        }
        return summarize(std::move(seconds));
    }
}
