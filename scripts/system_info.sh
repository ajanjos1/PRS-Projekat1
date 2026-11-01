#!/usr/bin/env bash
# Evidentira testno okruženje (CPU, jezgre, RAM, OS, kompajler, zastavice, SIMD, OpenMP).
# Upotreba: scripts/system_info.sh build/prs_swe > results/system_info.txt
set -u
BIN="${1:-build/prs_swe}"

echo "=== Datum ==="; date -Is
echo; echo "=== CPU (lscpu) ==="
lscpu 2>/dev/null | grep -E "Model name|Architecture|^CPU\(s\)|Thread\(s\) per core|Core\(s\) per socket|Socket\(s\)|L1d|L2|L3|Hypervisor|NUMA node\(s\)|CPU max MHz" || sysctl -n machdep.cpu.brand_string
echo "SIMD zastavice: $(grep -m1 -o -w -E 'sse4_2|avx|avx2|fma|avx512f' /proc/cpuinfo 2>/dev/null | sort -u | tr '\n' ' ')"
echo; echo "=== Memorija ==="; free -h 2>/dev/null | head -2
echo; echo "=== OS ==="; (grep PRETTY_NAME /etc/os-release 2>/dev/null || uname -s); uname -r
echo; echo "=== Kompajler ==="; c++ --version 2>/dev/null | head -1; cmake --version | head -1
echo; echo "=== OpenMP okruženje ==="; env | grep -E '^OMP_|^GOMP_' || echo "(nijedna OMP_* varijabla nije postavljena - koriste se podrazumijevane vrijednosti)"
echo; echo "=== Program ==="; "$BIN" info
