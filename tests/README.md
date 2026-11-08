# `tests/` – testovi

Opšti opis projekta: [korijenski README](../README.md); šta testovi dokazuju: [docs/ALGORITHM_EXPLANATION.md, odjeljak 10](../docs/ALGORITHM_EXPLANATION.md#10-kako-je-provjerena-ispravnost).

## Faza 2: `prs_erosion_tests` (testovi 1–10 iz specifikacije)

Jedan program s vlastitim minimalnim okvirom (`TestFramework.hpp`: makroi `PRS_TEST`, `CHECK`, `CHECK_NEAR`,
`CHECK_LE`), bez vanjskih biblioteka. Ispisuje `[  OK  ]` / `[ FAIL ]` po testu i vraća 0 samo ako su sve
provjere prošle.

| Fajl | Testovi |
|---|---|
| `RainfallTests.cpp` | jedinice (1 L/m² = 1 mm), intenzitet, kiša samo u trajanju, zbir po neuniformnim koracima = ukupna kiša, validacija (negativno, 0 h, NaN, Inf), parsiranje (decimalni zarez, tekst), simulirani korisnik s pogrešnim unosom, tačna količina vode u simulaciji |
| `HeightmapTests.cpp` | **test 10**: PNG 16 i 8 bita, RGB (R=G=B i boja), siva+alfa, JPEG (.jpg/.jpeg/.JPG), greške (nepostojeća, .bmp, oštećena, folder), skala i inverzija, izvoz u istoj skali (nekvadratna 37×23), odsijecanje izvan raspona, cijeli tok na nekvadratnoj mapi 45×28 + ulaz bajt-identičan + zaštita od prepisivanja, primjeri iz `assets/heightmaps` |
| `WaterConservationTests.cpp` | protok iz razlike vodenih površina, jezero u mirovanju na neravnom dnu, **test 2** (ravan teren), **test 7** (očuvanje vode u svakom koraku), bilans s otvorenim rubom + infiltracijom + isparavanjem, stabilnost duboke bare, voda teče niz nagib (**test 3**, dio za vodu) |
| `ErosionTests.cpp` | kubni korijen, kapacitet, relaksacija bez preskakanja, **test 1** (bez kiše), **test 3** (nagib: erozija/depozicija), **test 8** (očuvanje materijala, zatvoreno i otvoreno), **test 9** (stabilnost), bez erozije kad je isključena, konvergencija u Δt |
| `ScenarioTests.cpp` | **test 4** (sintetička planina + stvarna mapa iz priloga), **test 5** (10/50/100/200 L/m², tabela), **test 6** (100 L/m² za 1/5/10 h, tabela) |
| `test_main.cpp` | registar, izvršavanje, filter, deterministički generatori terena (vlastiti LCG) |

Pokretanje (iz **korijena projekta**, da se nađu primjeri u `assets/heightmaps`; bez njih se ti dijelovi preskaču):

```bash
./build/prs_erosion_tests              # Linux: svi testovi (~25 s)
./build/prs_erosion_tests Scenario     # samo testovi čija grupa/ime sadrži "Scenario"
build-vs\Release\prs_erosion_tests.exe  # Windows
ctest --test-dir build                 # CTest (radni folder je korijen projekta); Windows: ctest --test-dir build-vs -C Release
```

Posljednje pokretanje u ovom okruženju (Linux, GCC 13.3, Release): **35 testova, 228 provjera, 0 neuspjelih**.
