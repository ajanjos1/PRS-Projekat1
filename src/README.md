# `src/` – implementacija

Opšti opis projekta: [korijenski README](../README.md); objašnjenje modela: [docs/ALGORITHM_EXPLANATION.md](../docs/ALGORITHM_EXPLANATION.md).

## Faza 2: simulator erozije (`prs_erosion`)

| Fajl | Linija | Uloga |
|---|---|---|
| `main.cpp` | 626 | Konzolni interfejs: meni `[1] Load heightmap [2] Exit`, pitanja (skala, kiša, oticanje, rub), sažetak, napredak, rezultati; komandna linija (`--input --rain --hours ...`). Nema numerike. |
| `ErosionSimulation.cpp` | 268 | Vremenska petlja: izbor Δt, redoslijed faza u koraku, faze kiša/oticanje, bilansi i završne statistike |
| `VirtualPipesSolver.cpp` | 233 | Kiša, protoci kroz virtuelne cijevi (Mei 2007) s Manningovim trenjem, ograničenje K, nova dubina i brzina, gubici, CFL |
| `HydraulicErosion.cpp` | 177 | Kapacitet transporta, erozija/depozicija (double buffer terena), konzervativni transport sedimenta (double buffer), taloženje |
| `Rainfall.cpp` | 49 | Preklop koraka s periodom kiše, validacija unosa |
| `SimulationConfig.cpp` | 56 | Validacija parametara |
| `Heightmap.cpp` | 46 | Validacija skale, normalizovano → metri (i inverzija) |
| `HeightmapIO.cpp` | 206 | Čitanje datoteke (UTF-8 putanje), prepoznavanje formata iz sadržaja, stb_image dekodiranje, sivo, 16-bitni izvoz |
| `ImageWriter.cpp` | 229 | Vlastiti PNG koder (8/16 bita, filteri, CRC32) uz zlib iz stb_image_write; JPEG zapis |
| `stb_image_impl.cpp` | 25 | Jedina implementacija stb_image (samo PNG i JPEG, bez stdio) |
| `ResultExport.cpp` | 625 | Izvoz: 16-bitne height mape, mapa razlike, hillshade poređenje, mapa dubine vode, `.npy`, CSV, TXT/JSON izvještaj |
| `ConsoleInput.cpp` | 153 | Parsiranje i validacija konzolnog unosa (testabilno preko `std::istream`) |

Gradnja: svi fajlovi faze 2 osim `main.cpp` čine statičku biblioteku **`prs_erosion_core`**; `main.cpp` je
program **`prs_erosion`**. Zastavice su obične (`-Wall -Wextra -Wpedantic`, MSVC `/W4 /utf-8 /permissive-`) –
**bez** `-fopenmp`, `-mavx2`, `/arch:AVX2`, `/openmp` (serijska faza, zahtjev AC7).

Redoslijed jednog koraka (`ErosionSimulation::step`):

```
addUniformWater ─► computeOutflowFlux ─► updateWaterAndVelocity ─► erodeAndDeposit ─► transportSediment ─► applyLosses
   (kiša)            (cijevi, K)            (d, u, v, CFL max)        (b → b_next)        (s → s_next)       (opciono)
```
