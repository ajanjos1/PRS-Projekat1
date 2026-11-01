# `src/` – implementacija

Izvorni kod aplikacije: kerneli (isti račun u više oblika), drajveri koji ih pokreću (serijski i OpenMP), registar
varijanti, stanje simulacije, generisanje terena, mjerni modul i komandna linija `prs_swe`. Deklaracije su u
[`include/`](../include/README.md). Opšti opis projekta: [korijenski README](../README.md).

## Sadržaj

| Fajl | Linija | Uloga | Varijante |
|---|---|---|---|
| `main.cpp` | 523 | Komandna linija: `info`, `bench`, `simulate`, `stream`; parsiranje argumenata, CSV/`.npy` izlaz | – |
| `variants.cpp` | 59 | Registar varijanti (`main_variants`, `ablation_variants`), `variant_supported`, `run_steps` (bira drajver) | sve |
| `driver_serial.cpp` | 32 | Petlja po koracima, jedna dretva (tri petlje po `y` + zamjena bafera terena) | A, B, C, D, A2 |
| `driver_openmp.cpp` | 117 | `run_omp_parallel_for` (fork-join po fazi) i `run_omp_region` (jedna paralelna regija); `openmp_available`, `openmp_max_threads` | E, F, G, H, E2, F2 |
| `kernels_scalar.cpp` | 45 | Skalarna referenca; vektorizacija isključena (zastavice + `SW_NO_VECTORIZE`) | A, E, E2 |
| `kernels_branchless.inl` | 140 | Zajedničko „branchless" tijelo kernela (`__restrict`, bez `if`); **nema include guard**, uključuje se unutar anonimnog `namespace`-a | – |
| `kernels_auto.cpp` | 37 | Uključuje `.inl` s kompajlerom koji **vektorizuje** petlje | B, H |
| `kernels_scalar_branchless.cpp` | 28 | Isti `.inl`, ali sa **isključenom** vektorizacijom (ablacija grananja) | A2 |
| `kernels_omp_simd.cpp` | 181 | Petlje označene `#pragma omp simd` (`SW_OMP_SIMD`) | C, F, F2 |
| `kernels_avx2.cpp` | 207 | AVX2 intrinsics, 8 × `float`; skalarni ostatak reda; fallback na `kernels_auto` bez AVX2 builda | D, G |
| `simulation.cpp` | 267 | `Simulation` (alokacija, halo, kopiranje), CFL i `make_step_constants`, `compute_totals`, `state_checksum`, `compare_states`, `extract`, `velocity_magnitude` | – |
| `terrain.cpp` | 92 | `init_terrain`: deterministički fBm teren (double, seed 20251) | – |
| `cpu_features.cpp` | 123 | CPUID/XGETBV provjera AVX2, naziv procesora, `enable_flush_denormals` (FTZ/DAZ), `build_description` | – |
| `benchmark.cpp` | 41 | `bench::summarize` (medijan, min, max, srednja, devijacija), `bench::consume` | – |
| `io_npy.cpp` | 33 | `save_npy` – NumPy `.npy` v1.0 | – |

## Kako su fajlovi povezani

```
main.cpp ─► variants.cpp : run_steps(sim, c, steps, KernelSet, Driver, threads)
   │            ├─ Driver::Serial         ─► driver_serial.cpp
   │            ├─ Driver::OmpParallelFor ─► driver_openmp.cpp ─┐
   │            └─ Driver::OmpRegion      ─► driver_openmp.cpp ─┤  svaki poziva po REDU, preko pokazivača iz KernelSet:
   │                                                            ▼  flux(F,c,b,y) → water(F,c,b,b_next,y) → sediment(F,c,y)
   │                                       kernels_scalar / _auto / _omp_simd / _avx2 / _scalar_branchless
   │                                       (kernels_scalar i kernels_avx2 pozivaju ref::*_cell iz include/cell_ops.hpp)
   ├─► simulation.cpp, terrain.cpp          stanje, CFL, dijagnostika, početni teren
   ├─► benchmark.cpp                        statistika i zaštita od eliminacije koda
   ├─► io_npy.cpp                           snimci simulacije za Python
   └─► cpu_features.cpp                     AVX2 provjera, FTZ/DAZ, opis builda
```

