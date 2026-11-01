#!/usr/bin/env bash
# Kompletan eksperimentalni postupak (Linux/macOS): build, testovi, izvještaji, benchmark, simulacija, grafikoni.
# Upotreba (iz korijena projekta):  bash scripts/run_benchmarks.sh
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p results/figures results/vectorization

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# 1) okruženje
bash scripts/system_info.sh build/prs_swe | tee results/system_info.txt

# 2) testovi ispravnosti
./build/prs_tests results/correctness_report.csv | tee results/correctness_tests_output.txt

# 3) izvještaji kompajlera o vektorizaciji (zaseban build)
cmake -S . -B build-vecreport -DCMAKE_BUILD_TYPE=Release -DPRS_VECTORIZATION_REPORTS=ON > /dev/null
touch src/kernels_*.cpp
cmake --build build-vecreport --target prs_core -j1 2> results/vectorization/compiler_report.txt > /dev/null || true

# 4) propusnost memorije (STREAM triad) i benchmark
./build/prs_swe stream --threads 1,2,4,8 --out results/stream.csv | tee results/stream_output.txt
./build/prs_swe bench --sizes 128,256,512,1024 --threads 1,2,4,8 --reps 10 --warmup 2 \
    --out results/benchmarks.csv --runs-out results/benchmark_runs.csv | tee results/benchmark_output.txt
# dimenzija koja nije djeljiva sa 8 (skalarni ostatak u SIMD petljama)
./build/prs_swe bench --sizes 1000,250 --threads 1,2 --reps 10 --warmup 2 --no-e2e --no-ablation \
    --out results/benchmarks_non_multiple8.csv --runs-out results/benchmark_runs_non_multiple8.csv | tee results/benchmark_non_multiple8_output.txt

# 5) simulacija za vizualizaciju i grafikoni
./build/prs_swe simulate --size 512 --steps 6000 --variant G_omp_avx2 --threads 2 --snap 1500 --diag 100 --out results/sim512
python3 scripts/visualize_simulation.py results/sim512 results/figures
python3 scripts/plot_results.py results results/figures
