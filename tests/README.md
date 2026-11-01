# `tests/` – testovi numeričke ispravnosti

Jedan program, **`prs_tests`** (`correctness_tests.cpp`, 337 linija), koji svaku varijantu poredi sa skalarnom referencom
(A), a samu referencu provjerava fizikalnim testovima. Opšti opis projekta: [korijenski README](../README.md).

## Sadržaj

| Fajl | Šta je |
|---|---|
| `correctness_tests.cpp` | Cjelokupni test program; vlastiti minimalni okvir (`report()` ispisuje `[ OK ]` / `[FAIL]` i upisuje red u CSV), bez vanjskih biblioteka za testiranje |

## Šta se testira (335 provjera = 280 + 11 + 7·6 + 2)

| Test | Šta provjerava | Prag | Broj |
|---|---|---|---|
| `ekvivalencija_sa_referencom` | svih 11 varijanti naspram A; mreže 8×8, 13×7, 37×29, 64×64, 100×75, 127×129, 250×250, 257×255; 300 koraka; višedretvene s 1, 2, 3, 4 i 8 dretvi | `1e-5` apsolutno / `1e-4` relativno; dodatno se ispisuje `bit-identicno` | 280 |
| `halo_i_padding_netaknuti` | na 29×11 (`W % 8 ≠ 0`) niko ne piše van unutrašnjosti nizova | tačno 0 | 11 |
| `jezero_u_mirovanju` | ravna površina vode bez kiše i isparavanja, 1000 koraka (61×45) | protoci, voda i teren tačno nepromijenjeni | 7 |
| `bilans_vode` | `W(n+1) = (W(n) + N·r·dt)·(1 − K_e·dt)` u svakom od 500 koraka (100×75) | relativna greška < 1e-5 | 7 |
| `ocuvanje_teren_plus_sediment` | 1500 koraka na 127×129 | relativna promjena < 1e-6, sediment > 0 | 7 |
| `nenegativnost_i_stabilnost` | 3000 koraka (20 × 150), 100×75 | `d ≥ 0`, `s ≥ 0`, sve konačno | 7 |
| `zatvoreni_rubovi` | izlazni protoci kroz rub domene (37×29, 400 koraka) | tačno 0 | 7 |
| `simetrija_lijevo_desno` | simetričan teren (50×31) daje bit-egzaktno simetrično rješenje | 0 neslaganja | 7 |
| `cfl_ogranicenje`, `stabilnost_na_cfl_limitu` | traženi `dt = 10 s` se ograničava na CFL limit; 3000 koraka na limitu je stabilno | `dt ≤ limit`; `max d < 1000 m` | 2 |

Testovi „A–G" (fizikalni) rade nad `main_variants()`, a ekvivalencija i padding nad glavnim i ablacijskim varijantama
zajedno. Varijante koje računar ne podržava (`variant_supported`) se preskaču uz ispis.

**Šta testovi ne pokrivaju:** ne uključuju `io_npy.hpp`, `benchmark.hpp` niti `main.cpp`, pa `.npy` izlaz, mjerni modul i
parsiranje argumenata nisu direktno testirani; `terrain.cpp` je pokriven samo posredno (preko `init_terrain`).

## Kako se pokreće

```bash
./build/prs_tests [izlazni_csv]                  # Linux/macOS
build-vs\Release\prs_tests.exe [izlazni_csv]      # Windows (Visual Studio generator)
ctest --test-dir build                            # preko CTest-a; Windows: ctest --test-dir build-vs -C Release
```

- **Izlaz:** `stdout` (red po provjeri + završno `Ukupno: 335 uspjesnih, 0 neuspjesnih`) i CSV `test,detail,result,value`.
  Bez argumenta CSV je `correctness_report.csv` u **tekućem** folderu.
- **Kod izlaza:** 0 samo ako nema neuspjelih provjera (pogodno za CI i CTest).
- **Trajanje:** oko 35 s (zavisi od računara).

## Kako je ugrađen u build

`CMakeLists.txt`, samo ako je `PRS_BUILD_TESTS=ON` (zadano):

```cmake
enable_testing()
add_executable(prs_tests tests/correctness_tests.cpp)
target_link_libraries(prs_tests PRIVATE prs_core)
add_test(NAME correctness COMMAND prs_tests)
```

Od [`include/`](../include/README.md) koristi samo `simulation.hpp` (stanje, varijante, poređenje stanja, dijagnostika) i
`cpu_features.hpp` (`cpu_brand`, `avx2_supported`, `build_description`).

## Ko referencira ovaj folder

| Ko | Kako |
|---|---|
| `CMakeLists.txt` | cilj `prs_tests` i CTest test `correctness` |
| `scripts/demo.ps1` | korak 3: pokreće `prs_tests.exe` i upisuje `demo_out\correctness_report.csv` i `correctness_tests_output.txt` (preskače se s `-SkipTests`) |
| `scripts/run_benchmarks.sh` | korak 2: `./build/prs_tests results/correctness_report.csv` |
| `results/correctness_report.csv`, `results/correctness_tests_output.txt` | izlaz programa (Linux VM) |
| `scripts/docgen/chapters_c.js` | čita `correctness_report.csv` i iz njega pravi tabelu „Rezultati testova numeričke ispravnosti" |
| `README.md` | odjeljci 8.6 i 11 |

## Napomene

- Test je **referentna tačka cijelog projekta**: svaka izmjena kernela ili zastavica treba se potvrditi ovdje prije
  mjerenja. `bench` dodatno sam provjerava svaku varijantu i prekida rad (kod izlaza 2) ako odstupa.
- Isti test prolazi na Linuxu (GCC 13) i Windowsu (MSVC): 280 od 280 poređenja je bit-identično na obje platforme.
