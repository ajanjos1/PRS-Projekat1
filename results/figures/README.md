# `results/figures/` – grafikoni i sažetak simulacije

Slike dobijene iz mjerenja (`perf_*`) i iz simulacije 512² (`sim_*`). Sve su **generisane** – nastaju Python skriptama iz
podataka u [`results/`](../README.md). PNG fajlovi nisu namijenjeni verzionisanju u gitu.

## Sadržaj

| Fajl | Veličina | Šta prikazuje | Skripta (ulaz) |
|---|---|---|---|
| `perf_speedup_variants.png` | 76 KB | ubrzanje jezgra naspram skalarne reference A za sve mreže | `plot_results.py` (`benchmarks.csv`) |
| `perf_throughput.png` | 56 KB | propusnost u Mcell/s po varijanti | `plot_results.py` |
| `perf_thread_scaling.png` | 192 KB | ubrzanje S(p) i efikasnost E(p) s brojem OpenMP dretvi; osjenčeno područje = više dretvi nego fizičkih jezgri | `plot_results.py` (+ `system_info.txt`) |
| `perf_bandwidth.png` | 83 KB | efektivna propusnost (model 112 B po ćeliji i koraku) naspram STREAM triad referentnih linija | `plot_results.py` (+ `stream.csv`, `stream_l3.csv`) |
| `perf_kernel_vs_e2e.png` | 69 KB | ubrzanje samog jezgra naspram cijele simulacije | `plot_results.py` |
| `perf_branch_simd_decomposition.png` | 71 KB | razlaganje A → A2 → B: efekat grananja naspram efekta SIMD-a | `plot_results.py` |
| `sim_terrain_before_after.png` | 688 KB | teren 512×512 prije i poslije 300 s simulirane erozije | `visualize_simulation.py` (`sim512/`) |
| `sim_terrain_difference.png` | 555 KB | razlika visine terena: crveno = erozija, plavo = depozicija | `visualize_simulation.py` |
| `sim_water_velocity.png` | 832 KB | dubina vode i brzina toka na kraju simulacije | `visualize_simulation.py` |
| `sim_water_evolution.png` | 212 KB | evolucija dubine vode kroz snimke `water_00*.npy` | `visualize_simulation.py` |
| `sim_mass_balance.png` | 101 KB | bilans vode i sedimenta i relativna promjena ukupne količine Σ(b + s) | `visualize_simulation.py` (`diagnostics.csv`) |
| `sim_summary.txt` | 0,4 KB | sažetak simulacije u tekstu (volumeni erozije/depozicije, raspon terena, dubine, brzine, drift Σ(b + s)) | `visualize_simulation.py` |

## Kako se ponovo prave

```bash
python3 scripts/visualize_simulation.py results/sim512 results/figures
python3 scripts/plot_results.py results results/figures
```

(Windows: `py -3 …`; zahtijeva `matplotlib`, a `visualize_simulation.py` i `numpy`.) `demo.cmd` pravi analogne slike u
`demo_out\figures\`, a ovaj folder ne dira.

## Ko referencira ovaj folder

| Ko | Kako |
|---|---|
| `scripts/docgen/build_document.js` | ugrađuje **9 slika**: sve `perf_*` osim `perf_throughput.png` i sve `sim_*` osim `sim_water_evolution.png` |
| `scripts/docgen/chapters_c.js` | **obavezno** čita `sim_summary.txt` za brojke iz poglavlja „Rezultati simulacije" |
| `scripts/run_benchmarks.sh` | `mkdir -p results/figures` i poziva oba Python skripta (zadani izlaz je upravo ovaj folder) |
| `README.md` (korijen) | odjeljak 8.7 |

## Napomene

- `perf_throughput.png` i `sim_water_evolution.png` generišu se, ali ih generator dokumenta ne koristi.
- Natpisi i boje su fiksirani u skriptama (validirana kategorijalna paleta), pa su slike iz različitih pokretanja uporedive.
- Slike prikazuju mjerenja s Linux VM-a (2 jezgre), pa dretve 4 i 8 označavaju prekomjerno opterećenje.
