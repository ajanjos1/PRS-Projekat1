# `include/` – zaglavni fajlovi biblioteke `prs_core`

Zaglavlja koja definišu **model i stanje simulacije, skalarnu specifikaciju ćelije, poravnatu memoriju, mjerni modul,
detekciju CPU-a i NumPy izlaz**. Sve je u `namespace sw` (`sw::ref` za specifikaciju ćelije, `sw::bench` za mjerni
modul). Implementacije deklarisanih funkcija su u [`src/`](../src/README.md). Opšti opis projekta: [korijenski README](../README.md).

## Sadržaj

| Fajl | Linija | Šta sadrži |
|---|---|---|
| `simulation.hpp` | 239 | **Glavno zaglavlje.** Konstante rasporeda (`kPadLeft = 8`, `kSimdFloats = 8`), `SimParams` (parametri modela), `StepConstants` (izvedene konstante koje koriste svi kerneli), `Fields` (nevlasnički pogled na nizove), klasa `Simulation` (vlasnik memorije, dvostruki bafer terena), tipovi funkcija `FluxRowFn` / `WaterRowFn` / `SedimentRowFn`, `KernelSet`, `Driver`, `Variant`, `TerrainSpec`, `Totals`, `StateDiff` i deklaracije svih kernela, drajvera i dijagnostike |
| `cell_ops.hpp` | 143 | **Skalarna specifikacija** jedne ćelije: `ref::flux_cell`, `ref::water_cell`, `ref::sediment_cell` (inline, header-only). Svi ostali kerneli moraju izvršiti iste operacije istim redoslijedom |
| `aligned_buffer.hpp` | 93 | `AlignedBuffer`: niz `float` poravnat na 64 B (`aligned_alloc` / `_aligned_malloc`), popunjen nulama, move-only, `copy_from` |
| `sw_common.hpp` | 36 | Makroi: `SW_RESTRICT`, `SW_NOINLINE`, `SW_NO_VECTORIZE` (po kompajleru), `SW_OMP_SIMD` |
| `benchmark.hpp` | 62 | `sw::bench::Stats`, šablon `measure(warmups, repeats, prepare, run, observe)`, deklaracije `summarize` i `consume` |
| `cpu_features.hpp` | 15 | `avx2_supported()` (CPUID + XGETBV), `cpu_brand()`, `build_description()` |
| `io_npy.hpp` | 10 | `save_npy(path, data, rows, cols)` – zapis NumPy `.npy` v1.0, `float32` |
| `build_info.hpp.in` | 7 | **Šablon**, ne pravo zaglavlje: CMake ga pretvara u `<build>/generated/build_info.hpp` s makroima `PRS_BUILD_COMPILER`, `PRS_BUILD_TYPE`, `PRS_BUILD_CXX_FLAGS`, `PRS_BUILD_SCALAR_FLAGS`, `PRS_BUILD_VECTOR_FLAGS` |

## Međusobne zavisnosti zaglavlja

```
sw_common.hpp ───┐
                 ├──► simulation.hpp ───► cell_ops.hpp
aligned_buffer.hpp ┘

benchmark.hpp, cpu_features.hpp, io_npy.hpp      (samostalna, bez lokalnih #include)
build_info.hpp.in ──(CMake configure_file)──► <build>/generated/build_info.hpp
```

## Deklarisano ovdje → definisano u

| Deklaracija | Definicija |
|---|---|
| `cfl_dt_limit`, `make_step_constants`, `Simulation` (konstruktor, `fields`, `refresh_halo`, `copy_state_from`, `clear_dynamic_state`), `compute_totals`, `state_checksum`, `compare_states`, `StateDiff::max_abs`, `extract`, `velocity_magnitude` | `src/simulation.cpp` |
| `init_terrain` | `src/terrain.cpp` |
| `kernels_scalar` / `kernels_scalar_branchless` / `kernels_auto` / `kernels_omp_simd` / `kernels_avx2` | `src/kernels_scalar.cpp` / `kernels_scalar_branchless.cpp` / `kernels_auto.cpp` / `kernels_omp_simd.cpp` / `kernels_avx2.cpp` |
| `run_serial` | `src/driver_serial.cpp` |
| `run_omp_parallel_for`, `run_omp_region`, `openmp_available`, `openmp_max_threads` | `src/driver_openmp.cpp` |
| `main_variants`, `ablation_variants`, `variant_supported`, `run_steps` | `src/variants.cpp` |
| `enable_flush_denormals` (deklarisano u `simulation.hpp`) | `src/cpu_features.cpp` |
| `avx2_supported`, `cpu_brand`, `build_description` | `src/cpu_features.cpp` |
| `bench::summarize`, `bench::consume` | `src/benchmark.cpp` |
| `save_npy` | `src/io_npy.cpp` |
| `ref::*`, `AlignedBuffer`, `bench::measure` | header-only (inline / šablon) |

## Ko referencira ovaj folder

**Putanja za prevođenje.** `CMakeLists.txt`: `target_include_directories(prs_core PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/generated)`.
Zbog `PUBLIC` je nasljeđuju `prs_swe` i `prs_tests`, pa se zaglavlja uključuju samo imenom (`#include "simulation.hpp"`).
Isti fajl poziva `configure_file(include/build_info.hpp.in …)`.

**Ko uključuje koje zaglavlje** (provjereno po `#include "…"`):

| Zaglavlje | Uključuju ga |
|---|---|
| `simulation.hpp` | `cell_ops.hpp`; `src/`: `cpu_features`, `driver_openmp`, `driver_serial`, `kernels_auto`, `kernels_avx2`, `kernels_omp_simd`, `kernels_scalar`, `kernels_scalar_branchless`, `main`, `simulation`, `terrain`, `variants`; `tests/correctness_tests.cpp` |
| `cell_ops.hpp` | `src/kernels_scalar.cpp`, `src/kernels_avx2.cpp` (skalarni ostatak reda) |
| `aligned_buffer.hpp` | `simulation.hpp` (pa posredno svi koji uključuju `simulation.hpp`; `main.cpp` ga koristi za STREAM nizove) |
| `sw_common.hpp` | `simulation.hpp` (zato su makroi dostupni u svim kernelima), `src/benchmark.cpp` |
| `benchmark.hpp` | `src/benchmark.cpp`, `src/main.cpp` |
| `cpu_features.hpp` | `src/cpu_features.cpp`, `src/variants.cpp`, `src/main.cpp`, `tests/correctness_tests.cpp` |
| `io_npy.hpp` | `src/io_npy.cpp`, `src/main.cpp` |
| `build_info.hpp` (generisano iz `.in`) | `src/cpu_features.cpp` |

Pored koda, folder se pominje u [korijenskom README-u](../README.md) (odjeljci 2, 3, 5, 13) i u tekstu priloga generatora
dokumenta (`scripts/docgen/build_document.js`).

## Napomene

- `build_info.hpp` **ne postoji u ovom folderu**: nastaje u build folderu (`build/generated/`, `build-vs/generated/`, …).
  Zato se `.in` fajl ne smije brisati, a generisani se ne smije ručno mijenjati.
- Promjena `simulation.hpp`, `cell_ops.hpp`, `aligned_buffer.hpp` ili `sw_common.hpp` ponovo prevodi **gotovo cijeli**
  projekat, jer ih uključuje skoro svaki prevodni fajl.
- `cell_ops.hpp` je specifikacija modela: izmjena formule ovdje mora biti praćena istom izmjenom u
  `kernels_branchless.inl`, `kernels_omp_simd.cpp` i `kernels_avx2.cpp`, inače testovi bit-identičnosti padaju.
