# `output/` – rezultati simulacija

Podrazumijevani izlazni folder programa `prs_erosion` (relativno prema **tekućem** folderu – pokrećite program
iz korijena projekta). Za ulaz `planina.png` program pravi `output/planina/`:

| Datoteka | Sadržaj |
|---|---|
| `planina_eroded.png` | **erodirana height mapa**: 16-bitni sivi PNG, iste dimenzije, ista visinska skala kao ulaz |
| `planina_original.png` | ulaz u istom formatu (16 bita, ista skala) – za direktno poređenje |
| `planina_difference.png` | Δh = poslije − prije: crveno = sniženje (erozija), plavo = povišenje (depozicija) |
| `planina_comparison.png` | sjenčeni reljef: original (lijevo) i rezultat (desno), isti z-faktor |
| `planina_water_max_depth.png` | najveća dubina vode tokom simulacije (gdje su tekli tokovi) |
| `planina_*_m.npy` | sirovi podaci u metrima (float64, bez gubitka): original, eroded, difference, water_max_depth |
| `planina_eroded_m.csv` | samo uz `--csv` |
| `simulation_report.txt` / `.json` | parametri, bilansi vode i materijala, statistika, upozorenja |

Sadržaj ovog foldera se ne verzioniše u gitu (osim ovog README-a).
