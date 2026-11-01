# `scripts/docgen/` – generator Word dokumenta rada

Node.js skripte koje iz mjerenja u `results/` sastavljaju seminarski rad kao **.docx** (naslovna strana, poglavlja,
tabele, slike, literatura u IEEE stilu, prilog). Tekst poglavlja je u JavaScript fajlovima, a **sve brojčane vrijednosti čitaju
se iz `results/*.csv`** (komentar u `chapters_c.js`: „ništa nije upisano ručno"). Nadređeni folder: [`scripts/`](../README.md).

## Sadržaj

| Fajl | Linija | Uloga |
|---|---|---|
| `build_document.js` | 178 | **Ulazna tačka.** Sastavlja dokument: naslovna strana, sadržaj, poglavlja A/B/C, literatura, Prilog A; upisuje `.docx` |
| `helpers.js` | 212 | Omotači nad paketom `docx`: `P` (paragraf), `H` (naslov), `bullets`, `numbered`, `EQ` / `inlineMath` (jednačine), `TABLE`, `FIG` (slika + natpis), `CODE`, `caption` |
| `references.js` | 37 | Literatura: objekat `REFS` (20 zapisa u IEEE stilu) i redoslijed citiranja `order`; numeracija se dodjeljuje redoslijedom prvog citiranja |
| `chapters_a.js` | 221 | Sažetak, Uvod, Predmet i problem proučavanja, Pregled naučne literature, Algoritmi za simulaciju plitkih voda i erozije |
| `chapters_b.js` | 193 | Projektovanje i implementacija, CPU vektorizacija, CPU paralelizacija, Priprema za GPU paralelizaciju |
| `chapters_c.js` | 243 | Eksperimentalna evaluacija: testno okruženje, metodologija, numerička ispravnost, performanse, ubrzanje i efikasnost, memorijska propusnost, rezultati simulacije |

## Kako se koristi

```bash
npm install docx          # paket nije deklarisan: u repozitoriju nema package.json
node scripts/docgen/build_document.js [REZULTATI_DIR] [IZLAZ.docx]
```

Zadano: `REZULTATI_DIR = <korijen>/results`, `IZLAZ.docx = <korijen>/docs/PRS_Shallow_Water_Terrain_Erosion_CPU.docx`.
`helpers.js` učitava paket sa `require("docx")`, pa `node_modules` mora biti dostupan iz ovog ili nekog nadređenog foldera.
Pri otvaranju u Wordu prihvatiti ažuriranje polja da se popuni automatski sadržaj.

## Šta generator čita iz `results/`

| Fajl | Obavezno | Gdje se koristi |
|---|---|---|
| `benchmarks.csv` | **da** | tabele performansi, ubrzanja, efikasnosti (`chapters_c.js`) |
| `system_info.txt` | **da** | opis testnog okruženja |
| `stream.csv` | **da** | memorijska propusnost |
| `correctness_report.csv` | **da** | tabela rezultata testova |
| `figures/sim_summary.txt` | **da** | brojke o simulaciji (erodirani/deponovani volumen, brzine, …) |
| `vectorization/disassembly_summary.txt` | **da** | tabela broja `ymm` / pakovanih / skalarnih instrukcija (`build_document.js`) |
| `figures/*.png` (9 slika) | **da** | ugrađuju se u dokument preko `FIG` (čita fajl odmah, bez provjere postojanja) |
| `stream_l3.csv`, `recheck_512_p1.csv`, `benchmarks_non_multiple8.csv` | ne | dopunske tabele; ako ih nema, preskaču se |

Slike koje dokument koristi: `perf_speedup_variants`, `perf_thread_scaling`, `perf_bandwidth`, `perf_kernel_vs_e2e`,
`perf_branch_simd_decomposition`, `sim_terrain_before_after`, `sim_terrain_difference`, `sim_water_velocity`,
`sim_mass_balance`. **Ne koriste se** `perf_throughput.png` i `sim_water_evolution.png`.

## Ko referencira ovaj folder

| Ko | Kako |
|---|---|
| `README.md` (korijen) | odjeljak 13: naredba `node scripts/docgen/build_document.js results docs/…docx` |
| `scripts/README.md` | tabela skripti |
| `docs/` | izlazni folder (`PRS_Shallow_Water_Terrain_Erosion_CPU.docx` nastaje ovdje) |

## Napomene

- **Dvostruko održavanje literature:** `references.js` (20 zapisa) i `references/bibliography.bib` (21 zapis) nisu
  povezani programski i imaju različite ključeve (`mei2007` naspram `mei2007fast`). Razlika je `khrapov2020gpu`, koji je
  u `.bib` fajlu, ali nije citiran u dokumentu. Izmjene treba ručno uskladiti.
- Naslovna strana ima prazna polja („Studenti", „Profesor / asistent", „Akademska godina") koja se popunjavaju ručno u
  Wordu, a datum („Sarajevo, novembar 2026.") je upisan u kod.
- Kod nema `package.json`, pa verzija paketa `docx` nije fiksirana.
- Generisani `.docx` je artefakt (≈ 2,6 MB) i nije namijenjen verzionisanju u gitu; skripte su njegov izvor.
