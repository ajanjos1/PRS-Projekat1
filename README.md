# PRS – Shallow-water and Terrain Erosion (CPU faza)

Seminarski rad iz predmeta **Paralelni računarski sistemi** (ETF Sarajevo).

Konzolna C++20 aplikacija `prs_swe` simulira tok vode po terenu (model virtuelnih cijevi, Mei, Decaudin, Hu 2007)
zajedno s hidrauličkom erozijom, depozicijom i konzervativnim transportom sedimenta. **Isti algoritam** je
implementiran u više CPU varijanti – skalarna referenca, automatska vektorizacija, `#pragma omp simd`, ručni AVX2
intrinsics i njihove kombinacije s OpenMP dretvama – a program ih **mjeri, poredi i provjerava da svaka daje
bit-identičan rezultat skalarnoj referenci**.

Ovaj dokument objašnjava projekat „odozdo": kako su podaci složeni u memoriji, šta tačno računa svaka petlja,
kako kompajler i procesor izvršavaju kernele, kako se projekat gradi i pokreće i kako se čitaju izlazni fajlovi.

## Sadržaj

1. [Brzi start](#1-brzi-start)
2. [Model: šta se računa](#2-model-šta-se-računa)
3. [Raspored podataka u memoriji](#3-raspored-podataka-u-memoriji)
4. [Kako teče jedan korak](#4-kako-teče-jedan-korak)
5. [Varijante i kerneli na niskom nivou](#5-varijante-i-kerneli-na-niskom-nivou)
6. [Zašto su rezultati bit-identični](#6-zašto-su-rezultati-bit-identični)
7. [Build sistem](#7-build-sistem)
8. [Pokretanje u detalje](#8-pokretanje-u-detalje)
9. [Izlazni fajlovi](#9-izlazni-fajlovi)
10. [Metodologija mjerenja](#10-metodologija-mjerenja)
11. [Testovi](#11-testovi)
12. [Rezultati](#12-rezultati)
13. [Struktura repozitorija](#13-struktura-repozitorija)
14. [Poznate napomene i ograničenja](#14-poznate-napomene-i-ograničenja)
15. [Literatura](#15-literatura)

---

## 1. Brzi start

Zahtjevi: **CMake ≥ 3.16**, **C++20 kompajler** (GCC ≥ 10, Clang ≥ 12 ili MSVC 2022/2026), procesor s **AVX2** (za
varijante D i G), OpenMP (opciono). Python 3 s `numpy` i `matplotlib` samo za grafikone.

### Windows (jedna komanda)

```bat
scripts\demo.cmd
```

Skripta izgradi projekat (Release), ispiše podatke o CPU-u, pokrene testove, benchmark, STREAM test memorije i
simulaciju 512×512, pa napravi grafikone. Traje oko 2–3 minute, a sav izlaz ide u `demo_out\`
(folder `results\` se ne dira). Detalji i opcije: [odjeljak 8.2](#82-windows--demo-skripta).

### Linux / macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/prs_tests                  # 335 testova, očekuje se "0 neuspjesnih"
./build/prs_swe info               # CPU, AVX2, OpenMP, lista varijanti
./build/prs_swe bench --sizes 512 --threads 1,2 --reps 5
```

---

## 2. Model: šta se računa

Domena je pravougaona mreža **W × H** ćelija veličine `L × L` metara. U svakoj ćeliji se pamte (sve `float`):

| Oznaka | Značenje | Jedinica |
|---|---|---|
| `b` | visina terena | m |
| `d` | dubina vode | m |
| `s` | suspendovani sediment (izražen kao visina) | m |
| `sr` | pomoćno polje: sediment po jedinici izlaznog volumena, `s·dt / (d1·L²)` | – |
| `fL, fR, fT, fB` | izlazni protok vode kroz lijevu, desnu, gornju (`y−1`) i donju (`y+1`) virtuelnu cijev | m³ po koraku |

Voda se između susjednih ćelija prenosi „cijevima" presjeka `A`; protok kroz cijev proporcionalan je razlici
ukupnih visina vode `h = b + d`. Jedan **vremenski korak** ima tri faze. U formulama ispod `i` je ćelija, a indeksi
`l, r, t, bt` označavaju lijevog, desnog, gornjeg i donjeg susjeda. Referentna implementacija jedne ćelije je u
[`include/cell_ops.hpp`](include/cell_ops.hpp) (`flux_cell`, `water_cell`, `sediment_cell`).

### Faza 1 – kiša i izlazni protoci

```
d1   = d + r·dt                                    kiša (d se ovdje NE upisuje)
h    = b + d1;    h_nb = b_nb + (d_nb + r·dt)      visine ćelije i susjeda
n_k  = max(0, f_k + (dt·A·g / L) · (h − h_k))      za k ∈ {L, R, T, B}
n_k  = n_k · open_k                                zid: open = 0 na rubu, inače 1
out  = (((nL + nR) + nT) + nB) · dt
K    = min(1, d1·L² / max(out, 1e-30))             ne izbaci više vode nego što je ima
f_k  = n_k · K
```

Zidovi na rubu domene ostvareni su **množenjem maskom 0/1**, a ne grananjem: `openL[x]` i `openR[x]` su nizovi po
koloni (0 za `x = 0`, odnosno `x = W−1`), a `open_t` / `open_b` su skalari po redu (`y > 0`, `y < H−1`).

### Faza 2 – voda, brzina, erozija/depozicija, isparavanje

```
fin   = ((fR[l] + fL[r]) + fB[t]) + fT[bt]         ono što susjedi šalju PREMA ovoj ćeliji
fout  = ((fL + fR) + fT) + fB
d2    = max(0, d1 + (fin − fout)·dt / L²)
dmean = ½·(d1 + d2)
wx    = ½·((in_l − fL) + (fR − in_r))              neto protok po x
wy    = ½·((in_t − fT) + (fB − in_b))              neto protok po y
inv   = (dmean > min_depth) ? 1/(L·dmean) : 0
speed = sqrt((wx·inv)² + (wy·inv)²)

gx, gy = (b[x+1] − b[x−1]) / 2L ,  (b[y+1] − b[y−1]) / 2L        centralne razlike nagiba terena
sinα   = max( sqrt(g² / (1 + g²)), min_tilt ),   g² = gx² + gy²
C      = ((K_c · sinα) · speed) · min(1, dmean / erosion_depth)  kapacitet transporta

diff   = C − s
amount = (diff > 0 ? dt·K_s : dt·K_d) · diff       erozija (>0) ili depozicija (≤ 0)
b_dst  = b_src − amount                            teren se mijenja (u DRUGI bafer)
s1     = s + amount
sr     = (d1 > sediment_eps) ? (s1·dt) / (d1·L²) : 0
d      = d2 · (1 − K_e·dt)                         isparavanje; ovdje se d prvi put upisuje
```

### Faza 3 – transport sedimenta

Konzervativni **upwind** prenos kroz iste cijevi: ćelija šalje udio sedimenta jednak udjelu vode koji je napustila, a
prima isto ono što su susjedi poslali prema njoj.

```
fout = ((fL + fR) + fT) + fB
in   = ((sr[l]·fR[l] + sr[r]·fL[r]) + sr[t]·fB[t]) + sr[bt]·fT[bt]
s    = max(0, (s − sr·fout) + in)
```

Zbir `teren + sediment` se čuva (do greške zaokruživanja), a voda se čuva do na kišu i isparavanje – to provjeravaju
testovi ([odjeljak 11](#11-testovi)).

### Parametri (`SimParams` u `include/simulation.hpp`)

| Parametar | Zadano | Značenje |
|---|---|---|
| `cell_size` (L) | 1,0 m | veličina ćelije |
| `pipe_area` (A) | 1,0 m² | presjek virtuelne cijevi |
| `gravity` (g) | 9,81 m/s² | gravitacija |
| `dt` | 0,05 s | traženi vremenski korak |
| `cfl` | 0,9 | faktor sigurnosti u CFL uslovu |
| `rain_rate` (r) | 0,002 m/s | kiša, uniformno po domeni |
| `evaporation` (K_e) | 0,01 1/s | isparavanje |
| `capacity` (K_c) | 0,6 | konstanta kapaciteta transporta |
| `dissolve` (K_s) | 0,6 1/s | brzina erozije |
| `deposit` (K_d) | 0,6 1/s | brzina depozicije |
| `min_tilt` | 0,05 | donja granica za `sin α` (erozija i na ravnom) |
| `min_depth` | 1e-3 m | ispod ove srednje dubine brzina je 0 |
| `erosion_depth` | 0,1 m | do ove dubine kapacitet linearno raste |
| `sediment_eps` | 1e-6 m | ćelije plitke od ovoga ne šalju sediment |

### Izvedene konstante i CFL uslov (`StepConstants`)

`make_step_constants()` jednom unaprijed računa sve što ne zavisi od ćelije (`rain_dt = r·dt`, `flux_coef = dt·A·g/L`,
`cell_area = L²`, `dt_over_area`, `inv_2L`, `dt_ks`, `dt_kd`, `inv_erosion_depth`, `evap_factor = 1 − K_e·dt`, …) i tako
**svi kerneli koriste iste float konstante** (preduslov za bit-identičnost).

Eksplicitna šema je stabilna samo za `c·dt/L ≤ 1/√2`, gdje je `c = √(g·A/L)` brzina talasa u modelu cijevi. Zato se
koristi `dt = min(traženi, cfl·L / (√2·c))`. Za zadane parametre: `c = 3,132 m/s`, CFL limit `= 0,2032 s`, pa se koristi
traženih `0,05 s`. Dodatno se baca izuzetak ako `dt·K_s > 1`, `dt·K_d > 1` ili `1 − K_e·dt < 0` (relaksacija bi
„preskočila" ravnotežu).

### Početni teren (`src/terrain.cpp`)

Deterministički fBm „value noise" (6 oktava, seed `20251`) plus centralni masiv i „grebeni":
`h = 0,45·massif² + 0,35·n + 0,20·ridge·massif`, skalirano na visinu `0,12·W·L`. Računa se u `double` i zavisi samo od
normalizovanih koordinata `(u, v) ∈ [0,1]`, pa sve rezolucije (128…1024) prikazuju isti pejzaž, a sve varijante dobijaju
identične ulaze. Voda, sediment i protoci počinju od nule.

---

## 3. Raspored podataka u memoriji

### SoA, `float`, row-major

Svaka veličina je poseban niz (**Structure of Arrays**). Razlog: SIMD instrukcija učitava 8 uzastopnih `float`
vrijednosti **istog polja**; da je raspored AoS (struktura po ćeliji), ta polja bi bila razbacana po memoriji.
`Simulation` (`include/simulation.hpp`) posjeduje 9 punih nizova (`b[0]`, `b[1]`, `d`, `s`, `sr`, `fL`, `fR`, `fT`, `fB`)
i 2 mala niza maski (`openL`, `openR`). `Fields` je **nevlasnički pogled** (sirovi pokazivači + `pitch`, `width`,
`height`) koji se predaje kernelima.

### Halo i padding

```
              kolona:  0 ........ 7 | 8 ......... 8+W-1 | 8+W ... pitch-1
  red 0   (gornji halo)                 [ halo: kopija reda y=0  ]
  red 1   (y = 0)        [ pad (7 praznih) | L-halo ] [ ćelije x = 0 … W-1 ] [ D-halo | pad ]
  red 2   (y = 1)        ...
  ...
  red H   (y = H-1)
  red H+1 (donji halo)                  [ halo: kopija reda y=H-1 ]
```

- Unutrašnja ćelija `(x, y)` je na indeksu **`i = (y + 1)·pitch + kPadLeft + x`**, `kPadLeft = 8`
  (`Simulation::index`, `Fields::row_offset`).
- Okvir širine 1 ćelija (halo) znači da stencil `i±1`, `i±pitch` **nikad ne izlazi iz niza** i da na rubu nema posebnih
  slučajeva u petlji.
- `pitch = round_up(kPadLeft + W + 1, 8)` – tj. 8 ćelija lijevo, `W` ćelija, najmanje 1 halo ćelija desno, zaokruženo
  na višekratnik od 8. Broj redova je `H + 2`.
- Halo se puni samo za **teren** (`refresh_halo`: replikacija rubnih vrijednosti, Neumann uslov), jer je potreban za
  nagib na rubu. Halo ostalih polja ostaje 0 i testovi provjeravaju da ga niko ne upisuje.

Primjer `W = H = 512`: `pitch = round_up(8 + 512 + 1, 8) = 528`, redova 514, jedan niz ima 271 392 `float` ≈ 1,04 MiB,
a svih 9 punih nizova ≈ 9,3 MiB (otprilike veličina L3 keša običnog desktop procesora).

### Poravnanje

- `AlignedBuffer` alocira memoriju **poravnatu na 64 B** (`std::aligned_alloc` / `_aligned_malloc`) i popunjava je
  nulama (uključujući halo i padding).
- Početak unutrašnjeg reda je `(y+1)·pitch + 8` elemenata. Pošto je `pitch` višekratnik od 8, a 8 `float` = 32 B,
  **svaki red počinje na 32-bajtnoj granici**. Zato AVX2 kernel koristi poravnato učitavanje (`_mm256_load_ps`) za
  centralnu ćeliju i susjede gore/dolje (`x ± pitch` ostaje višekratnik od 8), a **neporavnato**
  (`_mm256_loadu_ps`) samo za lijevog i desnog susjeda (`x − 1`, `x + 1`).

### Dvostruko baferovan teren

Faza 2 čita visine terena susjeda (za nagib) **i** mijenja teren ćelije. Kad bi se pisalo u isti niz, ćelija bi čitala
već izmijenjenu visinu susjeda iz istog koraka, a rezultat bi zavisio od redoslijeda (i dretvi). Zato postoje dva
bafera: `b_src` se samo čita, `b_dst` se samo piše, a na kraju koraka `swap_terrain()` zamijeni uloge (zamjena indeksa,
bez kopiranja).

### Saobraćaj prema memoriji

`bench` koristi minimalni model od **112 B po ćeliji i koraku** (faza 1: 6 čitanja + 4 upisa, faza 2: 7 + 4, faza 3:
6 + 1 → 28 `float` · 4 B, pod pretpostavkom da se susjedi uzimaju iz keša). Kolona `est_GBps` je zato **gornja procjena
prometa**, a ne mjerenje DRAM-a.

---

## 4. Kako teče jedan korak

### Lanac poziva

```
main()                                        src/main.cpp
 └─ run_bench / run_simulate / run_stream / print_info
     └─ run_steps(sim, c, steps, kernels, driver, threads)       src/variants.cpp
         ├─ Driver::Serial         → run_serial(...)             src/driver_serial.cpp
         ├─ Driver::OmpParallelFor → run_omp_parallel_for(...)   src/driver_openmp.cpp
         └─ Driver::OmpRegion      → run_omp_region(...)         src/driver_openmp.cpp
              za svaki korak:
                 sim.refresh_terrain_halo()                       (serijski, O(W + H))
                 za y: kernels.flux(F, c, b, y)                   faza 1
                 za y: kernels.water(F, c, b, b_next, y)          faza 2
                 za y: kernels.sediment(F, c, y)                  faza 3
                 sim.swap_terrain()
```

`KernelSet` je struktura od **tri pokazivača na funkcije** (`FluxRowFn`, `WaterRowFn`, `SedimentRowFn`), svaka obrađuje
**jedan red**. Dakle, indirektni poziv se plaća po redu (jednom na `W` ćelija), a ne po ćeliji, i zanemarljiv je.
Varijanta je kombinacija *„kako računam red"* (`KernelSet`) i *„ko računa koje redove"* (`Driver`).

### Ko šta čita i piše

Ovo je tabela koja opravdava paralelizaciju:

| Faza | Čita | Piše | Posljedica |
|---|---|---|---|
| 1 | `b`, `d` (ćelija + 4 susjeda); `fL,fR,fT,fB` (samo svoje); maske | `fL,fR,fT,fB` (samo svoje) | susjedi se samo čitaju iz polja koja faza ne mijenja (`b`, `d`) |
| 2 | `d`, `s`, `fL..fB` (svoje); `fR[l]`, `fL[r]`, `fB[t]`, `fT[bt]` (susjedi); `b_src` (ćelija + 4 susjeda) | `b_dst`, `s`, `sr`, `d` (samo svoje) | protoci susjeda su gotovi iz faze 1; teren ide u drugi bafer |
| 3 | `s` (svoje); `sr`, `fL..fB` (ćelija + susjedi) | `s` (samo svoje) | susjedi čitaju `sr` i `f*`, a upisuje se samo `s[i]` |

Zaključci:

1. **Unutar jedne faze nema race condition-a** – svaka ćelija piše isključivo svoje elemente, a susjede čita iz nizova
   koje ta faza ne mijenja. Zato se redovi mogu dijeliti dretvama, a ćelije u redu SIMD trakama.
2. **Između faza postoji prava zavisnost** (faza 2 čita rezultate faze 1 u *susjednim* ćelijama), pa je potrebna
   **barijera**: sve ćelije faze *k* moraju biti gotove prije početka faze *k+1*. U OpenMP-u je to implicitna barijera
   na kraju `parallel for` / `omp for`.
3. Faza 1 **ne upisuje `d`**; kišu računa lokalno (`d1 = d + r·dt`), a faza 2 je ponovo računa istom formulom. Tako
   susjed u fazi 1 uvijek vidi staru vrijednost `d`, bez obzira na to koja je dretva prva stigla.

---

## 5. Varijante i kerneli na niskom nivou

| Varijanta | Kernel (fajl) | Driver | Šta ona mjeri |
|---|---|---|---|
| **A_scalar** | `kernels_scalar.cpp` | serijski | referenca, vektorizacija isključena |
| **B_auto** | `kernels_auto.cpp` + `kernels_branchless.inl` | serijski | automatska vektorizacija kompajlera |
| **C_omp_simd** | `kernels_omp_simd.cpp` | serijski | `#pragma omp simd` |
| **D_avx2** | `kernels_avx2.cpp` | serijski | ručni AVX2 intrinsics |
| **E_omp** | kao A | `omp parallel for` | samo dretve |
| **F_omp_simd** | kao C | `omp parallel for` | dretve + `omp simd` |
| **G_omp_avx2** | kao D | `omp parallel for` | dretve + AVX2 |
| A2_scalar_branchless | `kernels_scalar_branchless.cpp` (izvor B) | serijski | ablacija: efekat grananja |
| E2_omp_region | kao A | `omp parallel` + `omp for` | ablacija: jedna paralelna regija |
| F2_omp_simd_region | kao C | `omp parallel` + `omp for` | ablacija: jedna paralelna regija |
| H_omp_auto | kao B | `omp parallel for` | dretve + auto-vektorizacija |

Registar je u `src/variants.cpp` (`main_variants()`, `ablation_variants()`). Varijante koje traže AVX2 ili OpenMP,
a računar ih ne podržava, `variant_supported()` preskače (u `info` izlazu piše `[NIJE PODRZANO]`).

### 5.1 A – skalarna referenca (`kernels_scalar.cpp`)

Petlja po `x` poziva `ref::flux_cell / water_cell / sediment_cell` za svaku ćeliju. Vektorizacija je isključena na tri
nivoa: zastavice kompajlera za cijeli fajl (GCC `-fno-tree-vectorize -fno-tree-loop-vectorize -fno-tree-slp-vectorize`,
Clang `-fno-vectorize -fno-slp-vectorize`), makro `SW_NO_VECTORIZE` ispred svake petlje (`#pragma loop(no_vector)` na
MSVC-u) i provjera u izvještaju (izvještaj o vektorizaciji za ovaj fajl mora biti prazan, a disasembler ne smije
sadržavati pakovane instrukcije – vidi `results/vectorization/disassembly_summary.txt`: 0 `ymm`, 0 pakovanih, 104
skalarne `*ss`). Ovdje se koriste grananja (`if (nl < 0) nl = 0;`, `if (denom < tiny) …`).

### 5.2 B i A2 – kod pisan za auto-vektorizaciju (`kernels_branchless.inl`)

Isto tijelo (bez `if` u petlji, sa `std::min/max`/ternarnim izrazima) uključuje se u **dva prevodna fajla** s različitim
zastavicama: `kernels_auto.cpp` (kompajler vektorizuje) i `kernels_scalar_branchless.cpp` (vektorizacija isključena –
ablacija A2). Razlika A → A2 mjeri efekat uklanjanja grananja (pogrešna predviđanja skokova), a A2 → B čisti efekat SIMD-a.

Šta je bilo potrebno da GCC 13 vektorizuje petlje:

- **`__restrict` na parametrima funkcije** (`flux_row_impl(…, const float* SW_RESTRICT b, …)`), a pokazivači na red iznad
  i ispod (`b − P`, `b + P`) prosljeđuju se kao zasebni parametri. GCC ne koristi pouzdano `restrict` na lokalnim
  pokazivačima; prva verzija je tražila previše runtime provjera preklapanja (*„versioning for alias required"*) i
  odustajala (`results/vectorization/gcc_report_v1_initial.txt`).
- **Uslovi kao `min/max/?:`** – kompajler ih pretvara u SIMD maske (if-conversion). Dijeljenja i korijeni računaju se
  bezuslovno (npr. `1/(L·max(dmean, min_depth))`), a rezultat se zatim bira, pa nema dijeljenja nulom.
- **`-fno-math-errno`** – bez ovoga `sqrt` postavlja `errno` i petlja se ne može vektorizovati.
- **`-fno-trapping-math`** – bez ovoga GCC javlja *„not vectorized: control flow in loop"*, jer množenje rezultatom
  `min/max` izraza može izazvati FP izuzetak i ne smije se spekulativno izvršiti. Vrijednosti se ne mijenjaju (nema
  reasocijacije ni FTZ promjena), što potvrđuju bit-identični testovi. Clang ovo ima po defaultu.

### 5.3 C – `#pragma omp simd` (`kernels_omp_simd.cpp`)

`SW_OMP_SIMD` (makro oko `_Pragma("omp simd")` / `__pragma(omp simd)`) je **obećanje programera** da su iteracije
unutrašnje petlje nezavisne, pa kompajler vektorizuje bez analize preklapanja pokazivača i bez cost-modela koji bi
vektorizaciju mogao odbiti (zato ovdje nema `__restrict`). Obećanje je tačno jer u svakoj fazi iteracija `x` piše samo
elemente s indeksom `x`. Direktiva **ne pokreće dretve** – to radi tek `omp parallel for`. Uz podrazumijevani
`-ftrapping-math` ni ovo nije bilo dovoljno, pa se fajl prevodi s `-fno-trapping-math` (isto kao B).

### 5.4 D – AVX2 intrinsics (`kernels_avx2.cpp`)

Jedan `__m256` sadrži 8 `float`; petlja korača po 8 (`kSimdFloats = 8`), a ostatak reda (`W % 8` ćelija) obrađuje
skalarna referenca iz `cell_ops.hpp`.

| Skalarno | AVX2 | Napomena |
|---|---|---|
| `if (x < 0) x = 0` / `max(x, 0)` | `_mm256_max_ps(zero, x)` | operandi su poredani tako da se poklapaju sa semantikom `std::max` (rezultat za NaN i ±0 je isti) |
| `min(1, vol/denom)` | `_mm256_min_ps(one, div)` | |
| `nl *= openL[x]` | `_mm256_mul_ps(…, _mm256_load_ps(openL + x))` | maska zida kao množenje |
| `inv = (dmean > min_depth) ? … : 0` | `_mm256_cmp_ps(…, _CMP_GT_OQ)` + `_mm256_and_ps` | maska pa AND – nulira trake koje ne zadovoljavaju uslov |
| `rate = (diff > 0) ? dt_ks : dt_kd` | `_mm256_blendv_ps(dtkd, dtks, mask)` | izbor po maski |
| `sqrt`, `/` | `_mm256_sqrt_ps`, `_mm256_div_ps` | `vsqrtps`, `vdivps` |
| `(d[i−1] + rain)` | `_mm256_loadu_ps(d + x − 1)` | neporavnano; centar i `±pitch` su poravnati |

Fajl se prevodi s `-mavx2` (MSVC: `/arch:AVX2`) i poziva se **samo ako `avx2_supported()` vrati true**. Ta provjera
(`src/cpu_features.cpp`) čita `CPUID` list 1 (bitovi `OSXSAVE` i `AVX`), `XGETBV` (operativni sistem čuva XMM i YMM
registre) i `CPUID` list 7 (EBX bit 5 = AVX2) – ne vjeruje samo procesoru nego i OS-u. Na ne-x86 platformama
(`PRS_ENABLE_AVX2=OFF`) `kernels_avx2()` vraća kernele iz B (`"avx2(fallback:auto)"`).

Računska cijena po ćeliji i koraku: **4 dijeljenja i 2 kvadratna korijena** (faza 1: `vol/denom`; faza 2: `1/(L·dmean)`,
`g²/(1+g²)`, `(s1·dt)/(d1·L²)` i dva `sqrt`). Te instrukcije su mnogo sporije od sabiranja i množenja, pa su (uz
promet prema memoriji) razlog zašto ubrzanje od vektorizacije nije puna širina od 8× – ovo je objašnjenje iz analize
koda, ne rezultat profilisanja.

### 5.5 Drajveri (`driver_serial.cpp`, `driver_openmp.cpp`)

**Serijski** – tri petlje po `y` unutar petlje po koracima, kao u dijagramu u [odjeljku 4](#4-kako-teče-jedan-korak).

**`omp parallel for` (E, F, G, H)** – tri `#pragma omp parallel for schedule(static) num_threads(threads)` po koraku,
dakle **fork-join po fazi**. `schedule(static)` daje svakoj dretvi jedan kontinuirani blok od ≈ `H/p` redova (jeftino i
dobro za lokalnost keša, jer susjedni redovi pripadaju istoj dretvi). Barijera je implicitna na kraju svake petlje.
Halo terena i `swap_terrain()` izvršava glavna dretva, serijski.

**`omp parallel` + `omp for` (E2, F2)** – jedna paralelna regija za sve korake, tako da se tim dretvi ne formira
iznova u svakoj fazi. Halo i zamjenu bafera radi jedna dretva (`#pragma omp single`, s implicitnom barijerom). Mjerenja
pokazuju da je razlika u odnosu na fork-join zanemariva pri ovim veličinama mreže.

**False sharing** – dretve pišu u različite redove; redovi su poravnati na 32 B, a `pitch·4 B ≥ 64 B`, pa granica
između dva bloka redova dijeli najviše jednu liniju keša po nizu. Efekat je zanemariv.

**Flush-to-zero / denormals-are-zero** – `enable_flush_denormals()` postavlja bitove 15 (FTZ) i 6 (DAZ) u `MXCSR`
(`_mm_setcsr(_mm_getcsr() | 0x8040)`). Subnormalni brojevi (< ~1,18·10⁻³⁸, npr. sediment koji eksponencijalno opada u
suhim ćelijama) inače mogu usporiti izvršavanje višestruko. `MXCSR` je registar **po dretvi**, pa se poziva u svakoj
dretvi tima, a za sve varijante – i serijske – isto, da poređenje ostane fer. Komentar u kodu se oslanja na to da OpenMP
runtime ponovo koristi iste dretve za naredne paralelne regije s istim brojem dretvi.

---

## 6. Zašto su rezultati bit-identični

Rezultat svake varijante poredi se s A bit-po-bit (`compare_states`), pa mora važiti da sve izvršavaju **iste IEEE-754
operacije istim redoslijedom**:

1. **Jedna specifikacija** – `cell_ops.hpp`; svi kerneli ponavljaju iste izraze, uključujući redoslijed sabiranja
   (npr. `(((nl + nr) + nt) + nb)`), a ne neki algebarski ekvivalentan.
2. **`-ffp-contract=off`** – kompajler ne smije spojiti `a·b + c` u FMA (jedno zaokruživanje umjesto dva). Na MSVC-u
   se koristi `/fp:precise`.
3. **Bez `-ffast-math`** – nema reasocijacije, zamjene dijeljenja množenjem recipročne vrijednosti niti pretpostavke da
   nema NaN/Inf. `-fno-math-errno` i `-fno-trapping-math` mijenjaju samo to da li se `errno`/FP izuzeci postavljaju, ne
   izračunate vrijednosti.
4. **Iste konstante** – sve dolaze iz `StepConstants`, izračunate jednom u `float`.
5. **Particija ne mijenja redoslijed** – svaka ćelija se računa sama od sebe (nema redukcija po ćelijama), pa broj
   dretvi i način podjele redova ne utiču na vrijednosti.

Posljedica: `prs_tests` za sve varijante i sve dimenzije/brojeve dretvi prijavljuje `bit-identicno` (280 od 280
poređenja i na Linuxu i na Windowsu).

---

## 7. Build sistem

`CMakeLists.txt` pravi statičku biblioteku **`prs_core`** (sav kod osim `main`), izvršni program **`prs_swe`**
(`src/main.cpp`) i test program **`prs_tests`** (`tests/correctness_tests.cpp`, registrovan kao CTest test `correctness`).
Zastavice se postavljaju **po izvornom fajlu**, jer se ista logika namjerno prevodi na različite načine:

| Fajl(ovi) | Zastavice (GCC/Clang) | Svrha |
|---|---|---|
| svi (PUBLIC) | `-Wall -Wextra -Wpedantic -ffp-contract=off -fno-math-errno` (MSVC: `/W4 /EHsc /fp:precise /utf-8`) | bit-identičnost; `/utf-8` jer izvori sadrže dijakritike |
| `kernels_scalar.cpp`, `kernels_scalar_branchless.cpp` | `-fno-tree-vectorize -fno-tree-loop-vectorize -fno-tree-slp-vectorize` (Clang: `-fno-vectorize -fno-slp-vectorize`) | A i A2 bez vektorizacije |
| `kernels_auto.cpp`, `kernels_omp_simd.cpp`, `kernels_scalar_branchless.cpp` | `-fno-trapping-math` | omogućava if-konverziju |
| `kernels_auto.cpp`, `kernels_omp_simd.cpp`, `kernels_avx2.cpp`, `kernels_scalar_branchless.cpp` | `-mavx2` (MSVC: `/arch:AVX2`) | ista širina vektora (256 b) za B, C, D radi fer poređenja |
| `prs_core` (kad je OpenMP uključen) | `-fopenmp` (MSVC: `/openmp:experimental`) | dretve i `omp simd` |

Napomena: A (`kernels_scalar.cpp`) se prevodi **bez** `-mavx2`, tj. kao skalarni kod osnovnog skupa instrukcija.

### CMake opcije

| Opcija | Zadano | Efekat |
|---|---|---|
| `PRS_ENABLE_OPENMP` | ON | OpenMP dretve i `omp simd`. Ako runtime nije pronađen: `-fopenmp-simd` (GCC/Clang), višedretvene varijante se preskaču |
| `PRS_ENABLE_AVX2` | ON | `-mavx2` i AVX2 kernel (samo x86; inače `D`/`G` koriste kod iz B) |
| `PRS_VECTORIZATION_REPORTS` | OFF | izvještaji kompajlera o vektorizaciji (`-fopt-info-vec-*`, `-Rpass=loop-vectorize`, `/Qvec-report:2`) |
| `PRS_BUILD_TESTS` | ON | gradi `prs_tests` |

Ako `CMAKE_BUILD_TYPE` nije zadan (jednokonfiguracioni generatori), koristi se **Release**.

`include/build_info.hpp.in` se pri konfigurisanju pretvara u `generated/build_info.hpp` (kompajler, tip builda,
zastavice). Taj tekst ispisuju `prs_swe info`, `bench` i `prs_tests`, pa je svako mjerenje vezano za tačne zastavice
kojima je dobijeno.

---

## 8. Pokretanje u detalje

### 8.1 Preduslovi

| Šta | Zašto | Provjera |
|---|---|---|
| CMake ≥ 3.16 | build | `cmake --version` |
| C++20 kompajler | `std::aligned_alloc`, `<filesystem>`, … | Windows: Visual Studio 2022 ili 2026 s radnim opterećenjem „Desktop development with C++"; Linux: `g++ --version` |
| AVX2 procesor | varijante D i G (i B, C, H kad je AVX2 build) | `prs_swe info` ispisuje `AVX2 (runtime): da/ne` |
| Python 3 + `numpy` + `matplotlib` | samo grafikoni | `py -m pip install numpy matplotlib` |

### 8.2 Windows – demo skripta

```bat
scripts\demo.cmd [-Full] [-SkipBuild] [-SkipTests] [-NoOpen]
```

`demo.cmd` je samo omotač koji pokreće `demo.ps1` s `-ExecutionPolicy Bypass` (važi samo za to pokretanje).
Skripta redom izvršava:

| Korak | Šta radi |
|---|---|
| 1 | Ako `build-vs\CMakeCache.txt` ne postoji, konfiguriše CMake (generator „Visual Studio 18 2026" ili, ako njega nema, „Visual Studio 17 2022", `-A x64`), pa gradi `--config Release -j` |
| 2 | `prs_swe info` |
| 3 | `prs_tests` → `demo_out\correctness_report.csv`, `demo_out\correctness_tests_output.txt` |
| 4 | `prs_swe bench` s mrežama i brojevima dretvi prilagođenim računaru → `demo_out\benchmarks.csv`, `benchmark_runs.csv`, `benchmark_output.txt` |
| 5 | `prs_swe stream --threads 1,<jezgre>` → `demo_out\stream.csv` |
| 6 | `prs_swe simulate --size 512 --steps 6000 --variant G_omp_avx2 --snap 1500` → `demo_out\sim512\` |
| 7 | Traži Python s `numpy`+`matplotlib`; ako ga nađe, pravi grafikone u `demo_out\figures\` |

| Opcija | Efekat |
|---|---|
| *(bez opcija)* | brzi režim: mreže 128, 256, 512; 3 ponavljanja, 1 warm-up; oko 2–3 min |
| `-Full` | mreže 128–1024; 10 ponavljanja, 2 warm-up; nekoliko minuta duže |
| `-SkipBuild` | preskače izgradnju (ako je `build-vs\` već izgrađen) |
| `-SkipTests` | preskače testove (≈ 35 s) |
| `-NoOpen` | ne otvara folder s grafikonima na kraju |

Lista dretvi se pravi iz broja **fizičkih** jezgri (npr. 6 jezgri → `1,2,4,6`). Skripta piše samo u `demo_out\`
(folder je u `.gitignore`).

### 8.3 Windows – ručno

```bat
:: 1) konfiguracija (cmake --help ispisuje dostupne generatore; za VS 2022 je "Visual Studio 17 2022")
cmake -G "Visual Studio 18 2026" -A x64 -S . -B build-vs

:: 2) izgradnja – Visual Studio je višekonfiguracioni generator, pa se Release bira ovdje
cmake --build build-vs --config Release -j

:: 3) pokretanje; izvršni fajlovi su u build-vs\Release\
build-vs\Release\prs_swe.exe info
build-vs\Release\prs_tests.exe
ctest --test-dir build-vs -C Release
```

Alternativa: u Visual Studiju `File → Open → Folder…` i odabrati korijen projekta (generiše `out\build\…`).

### 8.4 Linux / macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
bash scripts/run_benchmarks.sh      # kompletan postupak, vidi dolje
```

`scripts/run_benchmarks.sh` (piše u `results/`, namijenjena Linuxu/macOS-u):

1. gradi `build/`,
2. `system_info.sh` → `results/system_info.txt` (koristi `lscpu`, `free`, `/proc/cpuinfo`),
3. `prs_tests` → `results/correctness_report.csv`, `results/correctness_tests_output.txt`,
4. izvještaji kompajlera (zaseban build `build-vecreport` s `PRS_VECTORIZATION_REPORTS=ON`) → `results/vectorization/compiler_report.txt`,
5. `stream` i `bench` (mreže 128–1024, 1/2/4/8 dretvi, 10 ponavljanja) + dodatno 1000² i 250² (širina nije višekratnik od 8),
6. `simulate` 512², 6000 koraka, pa `visualize_simulation.py` i `plot_results.py`.

### 8.5 Načini rada programa `prs_swe`

```
prs_swe info
prs_swe bench    [opcije]
prs_swe simulate [opcije]
prs_swe stream   [opcije]
```

Bez argumenata program radi kao `info`. Opcije su oblika `--ime vrijednost`; opcija bez vrijednosti je zastavica.
Veličina mreže može biti `512` (⇒ 512×512) ili `250x130`.

#### `info`

Ispisuje CPU (iz CPUID), broj logičkih jezgri, AVX2, OpenMP (broj dretvi), opis builda, `dt`/CFL limit i listu
varijanti sa oznakom `[NIJE PODRZANO]` gdje je to slučaj.

#### `bench` – mjerenje performansi

| Opcija | Zadano | Značenje |
|---|---|---|
| `--sizes` | `128,256,512,1024` | mreže koje se mjere |
| `--threads` | `1,2,4,8` | brojevi dretvi za višedretvene varijante |
| `--reps` | `10` | broj mjerenih ponavljanja |
| `--warmup` | `1` | broj zagrijavanja (ne mjere se) |
| `--spinup` | `200` | koraka prije mjerenja da „voda već teče" |
| `--updates` | `8388608` (2²³) | ćelija-koraka po mjerenju; `steps = max(4, round(updates / (W·H)))`, tj. 512/128/32/8 koraka za 128²/256²/512²/1024² |
| `--e2e-threads` | `2` | broj dretvi u mjerenju cijele simulacije |
| `--e2e-factor` | `10` | cijela simulacija radi 10× više koraka od mjerenja jezgra |
| `--no-e2e` | – | preskače mjerenje cijele simulacije |
| `--no-ablation` | – | preskače A2, E2, F2, H |
| `--variants` | sve | npr. `A_scalar,G_omp_avx2` (A se uvijek mjeri jer je baza ubrzanja) |
| `--out` | `results/benchmarks.csv` | glavni CSV |
| `--runs-out` | `results/benchmark_runs.csv` | CSV pojedinačnih ponavljanja |

Primjeri:

```
prs_swe bench --sizes 128,256,512,1024 --threads 1,2,4,8 --reps 10 --warmup 2
prs_swe bench --sizes 512 --threads 1,2 --variants G_omp_avx2,F_omp_simd --no-e2e
prs_swe bench --sizes 1000,250 --threads 1,2 --no-e2e --no-ablation --out results/bench_nm8.csv
```

Poslije svake izmjerene mreže CSV se odmah upisuje (prekinuto mjerenje ne gubi djelimične rezultate). Ako ijedna
varijanta odstupi od reference van tolerancije (`1e-5` apsolutno / `1e-4` relativno), program ispiše `GRESKA: …` i
vrati kod izlaza **2**.

#### `simulate` – jedna kompletna simulacija sa snimcima

| Opcija | Zadano | Značenje |
|---|---|---|
| `--size` | `512` | mreža |
| `--steps` | `4000` | broj koraka |
| `--variant` | `G_omp_avx2` | bilo koja varijanta iz tabele |
| `--threads` | `2` | broj dretvi |
| `--snap` | `1000` | svakih toliko koraka snima `water_*.npy` i `terrain_*.npy` (`0` = ne snima) |
| `--diag` | `50` | svakih toliko koraka upisuje red u `diagnostics.csv` |
| `--out` | `results/sim` | izlazni folder |
| `--rain --kc --ks --kd --ke --dt` | iz `SimParams` | zamjena parametara modela |

```
prs_swe simulate --size 512 --steps 6000 --variant G_omp_avx2 --threads 4 --snap 1500 --diag 100 --out demo_out/sim512
```

Ako se vrijednosti u nekom koraku ne mogu predstaviti (NaN/Inf), program prekida s greškom *„Simulacija je postala
nestabilna"*.

#### `stream` – propusnost memorije

STREAM *triad* `a[i] = b[i] + s·c[i]` nad tri niza, svaki `--mb` MB (zadano 128, dovoljno veliko da ne stane u keš).
Promet je `3·4·n` bajtova; ispisuje GB/s (medijan i najbolje) za svaki broj dretvi iz `--threads` (zadano `1,2,4,8`) i
upisuje `--out` (zadano `results/stream.csv`). Služi za poređenje „koliko memorija može" naspram „koliko kernel treba".
`--mb 12` mjeri sa nizom koji stane u L3 keš.

### 8.6 Testovi

```bash
./build/prs_tests [izlazni_csv]                 # Linux
build-vs\Release\prs_tests.exe [izlazni_csv]     # Windows
ctest --test-dir build                           # preko CTest-a (Windows: ctest --test-dir build-vs -C Release)
```

Bez argumenta CSV se upisuje u `correctness_report.csv` u tekućem folderu. Završni red je
`Ukupno: 335 uspjesnih, 0 neuspjesnih`, a kod izlaza je 0 samo ako nema neuspjelih testova.

### 8.7 Grafikoni

```bash
python3 scripts/visualize_simulation.py results/sim512 results/figures    # slike simulacije iz .npy
python3 scripts/plot_results.py results results/figures                    # grafikoni performansi iz CSV-ova
```

(Windows: `py -3 …`.) `visualize_simulation.py` pravi `sim_terrain_before_after`, `sim_terrain_difference`,
`sim_water_evolution`, `sim_water_velocity`, `sim_mass_balance` i `sim_summary.txt`. `plot_results.py` pravi
`perf_speedup_variants`, `perf_throughput`, `perf_thread_scaling`, `perf_bandwidth`, `perf_kernel_vs_e2e` i
`perf_branch_simd_decomposition`; broj fizičkih jezgri čita iz reda `Core(s) per socket` u `system_info.txt`.

### 8.8 Okruženje OpenMP

Broj dretvi u `bench`/`simulate` zadaje se **opcijom `--threads`** (prosljeđuje se kao `num_threads(p)`), a ne
`OMP_NUM_THREADS`; ta varijabla utiče samo na ispis maksimuma u `info`. Broj dretvi treba uskladiti s brojem
**fizičkih** jezgri – s više dretvi nego jezgri dobija se *oversubscription*, a ne skaliranje. Za stabilna mjerenja s
GCC OpenMP runtime-om (libgomp) preporučuje se:

```bash
OMP_PROC_BIND=close OMP_PLACES=cores ./build/prs_swe bench …
```

(PowerShell: `$env:OMP_PROC_BIND = "close"`.)

### 8.9 Rješavanje problema

| Simptom | Uzrok / rješenje |
|---|---|
| `info`: `AVX2 (runtime): ne`, varijante `[NIJE PODRZANO]` | procesor/OS ne podržava AVX2; preskaču se sve varijante čiji je kernel preveden s `-mavx2` (A2, B, C, D, F, F2, G, H), a rade A, E i E2 |
| `Varijanta nije podrzana na ovom racunaru` | `simulate --variant …` traži varijantu koja nije podržana; izaberite npr. `A_scalar` |
| `GRESKA: … odstupa od reference` (kod 2) | varijanta daje rezultat izvan tolerancije; pokrenuti `prs_tests` da se vidi koja |
| `Neispravni parametri` / `dt * K_s … moraju biti <= 1` | zadani `--dt/--ks/--kd/--ke` krše uslov stabilnosti relaksacije |
| `Nije pronadjen Visual Studio 2022/2026 generator` | instalirati VS s C++ opterećenjem, ili ručno zadati generator ([8.3](#83-windows--ručno)) |
| Skripta se ne pokreće (execution policy) | koristiti `scripts\demo.cmd`, ne `demo.ps1` direktno |
| Grafikoni se preskaču | nema Pythona s `numpy`+`matplotlib`: `winget install Python.Python.3.13`, pa `py -m pip install numpy matplotlib` |
| `grep` ne nalazi tekst u `demo_out\*.txt` | PowerShell 5.1 ih upisuje kao UTF-16; otvoriti u editoru ili pretvoriti u UTF-8 |
| Nakon demoa se pojavio prazan `results\` | `prs_swe stream` bezuslovno poziva `create_directories("results")`; bezopasno |
| Višedretvene varijante nedostaju | OpenMP nije pronađen pri konfigurisanju (provjeriti poruku CMake-a) |

---

## 9. Izlazni fajlovi

### `benchmarks.csv` (jedan red = jedna varijanta × broj dretvi × mjera)

| Kolona | Značenje |
|---|---|
| `grid, W, H, cells` | mreža |
| `variant, label, kernels, driver` | varijanta, opis, `KernelSet`, drajver (`serial`, `omp_parallel_for`, `omp_region`) |
| `measure` | `kernel` (samo koraci) ili `e2e` (alokacija + teren + koraci + dijagnostika) |
| `threads, steps, reps, warmups` | parametri mjerenja |
| `median_ms, min_ms, max_ms, mean_ms, stddev_ms, cv_percent` | statistika vremena (uzoračka devijacija, `cv = 100·σ/μ`) |
| `mcells_per_s` | `W·H·steps / median / 10⁶` |
| `ns_per_cell_step` | `median·10⁹ / (W·H·steps)` |
| `est_GBps` | `112·W·H·steps / median / 10⁹` (model prometa, vidi [odjeljak 3](#saobraćaj-prema-memoriji)) |
| `speedup_vs_scalar` | `median(A) / median(varijanta)` – A je uvijek prvi u redoslijedu |
| `speedup_vs_1thread_same_kernels` | ubrzanje u odnosu na **serijsku** varijantu s istim kernelima (npr. G naspram D) |
| `parallel_efficiency` | prethodna kolona podijeljena brojem dretvi |
| `bitwise_equal_to_scalar`, `max_abs_diff` | rezultat poređenja s referencom (samo za `kernel` redove) |
| `checksum` | `state_checksum` stanja nakon mjerenja |

Napomena: u `e2e` redovima kolona `bitwise_equal_to_scalar` je uvijek 1 (ne provjerava se).

### `benchmark_runs.csv`

`grid,variant,measure,threads,run,time_ms` – sva pojedinačna ponavljanja (za box-plot, test normalnosti, itd.).

### `stream.csv`

`threads,array_mb,median_ms,min_ms,GBps_median,GBps_best`.

### `correctness_report.csv`

`test,detail,result,value` – jedan red po provjeri (`PASS`/`FAIL`).

### Folder simulacije (`simulate --out DIR`)

| Fajl | Sadržaj |
|---|---|
| `terrain_initial.npy` | početni teren |
| `water_NNNNNN.npy`, `terrain_NNNNNN.npy` | snimci na svakih `--snap` koraka |
| `terrain_final.npy`, `water_final.npy`, `sediment_final.npy`, `speed_final.npy` | završno stanje; brzina toka iz protoka i trenutne dubine |
| `diagnostics.csv` | `step,time_s,water_m3,terrain_m3,sediment_m3,terrain_plus_sediment_m3,max_depth_m,min_depth_m,min_sediment_m,rain_added_m3` |
| `meta.txt` | `W, H, steps, dt, cell_size, variant, threads, wall_time_s` i parametri modela |

`.npy` je NumPy format verzije 1.0: magic `\x93NUMPY`, zaglavlje s `{'descr': '<f4', 'fortran_order': False, 'shape': (H, W)}`
dopunjeno razmacima do višekratnika od 64 B, pa **sirovi little-endian `float32`** niz u row-major poretku. U Pythonu:
`numpy.load("terrain_final.npy")`. Halo i padding se ne snimaju (`extract()` kopira samo unutrašnjost).

---

## 10. Metodologija mjerenja

- **Početno stanje:** teren + *spin-up* (200 koraka skalarnom varijantom, izvan mjerenja) – voda već teče, što je
  realističnije od praznog stanja; isto stanje dobijaju sve varijante (`copy_state_from`).
- **Referenca:** skalarna varijanta A iz istog početnog stanja; svako mjerenje se poredi s njom (`compare_states`).
- **Vrijeme:** `std::chrono::steady_clock`; `prepare()` (vraćanje početnog stanja) **nije** u mjerenom vremenu;
  `atomic_signal_fence` s obje strane sprečava da kompajler pomjeri posao preko granice mjerenja; warm-up se ne računa.
- **Sprečavanje eliminacije koda:** `consume(observe())` XOR-uje bite kontrolne sume u globalni atomic, pa kompajler ne
  može odbaciti „neiskorišten" rad.
- **Statistika:** medijan (otporan na ispade), min, max, srednja vrijednost i uzoračka standardna devijacija;
  izvještava se medijan.
- **Količina rada:** po mjerenju je ≈ 2²³ ćelija-koraka za svaku mrežu, pa su vremena različitih mreža uporediva.
- **Jezgro naspram cijele simulacije:** `measure=kernel` mjeri samo korake; `measure=e2e` uključuje alokaciju,
  generisanje terena i dijagnostiku (10× više koraka).
- **Memorija:** `stream` daje gornju granicu propusnosti za poređenje s `est_GBps`.

---

## 11. Testovi

`tests/correctness_tests.cpp` – 335 provjera (`280 + 11 + 7·6 + 2`):

| Test | Šta provjerava | Prag | Broj |
|---|---|---|---|
| ekvivalencija sa referencom | svaka od 11 varijanti, na mrežama 8×8, 13×7, 37×29, 64×64, 100×75, 127×129, 250×250, 257×255, 300 koraka; višedretvene s 1, 2, 3, 4, 8 dretvi | `1e-5` apsolutno / `1e-4` relativno, a prijavljuje se i `bit-identicno` | 280 |
| halo i padding netaknuti | na 29×11 (`W % 8 ≠ 0`) niko ne piše van unutrašnjosti | tačno 0 | 11 |
| jezero u mirovanju | ravna površina vode, bez kiše i isparavanja, 1000 koraka: protoci, voda i teren se ne mijenjaju | tačno 0 | 7 |
| bilans vode | `W_{n+1} = (W_n + N·r·dt)·(1 − K_e·dt)` svaki korak | relativna greška < 1e-5 (izmjereno ≈ 1,5e-8) | 7 |
| očuvanje teren + sediment | 1500 koraka na 127×129 | relativno < 1e-6 (izmjereno ≈ 2e-7), sediment > 0 | 7 |
| nenegativnost i stabilnost | 3000 koraka | `d ≥ 0`, `s ≥ 0`, sve konačno | 7 |
| zatvoreni rubovi | izlazni protok kroz rub domene | tačno 0 | 7 |
| simetrija lijevo–desno | simetričan teren daje simetrično rješenje | 0 neslaganja (bit-egzaktno) | 7 |
| CFL | traženi `dt = 10 s` se ograničava na limit; 3000 koraka na limitu je stabilno | `dt ≤ limit`, `max d < 1000 m` | 2 |

Fizikalni testovi (jezero, bilans, očuvanje, simetrija, rubovi) se rade za A–G; ekvivalencija i padding za svih 11 varijanti.
Širine koje nisu višekratnik od 8 (13, 37, 127, 257, 29, 61…) namjerno pokrivaju skalarni ostatak u AVX2 petljama.

---

## 12. Rezultati

Rezultati u repozitoriju (`results/`) izmjereni su na virtuelnoj mašini s **2 vCPU** (Intel Xeon @ 2,10 GHz, KVM),
GCC 13.3, Ubuntu 24.04. Mjerenja s 4 i 8 dretvi na toj mašini su zato mjerenja **prekomjernog opterećenja**
(oversubscription), a ne skaliranja. Za rezultate na vlastitom računaru pokrenuti `scripts\demo.cmd` (Windows) ili
`scripts/run_benchmarks.sh` (Linux/macOS).

### Linux VM (GCC 13.3, 2 vCPU), mreža 1024², medijan od 10 ponavljanja

| Varijanta | dretve | medijan [ms] | Mcell/s | ubrzanje naspram A |
|---|---|---|---|---|
| A_scalar | 1 | 220,2 | 38,1 | 1,00 |
| A2_scalar_branchless | 1 | 193,4 | 43,4 | 1,14 |
| B_auto | 1 | 34,6 | 242,7 | 6,37 |
| C_omp_simd | 1 | 34,9 | 240,2 | 6,30 |
| D_avx2 | 1 | 35,1 | 239,0 | 6,27 |
| E_omp | 2 | 115,4 | 72,7 | 1,91 |
| F_omp_simd | 2 | 21,2 | 396,2 | 10,40 |
| H_omp_auto | 2 | 18,3 | 457,6 | 12,01 |
| **G_omp_avx2** | **2** | **16,7** | **502,4** | **13,19** |

### Windows (Intel Core i5-9400, 6 jezgri, MSVC 19.51), brzi režim `demo.cmd`, mreža 512², 3 ponavljanja

| Varijanta | dretve | medijan [ms] | Mcell/s | ubrzanje naspram A |
|---|---|---|---|---|
| A_scalar | 1 | 274,4 | 30,6 | 1,00 |
| B_auto | 1 | 247,8 | 33,8 | 1,11 |
| D_avx2 | 1 | 35,2 | 238,6 | 7,81 |
| E_omp | 6 | 49,0 | 171,3 | 5,60 |
| F_omp_simd | 1 / 6 | 248,8 / 45,7 | 33,7 / 183,5 | 1,10 / 6,01 |
| G_omp_avx2 | 1 / 2 / 4 / 6 | 36,6 / 18,2 / 12,0 / 11,1 | 229 / 461 / 699 / 754 | 7,50 / 15,08 / 22,85 / **24,67** |

### Šta iz njih slijedi

- **Vektorizacija sama** daje ≈ 6–8× (maksimum za 8 × `float` je 8×; razlika dolazi od dijeljenja, korijena i prometa
  prema memoriji – [odjeljak 5.4](#54-d--avx2-intrinsics-kernels_avx2cpp)).
- **Dretve** skaliraju skoro linearno dok ima fizičkih jezgri (efikasnost 0,92–1,00 za E i F do 6 dretvi na Windowsu).
  Kombinacija **SIMD + dretve (G)** je najbrža: ≈ 13× na 2 jezgre (Linux) i ≈ 25× na 6 jezgri (Windows).
- **Na GCC-u** su auto-vektorizacija, `omp simd` i ručni AVX2 praktično jednaki (≈ 6,3×): kad je kod napisan bez
  grananja, s `__restrict` i uz `-fno-trapping-math`, kompajler vektorizuje jednako dobro kao ručni kod.
- **Na MSVC-u to ne važi:** B_auto i F_omp_simd s jednom dretvom daju samo ≈ 1,1×, dok ručni AVX2 daje ≈ 7,8×. Uzrok
  nije analiziran; za istragu postoji opcija `PRS_VECTORIZATION_REPORTS`.
- **Ablacije** (A2, E2/F2, H) pokazuju male razlike: grananje donosi +14 % na 1024², ali −4 % na 512²; jedna OpenMP
  regija naspram fork-join-a nema značajnog efekta.
- **Šum:** na VM-u koeficijent varijacije ide do ≈ 8 %, pa pojedine ćelije (npr. D_avx2 sporiji od B_auto na 512²)
  treba čitati oprezno; 1024² je pouzdanije. Kontrolno mjerenje je u `results/recheck_512_p1.csv`.

---

## 13. Struktura repozitorija

```
CMakeLists.txt     build: biblioteka prs_core, programi prs_swe i prs_tests, zastavice po fajlu
include/           simulation.hpp  – parametri, Fields, Simulation, KernelSet, Driver, Variant, dijagnostika
                   cell_ops.hpp    – skalarna specifikacija jedne ćelije (faze 1–3)
                   aligned_buffer.hpp, sw_common.hpp (makroi), benchmark.hpp (mjerenje i statistika),
                   cpu_features.hpp, io_npy.hpp, build_info.hpp.in
src/               kernels_scalar.cpp (A), kernels_branchless.inl + kernels_auto.cpp (B) +
                   kernels_scalar_branchless.cpp (A2), kernels_omp_simd.cpp (C), kernels_avx2.cpp (D),
                   driver_serial.cpp, driver_openmp.cpp, variants.cpp (registar),
                   simulation.cpp (stanje, CFL, dijagnostika), terrain.cpp (početni teren),
                   benchmark.cpp, cpu_features.cpp (CPUID, FTZ/DAZ), io_npy.cpp, main.cpp (info|bench|simulate|stream)
tests/             correctness_tests.cpp
scripts/           demo.cmd, demo.ps1          – Windows demonstracija (piše u demo_out/)
                   run_benchmarks.sh, system_info.sh – Linux/macOS postupak (piše u results/)
                   plot_results.py, visualize_simulation.py – grafikoni
                   docgen/                     – generator Word dokumenta rada (Node.js)
docs/              literature_review.md (pregled literature 2020–danas)
references/        bibliography.bib
results/           referentna mjerenja (Linux VM): CSV i TXT, vectorization/ (izvještaji GCC/Clang, disasembler)
```

**Generisani artefakti** (nastaju iz koda i skripti; nisu namijenjeni verzionisanju u gitu): `build/`, `build-vs/`,
`out/`, `.vs/`, `demo_out/`, `results/figures/*.png`, `results/sim512/*.npy`, kao i Word dokument rada
`docs/PRS_Shallow_Water_Terrain_Erosion_CPU.docx`.

Word dokument se regeneriše iz `results/` (Node.js i paket `docx`, koji treba instalirati ručno – nema `package.json`):

```bash
npm install docx
node scripts/docgen/build_document.js results docs/PRS_Shallow_Water_Terrain_Erosion_CPU.docx
```

(Pri otvaranju u Wordu prihvatiti ažuriranje polja da se popuni automatski sadržaj.) Generator čita `results/*.csv` i
`results/vectorization/disassembly_summary.txt`; bez njih ne može da radi.

---

## 14. Poznate napomene i ograničenja

- **Dva izlazna foldera:** `scripts\demo.cmd` piše u `demo_out\`, a `run_benchmarks.sh` i podrazumijevane vrijednosti
  opcija `--out` pišu u `results\`. Podaci iz `results\` (Linux VM) se demoom ne obnavljaju.
- **`results/vectorization/`:** `run_benchmarks.sh` piše `compiler_report.txt`, a sačuvani fajlovi (`gcc_report_v1_initial`,
  `gcc_report_final`, `clang18_report`, `disassembly_summary`) su dobijeni ručno; `gcc_report_v1_initial.txt` je izvještaj
  ranije verzije koda.
- **MSVC:** izgradnja i svih 335 testova prolaze (`/openmp:experimental`, `/arch:AVX2`), ali auto-vektorizacija i
  `omp simd` ovdje ne daju ubrzanje (odjeljak 12). Mjerenja za rad rađena su s GCC-om na Linuxu.
- **`demo.ps1`, korak 6:** ispis kaže „30 s simuliranog vremena", a 6000 koraka × 0,05 s je **300 s**.
- **Procjena propusnosti** (`est_GBps`) je model prometa, ne mjerenje; za manje mreže radni skup stane u keš pa
  vrijednost može premašiti STREAM.
- **Samo CPU:** GPU (CUDA) faza nije dio ovog repozitorija; arhitektura (SoA, dvostruki bafer terena, halo, maske
  zidova, skalarna referenca kao specifikacija) je projektovana da se preslika na CUDA bez promjene numeričkog modela.

---

## 15. Literatura

- X. Mei, P. Decaudin, B.-G. Hu, *Fast Hydraulic Erosion Simulation and Visualization on GPU*, Pacific Graphics 2007,
  doi:10.1109/PG.2007.15 – model virtuelnih cijevi i erozija zasnovana na kapacitetu (osnova implementacije).
- Pregled radova 2020–danas: [`docs/literature_review.md`](docs/literature_review.md); BibTeX: [`references/bibliography.bib`](references/bibliography.bib).
