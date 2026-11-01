# `scripts/` – pokretanje, mjerenje, grafikoni i generisanje dokumenta

Skripte koje ne spadaju u C++ aplikaciju: demonstracija na Windowsu, kompletan eksperimentalni postupak na
Linuxu/macOS-u, evidencija okruženja, grafikoni (Python) i generator Word dokumenta (Node.js, podfolder
[`docgen/`](docgen/README.md)). Opšti opis projekta: [korijenski README](../README.md).

## Sadržaj

| Fajl | Platforma | Uloga | Piše u |
|---|---|---|---|
| `demo.cmd` | Windows | Omotač: `powershell -NoProfile -ExecutionPolicy Bypass -File demo.ps1 %*` (zaobilazi execution policy samo za to pokretanje) | – |
| `demo.ps1` | Windows (PowerShell 5.1) | Build → `info` → testovi → `bench` → `stream` → `simulate` → grafikoni | `demo_out\` |
| `run_benchmarks.sh` | Linux/macOS (bash) | Kompletan postupak: build, `system_info`, testovi, izvještaji kompajlera, `stream`, `bench`, `simulate`, grafikoni | `results/` |
| `system_info.sh` | Linux/macOS | Evidencija okruženja (CPU, jezgre, RAM, OS, kompajler, SIMD zastavice, OpenMP, `prs_swe info`) | stdout |
| `plot_results.py` | bilo koja | 6 grafikona performansi iz CSV-ova | zadani folder za slike |
| `visualize_simulation.py` | bilo koja | slike i sažetak simulacije iz `.npy` snimaka | zadani folder za slike |
| [`docgen/`](docgen/README.md) | bilo koja (Node.js) | generiše Word dokument rada iz `results/` | `docs/*.docx` |

## Tok izvršavanja

```
scripts\demo.cmd ─► demo.ps1 ─► cmake (build-vs, Release) ─► prs_swe info / prs_tests / bench / stream / simulate
                           └──► plot_results.py + visualize_simulation.py ───────────────────────────► demo_out\

run_benchmarks.sh ─► cmake (build/) ─► system_info.sh ─► prs_tests ─► (build-vecreport: izvještaji) ─► stream, bench
                  └─► simulate ─► visualize_simulation.py + plot_results.py ───────────────────────► results/
```

## Ugovori o podacima (šta koja skripta čita i piše)

### `plot_results.py [REZULTATI_DIR] [IZLAZ_DIR]` (zadano `results`, `results/figures`)

| Ulaz | Obavezno | Napomena |
|---|---|---|
| `benchmarks.csv` | da | kolone iz `bench` izlaza |
| `system_info.txt` | ne | traži red `Core(s) per socket:` (broj fizičkih jezgri; zadano 2); `demo.ps1` ga pravi sam jer na Windowsu nema `lscpu` |
| `stream.csv`, `stream_l3.csv` | ne | referentne linije propusnosti; oznake su „RAM (384 MB)" i „keš (36 MB)" (3 niza × 128 MB, odnosno 12 MB) |

Izlaz (6 PNG): `perf_speedup_variants`, `perf_throughput`, `perf_thread_scaling`, `perf_bandwidth`, `perf_kernel_vs_e2e`,
`perf_branch_simd_decomposition`. Zavisnost: `matplotlib` (ne uvozi `numpy`).

### `visualize_simulation.py [SIM_DIR] [IZLAZ_DIR]` (zadano `results/sim512`, `results/figures`)

Ulaz (izlaz `prs_swe simulate`): `terrain_initial.npy`, `terrain_final.npy`, `water_final.npy`, `sediment_final.npy`,
`speed_final.npy`, `water_0*.npy` (snimci), `diagnostics.csv`, `meta.txt`.
Izlaz: `sim_terrain_before_after.png`, `sim_terrain_difference.png`, `sim_water_velocity.png`, `sim_water_evolution.png`,
`sim_mass_balance.png` i `sim_summary.txt`. Zavisnosti: `numpy`, `matplotlib`.

### `demo.ps1`

Opcije `-Full`, `-SkipBuild`, `-SkipTests`, `-NoOpen`. Bira broj dretvi prema broju **fizičkih** jezgri, traži Python koji ima
`numpy` i `matplotlib` (py launcher, `%LOCALAPPDATA%\Programs\Python`, `PATH`; ignoriše Microsoft Store stub) i postavlja
`PYTHONUTF8=1`. Tekst u skripti je namjerno ASCII (PowerShell 5.1 pogrešno čita UTF-8 bez BOM-a).
Generator Visual Studia bira automatski: „Visual Studio 18 2026", inače „Visual Studio 17 2022".

### `run_benchmarks.sh`

`set -euo pipefail`, radi iz korijena projekta (`cd "$(dirname "$0")/.."`). Poziva `system_info.sh` i oba Python
skripta, pa zahtijeva Linux alate (`lscpu`, `free`, `/proc/cpuinfo`) i `python3`.

## Ko referencira ovaj folder

| Ko | Kako |
|---|---|
| `README.md` | odjeljci 1, 8, 13 (pokretanje, opcije, struktura) |
| `scripts/demo.cmd` → `demo.ps1`; `demo.ps1` → `plot_results.py`, `visualize_simulation.py` | lanac poziva |
| `run_benchmarks.sh` → `system_info.sh`, `plot_results.py`, `visualize_simulation.py` | lanac poziva |
| `scripts/docgen/build_document.js` | u tekstu priloga navodi `scripts/` i komande iz `run_benchmarks.sh` |
| `src/main.cpp`, `results/*` | podrazumijevani izlazni folderi poklapaju se s onima koje skripte očekuju |

## Napomene

- **Dva izlazna foldera:** `demo.ps1` → `demo_out\`, `run_benchmarks.sh` → `results/`. Demo namjerno ne dira `results/`.
- `run_benchmarks.sh` piše `results/vectorization/compiler_report.txt`, a sačuvani izvještaji u tom folderu dobijeni su ručno.
- Izlazne `.txt` datoteke koje `demo.ps1` pravi preko `Tee-Object` u PowerShellu 5.1 mogu biti UTF-16 (ne čitaju se običnim `grep`-om).
- `demo.ps1`, korak 6: ispis kaže „30 s simuliranog vremena", ali 6000 koraka × 0,05 s je **300 s**.
