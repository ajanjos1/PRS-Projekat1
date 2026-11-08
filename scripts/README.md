# `scripts/` – pokretanje, eksperimenti i slike

Opšti opis projekta: [korijenski README](../README.md).

## Faza 2: simulator erozije

| Fajl | Platforma | Uloga | Piše u |
|---|---|---|---|
| `erosion_demo.cmd` | Windows | Omotač za `erosion_demo.ps1` (zaobilazi execution policy samo za to pokretanje) | – |
| `erosion_demo.ps1` | Windows (PowerShell 5.1) | Build (Release) → `prs_erosion_tests` → `prs_erosion` na primjerima (`-AllMaps` za sva tri) → opciono slike | `output\demo_*` |
| `run_experiments.py` | bilo koja (Python 3, samo standardna biblioteka) | Testovi 4–6 na stvarnim mapama preko komandne linije programa; tabele iz `simulation_report.json` | `output/experiments/` |
| `visualize_results.py` | bilo koja (numpy, matplotlib) | "Original vs Eroded" s istom skalom boja + Δh u metrima + max dubina vode, iz `.npy` izlaza | zadana slika ili prozor |

```bat
scripts\erosion_demo.cmd                       :: primjer_1, 100 L/m2 za 1 h
scripts\erosion_demo.cmd -AllMaps -Rain 50 -Hours 2
```

```bash
python3 scripts/run_experiments.py --exe build/prs_erosion            # sve (~10 min serijski)
python3 scripts/run_experiments.py --exe build/prs_erosion --quick    # kraće
python3 scripts/visualize_results.py output/<ime_mape> --save slika.png
```

