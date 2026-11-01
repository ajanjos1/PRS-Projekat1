# `results/sim512/` – izlaz jedne simulacije 512 × 512

Snimci i dijagnostika koje piše `prs_swe simulate`. Nadređeni folder: [`results/`](../README.md).

## Kako je nastao

```bash
prs_swe simulate --size 512 --steps 6000 --variant G_omp_avx2 --threads 2 --snap 1500 --diag 100 --out results/sim512
```

Sadržaj `meta.txt`: `W=512`, `H=512`, `steps=6000`, `dt=0.05` (⇒ 300 s simuliranog vremena), `cell_size=1`, `variant=G_omp_avx2`,
`threads=2`, `wall_time_s=3.31833`, `rain_rate=0.002`, `evaporation=0.01`, `Kc=0.6`, `Ks=0.6`, `Kd=0.6`, `min_tilt=0.05`,
`erosion_depth=0.1`.

## Sadržaj

| Fajl | Veličina | Šta je |
|---|---|---|
| `terrain_initial.npy` | 1 MB | početni teren (korak 0) |
| `water_001500.npy`, `water_003000.npy`, `water_004500.npy`, `water_006000.npy` | 4 × 1 MB | dubina vode na snimcima svakih 1500 koraka |
| `terrain_001500.npy`, `terrain_003000.npy`, `terrain_004500.npy`, `terrain_006000.npy` | 4 × 1 MB | teren na istim koracima |
| `terrain_final.npy`, `water_final.npy`, `sediment_final.npy` | 3 × 1 MB | završno stanje (korak 6000) |
| `speed_final.npy` | 1 MB | brzina toka na kraju (iz protoka i trenutne dubine) |
| `diagnostics.csv` | 6,5 KB (61 red + zaglavlje) | po jedan red na svakih 100 koraka: `step,time_s,water_m3,terrain_m3,sediment_m3,terrain_plus_sediment_m3,max_depth_m,min_depth_m,min_sediment_m,rain_added_m3` |
| `meta.txt` | 178 B | parametri i vrijeme izvršavanja |

**Format `.npy`:** NumPy v1.0, `float32` little-endian, oblik `(512, 512)` = `(H, W)`, redoslijed po redovima. Svaki fajl ima
**1 048 704 B** = 128 B zaglavlja + 512·512·4 B podataka. Halo i padding se ne snimaju (samo unutrašnjost mreže).
Čitanje: `numpy.load("terrain_final.npy")`.

## Ko referencira ovaj folder

| Ko | Kako |
|---|---|
| `scripts/visualize_simulation.py` | čita `terrain_initial`, `terrain_final`, `water_final`, `speed_final`, `water_0*.npy`, `diagnostics.csv`, `meta.txt` (zadani ulaz je upravo `results/sim512`) |
| `scripts/run_benchmarks.sh` | korak 5: pravi ovaj folder istom naredbom kao gore |
| `scripts/docgen` (posredno) | rezultati ove simulacije ulaze u dokument preko slika i `figures/sim_summary.txt` |
| `README.md` (korijen) | odjeljci 8.5 i 9 |

## Napomene

- Ukupno oko **14 MB** u 13 `.npy` fajlova: generisani binarni podaci koji nisu namijenjeni verzionisanju u gitu.
- Lako se obnavlja (simulacija traje oko 3 s). Izvršavanje je deterministično s obzirom na seed terena (`20251`) i parametre.
- `sediment_final.npy` se ne koristi u slikama (`visualize_simulation.py` ga ne učitava); zadržan je kao izlaz simulacije.