**Varijanta = `KernelSet` (kako se računa red) + `Driver` (ko računa koje redove).** Indirektni poziv se plaća jednom po
redu, a ne po ćeliji.

## Gradnja (`CMakeLists.txt`)

- 13 `.cpp` fajlova (sve osim `main.cpp`) čine statičku biblioteku **`prs_core`**; `main.cpp` je izvršni program **`prs_swe`**.
- `kernels_branchless.inl` se **ne navodi** u CMake-u – uključuje ga `#include` u `kernels_auto.cpp` i
  `kernels_scalar_branchless.cpp`.
- Zastavice se postavljaju **po fajlu** (isti izvor se namjerno prevodi na različite načine):

| Fajl | Posebne zastavice (GCC/Clang) |
|---|---|
| `kernels_scalar.cpp` | `-fno-tree-vectorize -fno-tree-loop-vectorize -fno-tree-slp-vectorize` (Clang: `-fno-vectorize -fno-slp-vectorize`); bez `-mavx2` |
| `kernels_scalar_branchless.cpp` | isto kao gore, plus `-fno-trapping-math`, `-mavx2` |
| `kernels_auto.cpp`, `kernels_omp_simd.cpp` | `-fno-trapping-math`, `-mavx2` |
| `kernels_avx2.cpp` | `-mavx2` (MSVC: `/arch:AVX2`) |
| svi | `-ffp-contract=off -fno-math-errno -Wall -Wextra -Wpedantic` (MSVC: `/W4 /EHsc /fp:precise /utf-8`) |

Detaljno objašnjenje svake zastavice: [korijenski README, odjeljci 5 i 7](../README.md).

## Ko referencira ovaj folder

| Ko | Kako |
|---|---|
| `CMakeLists.txt` | eksplicitne putanje: `src/kernels_scalar.cpp`, `kernels_auto.cpp`, `kernels_omp_simd.cpp`, `kernels_scalar_branchless.cpp`, `kernels_avx2.cpp`, `cpu_features.cpp`, `simulation.cpp`, `terrain.cpp`, `variants.cpp`, `driver_serial.cpp`, `driver_openmp.cpp`, `benchmark.cpp`, `io_npy.cpp` (u `prs_core`) i `src/main.cpp` (u `prs_swe`); `src/kernels_omp_simd.cpp` i u `-fopenmp-simd` slučaju |
| `scripts/run_benchmarks.sh` | `touch src/kernels_*.cpp` – prisiljava ponovno prevođenje radi izvještaja o vektorizaciji |
| `results/vectorization/*` | izvještaji kompajlera navode `src/kernels_branchless.inl:LINIJA`, `src/kernels_omp_simd.cpp:LINIJA`, `src/kernels_auto.cpp:LINIJA` |
| `scripts/docgen/chapters_b.js` | tabela vektorizacije po fajlovima (`kernels_scalar.cpp`, `kernels_auto.cpp`, `kernels_omp_simd.cpp`) i opis arhitekture |
| `README.md` | odjeljci 4, 5, 7, 13 |
| komentari u kodu | `kernels_auto.cpp` → `results/vectorization/gcc_report_v1_initial.txt`; `main.cpp` → podrazumijevani izlazi u `results/` |

Testovi (`tests/`) ne uključuju fajlove iz `src/` direktno, nego se linkuju s bibliotekom `prs_core`.

## Napomene

- **Uzorak za novu varijantu:** novi `KernelSet` (tri funkcije po redu) + unos u `variants.cpp`; testovi i `bench` je
  automatski uključuju jer rade nad `main_variants()` / `ablation_variants()`.
- `main.cpp` upisuje podrazumijevane izlaze u `results/` (relativno prema **tekućem folderu**, ne prema lokaciji
  izvršnog fajla). `stream` uz to bezuslovno pravi folder `results/`.
- Komentari u kodu su pisani na bosanskom i sadrže dijakritike; MSVC zato zahtijeva `/utf-8`.
