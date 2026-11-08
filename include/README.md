# `include/` – zaglavlja

Opšti opis projekta: [korijenski README](../README.md); objašnjenje modela: [docs/ALGORITHM_EXPLANATION.md](../docs/ALGORITHM_EXPLANATION.md).

## Faza 2: simulator erozije (`prs_erosion`, `namespace prs`)

| Fajl | Linija | Sloj | Šta sadrži |
|---|---|---|---|
| `Grid2D.hpp` | 79 | podaci | `Grid2D<T>`: 2D polje u jednom kontinuiranom nizu (row-major, `i = y·W + x`), `swap` za double buffering |
| `Heightmap.hpp` | 52 | model terena | `TerrainScale` (veličina ćelije, min/max visina, podrazumijevane vrijednosti), `Heightmap` (apstraktni teren koji prima solver), `makeHeightmap` |
| `Rainfall.hpp` | 38 | kiša | `RainfallEvent`: L/m² → m, intenzitet, `depthAddedBetween(t0, t1)`; `validateRainfallInput` |
| `SimulationConfig.hpp` | 81 | parametri | `BoundaryCondition`, `WaterFlowParameters` (g, Manning n, CFL, rub, gubici), `ErosionParameters` (Kc, Ks, Kd, ...), `SimulationConfig` |
| `SimulationState.hpp` | 55 | stanje | sva polja po ćelijama (SoA): b, d, s, protoci, brzine, drugi baferi, dijagnostika |
| `VirtualPipesSolver.hpp` | 78 | numerika | tok vode: kiša, protoci kroz virtuelne cijevi + trenje + ograničenje K, dubina i brzina, gubici, stabilan Δt |
| `HydraulicErosion.hpp` | 69 | numerika | kapacitet, erozija/depozicija, transport sedimenta kroz cijevi, taloženje na kraju |
| `ErosionSimulation.hpp` | 134 | numerika | vremenska petlja (adaptivni Δt, faze kiša/oticanje), statistike i bilansi, povratna funkcija za napredak |
| `MathUtils.hpp` | 28 | numerika | `cubeRootPositive` (brzi kubni korijen za Manningovo trenje) |
| `HeightmapIO.hpp` | 75 | ulaz/izlaz | učitavanje PNG/JPEG (stb_image), pretvaranje u sivo, 16-bitni izvoz u istoj visinskoj skali, izvještaj o odsijecanju |
| `ImageWriter.hpp` | 31 | ulaz/izlaz | PNG koder (8/16 bita, 1–4 kanala), JPEG zapis, UTF-8 putanje |
| `ResultExport.hpp` | 56 | ulaz/izlaz | izvoz svih rezultata (PNG, `.npy`, CSV, TXT/JSON izvještaj), vizualizacije |
| `ConsoleInput.hpp` | 51 | konzola | parsiranje brojeva (decimalni zarez, odbijanje NaN/Inf/teksta), pitanja s podrazumijevanim vrijednostima |

Zavisnosti: numerički sloj (`VirtualPipesSolver`, `HydraulicErosion`, `ErosionSimulation`) uključuje samo
`Grid2D`, `Heightmap`, `Rainfall`, `SimulationConfig`, `SimulationState` i `MathUtils` – **ne zavisi** od
konzole ni od biblioteke za slike. `HeightmapIO`/`ImageWriter`/`ResultExport` i `ConsoleInput` su odvojeni slojevi.
Biblioteka stb je u `third_party/stb/` i uključuje se samo u `src/stb_image_impl.cpp`, `src/HeightmapIO.cpp`
(deklaracije) i `src/ImageWriter.cpp`.
