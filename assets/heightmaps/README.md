# `assets/heightmaps/` – primjeri height mapa

Primjeri iz priloga (izvorno `C:\Users\Benjamin\Desktop\height_mape`), kopirani bez izmjena. Testovi
(`tests/HeightmapTests.cpp`, `tests/ScenarioTests.cpp`) i `scripts/run_experiments.py` ih koriste kada se pokreću
iz korijena projekta.

| Folder | Datoteka | Format | Napomena |
|---|---|---|---|
| `primjer_1` | `height-map-input-try-1-height-map-16bit-texturemap-app-2026-11-08.png` | 412×221, 16-bit sivo | **nekvadratna**; preporučeni ulaz |
| | `Height_map_input_try_1_height.png` | 412×221, 8-bit RGBA (R=G=B) | ista mapa, 8 bita |
| | `Height_map_input_try_1.png` | 412×221, RGBA u boji | tekstura (nije height mapa) |
| `primjer_2` | `mocvarno-tlo-height-map-16bit-texturemap-app-2026-11-08.png` | 554×554, 16-bit sivo | močvarno tlo; svijetla pozadina je "visoka" – razmotriti `--invert` |
| | `mocvarno_tlo_height.png` | 554×554, 8-bit RGBA (R=G=B) | |
| | `mocvarno_tlo.jpeg` | 554×554, JPEG u boji | tekstura (test uvoza JPEG-a) |
| `primjer_3` | `mali-otocic-height-map-16bit-texturemap-app-2026-11-08.png` | 736×736, 16-bit sivo | ostrvo u moru |
| | `mali_otocic_height.png` | 736×736, 8-bit RGBA (R=G=B) | |
| | `mali_otocic.jpg` | 736×736, JPEG u boji | tekstura (test uvoza JPEG-a) |

Ove height mape su izvedene iz slika u boji (tekstura), pa sadrže i osvjetljenje/sitnu teksturu koje voda
"vidi" kao neravnine.
