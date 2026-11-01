# `results/vectorization/` – izvještaji o vektorizaciji

Dokaz da svaki kernel radi ono što varijanta tvrdi: A i A2 **nisu** vektorizovani, a B, C (i D) jesu. Nadređeni folder:
[`results/`](../README.md). Ove datoteke su dobijene **ručno** (izdvajanjem iz izvještaja kompajlera i disasemblera), a ne
skriptom.

## Sadržaj

| Fajl | Linija | Šta prikazuje |
|---|---|---|
| `gcc_report_v1_initial.txt` | 12 | GCC 13.3 (`-fopt-info-vec-optimized -fopt-info-vec-missed`) za **prvu verziju** koda. `kernels_auto.cpp:44` i `:94` (faze 1 i 2) i `kernels_omp_simd.cpp:46` i `:97`: *„not vectorized: control flow in loop"*. `kernels_auto.cpp:152` (faza 3) vektorizovana, ali uz *„loop versioned for vectorization because of possible aliasing"*, tj. s runtime provjerom preklapanja pokazivača |
| `gcc_report_final.txt` | 9 | GCC 13.3 za **konačnu verziju**: sve tri petlje u `kernels_branchless.inl` (`:17`, `:64`, `:121`) vektorizovane sa 32 B i 16 B vektorima; `kernels_omp_simd.cpp` (`:51`, `:102`, `:161`) vektorizovan sa 32 B vektorima; nema „missed" i nema provjere preklapanja |
| `clang18_report.txt` | 12 | Clang 18 (`-Rpass=loop-vectorize`): sve tri petlje `kernels_branchless.inl` i `kernels_omp_simd.cpp` vektorizovane (širina 8, *interleave* 1). `kernels_scalar.cpp` (`:19`, `:27`, `:35`) i `kernels_branchless.inl` u prevodnom fajlu A2: *„loop not vectorized"* (namjerno) |
| `disassembly_summary.txt` | 6 | Broj instrukcija po objektnom fajlu (vidi tabelu ispod) |

### `disassembly_summary.txt`

| Objektni fajl | `ymm` | pakovane (`…ps`) | skalarne (`…ss`) | `vsqrtps` | `vdivps` |
|---|---|---|---|---|---|
| `kernels_scalar` | 0 | 0 | 104 | 0 | 0 |
| `kernels_scalar_branchless` | 0 | 0 | 199 | 0 | 0 |
| `kernels_auto` | 184 | 202 | 292 | 4 | 8 |
| `kernels_omp_simd` | 185 | 101 | 102 | 2 | 4 |
| `kernels_avx2` | 198 | 137 | 142 | 2 | 4 |

Zaključci: A i A2 su **potpuno skalarni** (nula `ymm` i pakovanih instrukcija) – preduslov za pošteno poređenje; B, C i D
koriste 256-bitne registre; `vsqrtps` / `vdivps` pokazuju da su dijeljenje i korijen zaista vektorizovani.

## Ko referencira ovaj folder

| Ko | Kako |
|---|---|
| `scripts/docgen/build_document.js` | **obavezno** čita `disassembly_summary.txt` (tabela „ymm / pakovane / skalarne") |
| `scripts/docgen/chapters_b.js` | sažeta tabela GCC izvještaja; natpis upućuje na ovaj folder za potpune izvještaje |
| `src/kernels_auto.cpp` | komentar: *„vidi `results/vectorization/gcc_report_v1_initial.txt`"* (razlog zašto se `__restrict` prosljeđuje kao parametri) |
| `scripts/run_benchmarks.sh` | pravi ovaj folder (`mkdir -p results/vectorization`) i piše `compiler_report.txt` – **fajl kojeg nema među sačuvanim** |
| `README.md` (korijen) | odjeljci 5.1–5.2 |

## Napomene

- Brojevi linija (`:17`, `:64`, …) odnose se na verzije izvora **u trenutku izvještaja**; `gcc_report_v1_initial.txt` opisuje
  raniju strukturu koda i ne može se reproducirati trenutnim izvorom.
- Za nov izvještaj: `cmake -S . -B build-vecreport -DCMAKE_BUILD_TYPE=Release -DPRS_VECTORIZATION_REPORTS=ON`, pa
  `cmake --build build-vecreport --target prs_core` (`/Qvec-report:2` na MSVC-u). GCC piše izvještaj na stderr.
