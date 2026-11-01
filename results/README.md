# `results/` – referentna mjerenja iz rada

Podaci koje su proizveli program `prs_swe`, testovi i Python skripte: mjerenja performansi, rezultati testova, informacije o
okruženju, izvještaji kompajlera, snimci simulacije i grafikoni. **Izmjereno na Linux VM-u** (2 vCPU Intel Xeon @ 2,10 GHz,
KVM, GCC 13.3, Ubuntu 24.04, 2026-11-01), pa su ovo „podaci iz rada", a ne podaci s računara na kojem se projekat pokreće.
Opšti opis projekta: [korijenski README](../README.md).

## Sadržaj

| Fajl / folder | Veličina | Šta je | Nastaje |
|---|---|---|---|
| `benchmarks.csv` | 37 KB (144 reda + zaglavlje) | glavna mjerenja: varijante A–G i ablacije, mreže 128–1024, `kernel` i `e2e` | `prs_swe bench` (`run_benchmarks.sh`, korak 4) |
| `benchmark_runs.csv` | 59 KB | sva pojedinačna ponavljanja (`grid,variant,measure,threads,run,time_ms`) | isto |
| `benchmark_output.txt` | 17 KB | ispis konzole za `bench` | isto |
| `benchmarks_non_multiple8.csv`, `benchmark_runs_non_multiple8.csv`, `benchmark_non_multiple8_output.txt` | 5 / 8 / 3 KB | mreže 1000² i 250² (širina nije višekratnik od 8 → skalarni ostatak u SIMD petljama) | `run_benchmarks.sh`, korak 4 |
| `correctness_report.csv`, `correctness_tests_output.txt` | 28 KB / 28 KB | rezultat 335 testova (`test,detail,result,value`) i ispis | `prs_tests` (korak 2) |
| `stream.csv`, `stream_output.txt` | 0,2 KB | STREAM triad, nizovi od 128 MB, 1/2/4/8 dretvi (≈ 15 GB/s s jednom, ≈ 35–38 GB/s s dvije i više dretvi) | `prs_swe stream` (korak 4) |
| `stream_l3.csv`, `stream_l3_output.txt` | 0,1 KB | STREAM s nizovima od 12 MB (stane u L3 keš) | **ručno** (`stream --mb 12`) |
| `recheck_512_p1.csv`, `recheck_512_p1_runs.csv` | 1 / 5 KB | kontrolno mjerenje 512², 1 dretva, 30 ponavljanja, 3 warm-up; varijante A, E, A2, E2 (provjera šuma) | **ručno** |
| `system_info.txt` | 2 KB | CPU, jezgre, keš, RAM, OS, kompajler, OpenMP, izlaz `prs_swe info` | `system_info.sh` (korak 1) |
| `.bench_done` | 5 B | marker s tekstom `DONE`; nijedna skripta u repou ga ne koristi | nepoznato (vjerovatno vanjska orkestracija mjerenja) |
| [`figures/`](figures/README.md) | 2,9 MB | 11 PNG slika i `sim_summary.txt` | `plot_results.py`, `visualize_simulation.py` |
| [`sim512/`](sim512/README.md) | 14 MB | snimci simulacije 512², 6000 koraka (13 `.npy`, `diagnostics.csv`, `meta.txt`) | `prs_swe simulate` |
| [`vectorization/`](vectorization/README.md) | 17 KB | izvještaji GCC-a i Clang-a o vektorizaciji, sažetak disasemblera | **ručno** |

Format kolona `benchmarks.csv` i izlaznih fajlova opisan je u [korijenskom README-u, odjeljak 9](../README.md).

## Ko piše u ovaj folder

| Ko | Šta |
|---|---|
| `scripts/run_benchmarks.sh` | `system_info.txt`, `correctness_*`, `stream*`, `benchmarks*`, `benchmark_runs*`, `benchmark_*_output.txt`, `sim512/`, `figures/` i (drugačije od sačuvanih) `vectorization/compiler_report.txt` |
| `src/main.cpp` (podrazumijevane vrijednosti) | `results/benchmarks.csv`, `results/benchmark_runs.csv`, `results/sim` i `results/stream.csv` ako se `--out` ne zada; `stream` uz to bezuslovno pravi folder `results/` |
| ručno | `stream_l3.*`, `recheck_512_p1*`, `vectorization/*` (sačuvani izvještaji), `.bench_done` |

`scripts/demo.cmd` **ne piše ovdje** – njegov izlaz ide u `demo_out\`. Zato brisanje sadržaja `results/` demo ne obnavlja.

## Ko čita iz ovog foldera

| Ko | Šta čita |
|---|---|
| `scripts/plot_results.py` | `benchmarks.csv`; opciono `system_info.txt` (red `Core(s) per socket`), `stream.csv`, `stream_l3.csv` |
| `scripts/visualize_simulation.py` | cijeli folder `sim512/` |
| `scripts/docgen/*` | obavezno: `benchmarks.csv`, `system_info.txt`, `stream.csv`, `correctness_report.csv`, `figures/sim_summary.txt`, `vectorization/disassembly_summary.txt`, 9 PNG iz `figures/`; opciono: `stream_l3.csv`, `recheck_512_p1.csv`, `benchmarks_non_multiple8.csv` |
| `src/kernels_auto.cpp` | komentar upućuje na `vectorization/gcc_report_v1_initial.txt` |
| `README.md`, `docs/`, `tests/` | opisi i imena izlaznih fajlova (`tests` ne čita ništa: samo piše CSV čiju putanju zadaje pozivalac) |

## Šta se može ponovo generisati

| Podatak | Ponovo generisanje |
|---|---|
| `sim512/`, `figures/` | **lako**, i na Windowsu: `prs_swe simulate --size 512 --steps 6000 --variant G_omp_avx2 --threads 2 --snap 1500 --diag 100 --out results/sim512`, pa oba Python skripta (simulacija traje oko 3 s) |
| `benchmarks*`, `correctness*`, `stream.csv`, `system_info.txt` | `scripts/run_benchmarks.sh` na Linuxu/macOS-u; **vrijednosti će se razlikovati** (drugi računar, šum), pa to neće biti isti podaci kao u radu |
| `stream_l3.*`, `recheck_512_p1*`, `vectorization/*`, `.bench_done` | nijedna skripta ih ne pravi u sačuvanom obliku; `vectorization/gcc_report_v1_initial.txt` je izvještaj **ranije verzije koda** i ne može se ponoviti |

## Napomene

- Ovaj folder je za git **izvor istine za brojeve u radu**. Brisanje nije jednostavno povratno: generator dokumenta bez
  obaveznih fajlova prekida rad, a Linux VM mjerenja se ne mogu reproducirati na drugom računaru.
- Tekstualni dio (CSV i TXT, ≈ 250 KB) je mali i pogodan za verzionisanje; `figures/*.png` i `sim512/*.npy` (≈ 17 MB) su
  generisani artefakti.
- Mjerenja s 4 i 8 dretvi na ovom VM-u su mjerenja prekomjernog opterećenja (VM ima 2 jezgre), ne skaliranja.
