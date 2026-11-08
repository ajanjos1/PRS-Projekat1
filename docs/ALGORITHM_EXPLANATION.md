# Objašnjenje algoritma – hidraulička erozija metodom virtuelnih cijevi

Ovaj dokument objašnjava **šta tačno računa** `prs_erosion` (faza 2, serijska implementacija): matematički model,
jednačine, jedinice, vremensku diskretizaciju, rubne uslove i način na koji je ispravnost provjerena. Uz svaku
jednačinu je naveden fajl i funkcija u kojoj se nalazi, tako da se tekst može pratiti direktno u kodu.

Sadržaj:

1. [Pregled: od slike do erodirane slike](#1-pregled-od-slike-do-erodirane-slike)
2. [Varijable i jedinice](#2-varijable-i-jedinice)
3. [Ulaz: height mapa i fizička skala](#3-ulaz-height-mapa-i-fizička-skala)
4. [Kiša](#4-kiša)
5. [Metoda virtuelnih cijevi (tok vode)](#5-metoda-virtuelnih-cijevi-tok-vode)
6. [Erozija, depozicija i transport sedimenta](#6-erozija-depozicija-i-transport-sedimenta)
7. [Vremenska diskretizacija](#7-vremenska-diskretizacija)
8. [Rubni uslovi](#8-rubni-uslovi)
9. [Izlaz](#9-izlaz)
10. [Kako je provjerena ispravnost](#10-kako-je-provjerena-ispravnost)
11. [Razlike u odnosu na Mei (2007) i Šťava (2008)](#11-razlike-u-odnosu-na-mei-2007-i-šťava-2008)
12. [Ograničenja modela](#12-ograničenja-modela)
13. [Priprema za paralelizaciju (naredna faza)](#13-priprema-za-paralelizaciju-naredna-faza)
14. [Literatura](#14-literatura)

---

## 1. Pregled: od slike do erodirane slike

```
 PNG/JPEG ──► HeightmapIO ──► normalizovane vrijednosti v ∈ [0,1] ──► TerrainScale ──► teren b(x,y) [m]
                                                                                            │
 kiša [L/m²], trajanje [h] ──► RainfallEvent (intenzitet r [m/s]) ─────────────────────────┤
                                                                                            ▼
                         ErosionSimulation: ponavljaj korake Δt (adaptivno) do T_kiše + T_oticanja
                           1. kiša  2.–4. protoci (virtuelne cijevi)  5.–6. voda i brzina
                           7.–8., 10. kapacitet, erozija/depozicija, novi teren   9. transport sedimenta
                                                                                            │
                                                                                            ▼
            ResultExport: erodirana height mapa (16-bitni PNG, ISTA skala) + razlika + izvještaj
```

Numerički dio (`VirtualPipesSolver`, `HydraulicErosion`, `ErosionSimulation`) ne zna ništa o slikama ni
konzoli: prima apstraktni teren (`Heightmap` = mreža visina u metrima + skala). Zato budući generator terena
može dati isti `Heightmap` bez ikakve izmjene solvera.

## 2. Varijable i jedinice

Sve se računa u SI jedinicama, u `double` preciznosti (razlog: visine reda 100 m mijenjaju se za 10⁻⁶ m po
koraku; `float` s 24 bita mantise na 100 m ima korak ≈ 8·10⁻⁶ m i takve promjene bi se izgubile).

| Oznaka | U kodu | Značenje | Jedinica |
|---|---|---|---|
| l | `cellSize` | dužina stranice ćelije (lx = ly = l), ujedno dužina virtuelne cijevi | m |
| b | `terrain` | visina terena (dno) | m |
| d | `water` | dubina vode | m |
| η = b + d | – | visina vodene površine | m |
| s | `sediment` | suspendovani sediment, izražen kao **debljina sloja materijala terena** koji je u vodi | m (= m³/m²) |
| f^L, f^R, f^T, f^B | `fluxLeft/Right/Top/Bottom` | izlazni zapreminski protok kroz lijevu/desnu/gornju/donju cijev | m³/s |
| u, v | `velocityX/Y` | brzina vode u smjeru +x (desno) i +y (dolje) | m/s |
| r | `intensityMetersPerSecond()` | intenzitet kiše | m/s |
| g | `gravity` | gravitaciono ubrzanje (9.81) | m/s² |
| n | `manningN` | Manningov koeficijent hrapavosti | s/m^(1/3) |
| K_c | `capacityCoefficient` | koeficijent kapaciteta transporta | s/m |
| K_s, K_d | `dissolvingRate`, `depositionRate` | brzina erozije i taloženja | 1/s |
| Δt | `dt` | vremenski korak | s |

**Zašto je s izražen kao visina materijala?** Tada su b i s ista fizička veličina (debljina stuba istog
materijala po jedinici površine) i zbir `Σ (b + s) · l²` je zapremina materijala u m³ – nema sabiranja
nekompatibilnih veličina (mase i zapremine, ili gustina sedimenta i vode). Model ne koristi gustine: sve što se
erodira, prenosi i taloži mjeri se u m³ materijala terena.

## 3. Ulaz: height mapa i fizička skala

`src/HeightmapIO.cpp` (`loadRaster`, `loadHeightmap`), `src/Heightmap.cpp` (`makeHeightmap`).

- Format se prepoznaje iz **sadržaja** (potpis PNG `89 50 4E 47…`, JPEG `FF D8 FF`), a ekstenzija mora biti
  `.png`, `.jpg` ili `.jpeg`.
- Slika se dekodira bibliotekom stb_image (v2.30, MIT/Public Domain) uvijek kao 16 bita po kanalu:
  16-bitni PNG se čuva bez gubitka; 8-bitna vrijednost v postaje 257·v, pa je `v16/65535 = v8/255` tačno.
- Siva vrijednost: 1 ili 2 kanala (siva, siva+alfa) → sivi kanal; RGB(A) → ako je R = G = B u svim pikselima,
  uzima se R (bez gubitka), inače luma po ITU-R BT.709: `Y = 0.2126 R + 0.7152 G + 0.0722 B`. Alfa se zanemaruje.
- Normalizacija: `v = vrijednost / 65535 ∈ [0, 1]` (crno = 0 = najniže, bijelo = 1 = najviše; opcija
  `--invert` / pitanje u meniju zamjenjuje konvenciju).
- Fizička skala (`TerrainScale`): `b = minElevation + v · (maxElevation − minElevation)`, a veličina ćelije
  je `cellSize`. Podrazumijevano: **ćelija 30 m** (uobičajena rezolucija globalnih DEM-ova), **0 – 100 m**. Ovo
  su pretpostavke modela, ne stvarne geografske dimenzije – slika ih ne sadrži.
- **JPEG** je kompresija s gubicima: blokovi 8×8 i "zvonjenje" oko oštrih ivica postaju lažne neravnine
  terena koje voda "vidi". Program to prijavljuje u izvještaju. Za ozbiljan rad preporučuje se 16-bitni PNG.

## 4. Kiša

`include/Rainfall.hpp`, `src/Rainfall.cpp`.

```
1 L/m² = 1 mm vodenog stuba = 0.001 m
intenzitet  r = ukupno [mm] / trajanje [h]            [mm/h]
            r [m/s] = r [mm/h] · 0.001 / 3600
primjer:    100 L/m² za 5 h → 20 mm/h = 5.556·10⁻⁶ m/s, ukupno simulirano vrijeme kiše 18000 s
```

U svakom koraku dodaje se **samo voda koja padne u tom koraku** – preklop intervala koraka s periodom kiše:

```
Δd_kiša = r · | [t, t + Δt] ∩ [0, T_kiše] |          (RainfallEvent::depthAddedBetween)
```

Zato je zbir po svim koracima tačno jednak ukupnoj kiši, bez obzira na to koliki su (adaptivni) koraci, i kiša
nikad ne "curi" u period oticanja. Nije dodavana ni ukupna količina po koraku, ni ukupna količina po satu.
Kiša je ravnomjerna u prostoru (ista na svakoj ćeliji).

Validacija unosa (`validateRainfallInput`, `ConsoleInput`): količina ≥ 0, trajanje > 0 (nema dijeljenja nulom),
konačni brojevi (NaN/Inf odbijeni), dozvoljen decimalni zarez i tačka, neispravan unos ponavlja pitanje umjesto
da sruši program. Period oticanja je **zaseban** parametar (`runoffHours`, podrazumijevano 0) i nije dio trajanja
kiše.

## 5. Metoda virtuelnih cijevi (tok vode)

`include/VirtualPipesSolver.hpp`, `src/VirtualPipesSolver.cpp`.

### 5.1 Diskretizacija

Teren je pravougaona mreža W × H ćelija (W = širina slike, H = visina; ne mora biti kvadratna). Ćelija (x, y)
je piksel (x, y); x raste udesno, y nadolje, indeks `i = y·W + x`. Svaka ćelija je virtuelnim cijevima povezana
sa 4 susjeda (lijevo, desno, gore, dolje) i za **svaku cijev pamti svoj izlazni protok** f ≥ 0 (O'Brien & Hodgins
1995; Mei i dr. 2007). Dvije susjedne ćelije tako imaju dvije cijevi u suprotnim smjerovima.

### 5.2 Ubrzanje vode u cijevi

Voda teče zbog razlike hidrostatičkog pritiska na krajevima cijevi, tj. zbog razlike visina **vodenih površina**
(ne samo terena):

```
Δη = (b_i + d_i) − (b_j + d_j)          ΔP = ρ g Δη          a = ΔP / (ρ l) = g Δη / l
f_novo = f + Δt · A_cijevi · a = f + Δt · A_cijevi · g · Δη / l          (Mei 2007, jed. 2–3; Šťava 2008, jed. 1)
```

Mei koristi konstantan presjek cijevi A_cijevi. Mi uzimamo **stvarni protočni presjek** između dvije ćelije:

```
h_f = max(η_i, η_j) − max(b_i, b_j)        (dubina vode iznad višeg od dva dna)
A_cijevi = l · h_f          →          f_novo = f + Δt · g · h_f · Δη
```

Razlog: s konstantnim A tanak sloj vode od 1 mm i rijeka od 1 m imali bi isti otpor cijevi, pa bi brzina tankog
sloja bila nefizikalno velika. Sa A = l·h_f jednačina je upravo diskretizacija jednačine količine kretanja plitke
vode bez advekcijskog člana (lokalna inercijalna aproksimacija, Bates i dr. 2010).

### 5.3 Trenje o podlogu (Manning)

Bez trenja voda na padini ubrzava neograničeno (Mei model nema trenje; za vizuelnu GPU demonstraciju od nekoliko
sekundi to nije bitno, ali za simulaciju koja traje satima fizičkog vremena jeste). Dodaje se trenje po Manningu,
**poluimplicitno** (Bates i dr. 2010):

```
               f + Δt · g · h_f · Δη
f_novo = ───────────────────────────────────          q = f / l  (jedinični protok, m²/s)
          1 + Δt · g · n² · |f| / (l · h_f^(7/3))
```

U ravnoteži (f_novo = f) ovo daje Manningovu formulu `q = h^(5/3) · √S / n`, gdje je S = Δη / l nagib vodene
površine. Imenilac je uvijek ≥ 1, pa trenje ne može promijeniti znak protoka niti izazvati nestabilnost ni za
vrlo plitku vodu (zato "poluimplicitno": |f| je iz starog koraka, ali se dijeli novim izrazom). Podrazumijevano
n = 0.04 (trava/obradivo tlo); n = 0 vraća Mei model bez trenja. `h^(7/3) = h² · ∛h`; kubni korijen se računa
funkcijom `cubeRootPositive` (`include/MathUtils.hpp`: početna procjena iz IEEE-754 bitova + 2 Halleyeve iteracije,
relativna greška < 10⁻¹³ – test `Erosion/kubni_korijen`), jer je `std::cbrt` bila najskuplja operacija simulacije.

Izlazni protok ne može biti negativan: `f_novo = max(0, f_novo)` – cijev vodi samo VAN ćelije. Ako nema ni
protoka ni pada prema susjedu (f = 0 i Δη ≤ 0), protok ostaje 0 bez računanja.

### 5.4 Ograničenje odljeva dostupnom vodom

U jednom koraku ćelija ne smije poslati više vode nego što ima (Mei 2007, jed. 4):

```
K = min(1, d1 · l² / ((f^L + f^R + f^T + f^B) · Δt))          f ← K · f   (za sve 4 cijevi)
```

Posljedica: dubina vode nikad ne postaje negativna (osim zaokruživanja reda 10⁻¹⁷ m, koje se evidentira u
bilansu kao `negativeDepthCorrection` i u testovima je < 10⁻⁹ ukupne vode).

### 5.5 Nova dubina vode

```
d2 = d1 + Δt · (Σ f_ulaz − Σ f_izlaz) / l²                  (Mei 2007, jed. 5–6)
Σ f_ulaz = f^R(x−1, y) + f^L(x+1, y) + f^B(x, y−1) + f^T(x, y+1)
```

**Očuvanje vode:** svaki protok f se jednom oduzima ćeliji koja ga šalje, a jednom dodaje ćeliji koja ga prima,
pa se pri sumiranju po cijeloj mreži svi članovi poništavaju; ostaju samo cijevi koje vode van mreže (kod
zatvorenog ruba su 0). Dakle `Σ d2 · l² = Σ d1 · l² − izlaz kroz rub`, tačno u aritmetici; u računaru greška je
reda zaokruživanja (test 7: najveća relativna greška po koraku 4.9·10⁻¹⁵).

### 5.6 Brzina vode

Iz neto protoka kroz ćeliju (Mei 2007, jed. 7–9):

```
ΔW_x = ½ · (f^R(x−1) − f^L(x) + f^R(x) − f^L(x+1))       [m³/s]
ΔW_y = ½ · (f^B(y−1) − f^T(y) + f^B(y) − f^T(y+1))
d̄ = ½ (d1 + d2)
u = ΔW_x / (l · d̄),    v = ΔW_y / (l · d̄)                 ako je d̄ > 10⁻⁴ m, inače u = v = 0
```

Prag 0.1 mm sprječava dijeljenje (skoro) nulom u ćelijama koje su suhe ili gotovo suhe.

## 6. Erozija, depozicija i transport sedimenta

`include/HydraulicErosion.hpp`, `src/HydraulicErosion.cpp`.

### 6.1 Kapacitet transporta

Mei (2007, jed. 10): `C = K_c · sin α · |v|` (α je lokalni nagib). Kod nas:

```
c_cap = min( K_c · max(sin α, sin_min) · |v| ,  c_max )         zapreminska koncentracija [-]
C     = c_cap · d                                                [m]
sin α = |∇η| / √(1 + |∇η|²),   ∇η iz centralnih razlika η = b + d (jednostrane na rubu mreže)
```

Tri razlike u odnosu na Mei i zašto:

1. **Množenje dubinom d.** Proizvod |v| · sin α je "unit stream power" (Yang 1972) – veličina od koje u
   klasičnim formulama transporta zavisi **koncentracija** sedimenta koju tok može nositi. Koncentracija puta
   dubina daje količinu materijala po jedinici površine (m), istu jedinicu kao s. Tanak film vode zato nosi malo,
   a dublji brz tok više – što odgovara stvarnosti (Mei-ov C ne zavisi od dubine, pa i film od 0.1 mm erodira
   kao rijeka).
2. **Nagib vodene površine umjesto nagiba terena.** Na padini s tankim slojem vode ∇η ≈ ∇b (isto kao kod Mei),
   ali u jezeru je površina ravna pa je kapacitet ≈ 0. S nagibom terena bi duboka jama s vodom (npr. 5 m) imala
   ogroman kapacitet `c · 5 m` i "kopala" dno – to je u ranoj verziji koda stvaralo lokalne rupe od nekoliko metara.
3. **Gornja granica koncentracije** c_max = 0.1 (10 % zapremine) – iznad toga tok više nije voda sa
   sedimentom nego blatni tok, koji ovaj model ne opisuje.

Podrazumijevano: K_c = 0.5 s/m, sin_min = 0 (na ravnom nema erozije – test 2), c_max = 0.1.

### 6.2 Erozija i depozicija

Količina u vodi se približava kapacitetu brzinom K_s (erozija) odnosno K_d (taloženje), `ds/dt = K (C − s)`.
Za konstantno C tokom koraka ova jednačina ima tačno rješenje, pa koristimo njega:

```
C > s (erozija):     Δ = (1 − e^(−K_s Δt)) · (C − s),   Δ ≤ b − b_stijena,     b ← b − Δ,  s ← s + Δ
C < s (depozicija):  Δ = (1 − e^(−K_d Δt)) · (s − C),                            b ← b + Δ,  s ← s − Δ
```

- Materijal se samo **premješta** između terena i vode (b + s se ne mijenja) – osnova očuvanja materijala.
- Faktor `1 − e^(−KΔt)` je uvijek u [0, 1): s nikad ne "preskoči" kapacitet, za bilo koji Δt (test
  `relaksacija_ne_preskace_kapacitet`). Mei koristi `K_s · (C − s)` po koraku bez Δt, pa rezultat zavisi od toga
  koliko koraka se izvrši.
- **Erodibilni sloj** (podrazumijevano 5 m): teren ne može pasti ispod `b0 − 5 m` ("stijena"). To je
  ograničenje erozije dostupnim materijalom (spec. 8.2).
- Podrazumijevano K_s = K_d = 0.02 1/s (vremenska konstanta 50 s).

### 6.3 Transport sedimenta

Sediment putuje **kroz iste virtuelne cijevi kao voda** (konzervativna "upwind" šema konačnih zapremina).
Ćelija kroz svaku cijev šalje dio sedimenta jednak dijelu vode koji je kroz tu cijev otišao:

```
s_kroz_cijev = s · (f · Δt) / (d1 · l²)          (koncentracija s/d1 × zapremina vode f·Δt / površina)
s_novo = s − Σ s_kroz_izlazne_cijevi + Σ s_primljeno_od_susjeda
```

- **Očuvanje:** šta jedna ćelija pošalje, susjed tačno primi (isti broj se oduzima i dodaje), pa je `Σ s`
  očuvana do zaokruživanja; kroz otvoreni rub izlazi i evidentira se (`sedimentBoundaryOutflow`).
- **Nenegativnost:** zbog K faktora (5.4) je `Σ f · Δt ≤ d1 · l²`, pa ćelija šalje najviše sav sediment koji ima.
- Mei i Šťava koriste semi-Lagrangeovu advekciju (`s(x) ← s(x − v·Δt)`), koja **nije** konzervativna – količina
  sedimenta tada nije tačno očuvana, što bi narušilo zahtjev 8.5 specifikacije.

### 6.4 Taloženje na kraju simulacije

Kad simulacija završi, sediment koji je još u vodi taloži se na mjestu gdje se nalazi (`b ← b + s`, `s ← 0`).
Inače bi taj materijal nestao iz izlazne height mape. Iznos se prijavljuje (`settledAtEndVolume`); opcija
`--no-settle` to isključuje.

### 6.5 Bilans materijala

```
M(t) = Σ (b − b0) · l² + Σ s · l² + izneseno_kroz_rub = 0     (u svakom koraku, u egzaktnoj aritmetici)
```

Sabiraju se razlike `b − b0`, a ne apsolutne visine, da greška zaokruživanja ne zavisi od nadmorske visine.
Test 8: najveća greška po koraku 1·10⁻⁸ m³ pri bruto eroziji 479 m³ (relativno 2·10⁻¹¹).

## 7. Vremenska diskretizacija

`src/ErosionSimulation.cpp` (`step`, `run`).

### 7.1 Redoslijed u jednom koraku

| Korak (spec. 9) | Šta | Funkcija | Čita kod susjeda | Piše |
|---|---|---|---|---|
| 1 | kiša `d += Δd_kiša` | `VirtualPipesSolver::addUniformWater` | – | d |
| 2–4 | η = b + d, protoci kroz cijevi, trenje, ograničenje K | `computeOutflowFlux` | b, d | f (svoje), d1 |
| 5–6 | nova dubina d2, brzine u, v | `updateWaterAndVelocity` | f | d, u, v |
| 7, 8, 10 | kapacitet, erozija/depozicija, novi teren | `HydraulicErosion::erodeAndDeposit` | b, d (za nagib) | b_next → zamjena, s |
| 9 | transport sedimenta | `transportSediment` | s, f, d1 | s_next → zamjena |
| – | gubici (ako su uključeni) | `applyLosses` | – | d |

Redoslijed prati Mei (2007): voda → brzina → erozija → transport. Erozija koristi brzinu iz istog koraka.

### 7.2 Dvostruko baferovanje (double buffering)

Unutar jedne faze svaka ćelija piše samo **svoje** vrijednosti, a čita i susjedne. Polje koje se u istoj fazi
čita kod susjeda i piše za ćeliju ima drugi bafer: teren u fazi erozije (`terrain` → `terrainNext`, nagib se
računa iz susjednih visina) i sediment u fazi transporta (`sediment` → `sedimentNext`). Faza čita samo stari, a piše
samo novi bafer; na kraju se baferi zamijene u O(1) (`Grid2D::swap`). Tako nijedna ćelija ne vidi djelimično
ažuriranog susjeda, a rezultat ne zavisi od redoslijeda obilaska ćelija.

Protoci se ažuriraju na mjestu jer ćelija čita samo **svoj** stari protok, a susjedima čita b i d (koji se u toj
fazi ne mijenjaju). Dubina d se u fazi 5–6 piše na mjestu jer se čitaju samo protoci susjeda.

### 7.3 Adaptivni vremenski korak (stabilnost)

Šema je eksplicitna, pa Δt mora zadovoljiti uslov stabilnosti (CFL). Koristi se:

```
Δt = α · l / max_ćelije( 2 · √(g · d) + max(|u|, |v|) ),      α = 0.7,   Δt ≤ 10 s
```

- **√(g d)** je brzina gravitacionog talasa u plitkoj vodi. **Faktor 2** dolazi iz Mei-ove strukture s dvije
  izlazne cijevi po paru ćelija: kad su obje aktivne (npr. kada se voda u bari zaljulja), neto protok se mijenja
  dvostruko brže od jedne cijevi. Von Neumannova analiza "šahovskog" moda u 2D tada daje granicu
  `Δt ≤ l / (2 √(g d))` umjesto `l / √(2 g d)` standardne šeme. Ovo je provjereno i numerički: duboka bara
  (5 m) s poremećajem "eksplodira" za manje od 20 koraka sa `Δt = 0.7 l/√(g d)`, a sa faktorom 2 ostaje stabilna
  (test `Water/stabilnost_duboke_bare`). U ranoj verziji bez faktora 2 ovo se na stvarnoj mapi pokazalo kao
  lokalna rupa od skoro 5 m u bari dubokoj 20 m.
- **|u|**: voda ne smije u jednom koraku preći više od dijela ćelije. Bez ovog člana rezultat zavisi od Δt
  (provjereno: erozija se razlikovala ~20 % između α = 0.7 i 0.35); s njim je razlika 0.14 % (test
  `Erosion/konvergencija_u_vremenskom_koraku`).
- Δt se računa iz stanja prethodnog koraka i smije rasti najviše 25 % po koraku.
- Posljednji korak kiše skraćuje se tako da tačno pogodi kraj kiše (i kraj simulacije), pa se faze ne miješaju.
- Ako stabilan Δt padne ispod 10⁻⁴ s, simulacija se prekida s porukom (zaštita od beskonačnog rada).
- Kad nigdje nema vode i do kraja faze ne pada kiša (npr. 0 L/m²), stanje se ne može promijeniti osim
  taloženja eventualnog sedimenta, čiji je izraz `1 − e^(−K_d Δt)` tačan za bilo koji Δt – pa se ostatak faze
  prelazi u jednom koraku.

Δt je **fizičko simulirano vrijeme** (npr. 18000 s za 5 h kiše), a ne vrijeme izvršavanja; jedan korak nije
"jedna sekunda". Broj koraka zavisi od veličine ćelije, dubine vode i brzine (tipično Δt ≈ 1–3 s za ćelije od
30 m).

### 7.4 Period oticanja

Nakon kiše (`t > T_kiše`) simulacija nastavlja bez padavina još `runoffHours` sati: voda se slijeva u niže
dijelove, a sediment se taloži kako tok slabi. To je zaseban parametar i prikazuje se odvojeno u sažetku,
napretku ("rain" / "runoff") i izvještaju.

## 8. Rubni uslovi

| Uslov | Cijevi na rubu | Bilans |
|---|---|---|
| `closed` (podrazumijevano) | ne postoje (zid): f = 0 | zatvoren sistem: kiša = voda na terenu |
| `open` | susjed izvan mreže ima **isto dno** kao rubna ćelija i nema vode (slobodan izliv) | kiša = voda + izlaz kroz rub (evidentira se); sediment izlazi s vodom |

Nagib terena/vodene površine na rubu računa se jednostranom razlikom. Kod zatvorenog ruba voda se skuplja uz
rub (kao uz zid); za isječke terena iz većeg područja realniji je `open`.

## 9. Izlaz

`src/ResultExport.cpp`.

- `<ime>_eroded.png`: **16-bitni sivi PNG** istih dimenzija i orijentacije, `v16 = round((b − min)/(max − min) · 65535)`
  – **ista skala kao ulaz**, bez nezavisne normalizacije. Bez boje vode, bez filtera: to su visine terena iz
  simulacije. Ako je ulaz invertovan, izlaz koristi istu (invertovanu) konvenciju.
- Vrijednosti izvan `[min, max]` (npr. depozicija na najvišem vrhu) se u PNG-u odsijeku na 0/65535 **uz upozorenje**
  u konzoli i izvještaju; tačne vrijednosti su uvijek u `<ime>_eroded_m.npy` (float64).
- 16-bitni PNG koder je vlastit (`src/ImageWriter.cpp`: IHDR/IDAT/IEND, CRC32, PNG filteri), jer stb_image_write
  podržava samo 8 bita; od stb se koristi samo zlib kompresija.
- `<ime>_original.png` – ulaz u istom formatu (isti tok obrade), `<ime>_difference.png` – Δh u divergentnoj
  skali boja (crveno = sniženje, plavo = povišenje, skala ±99. percentil |Δh|, navedena u izvještaju),
  `<ime>_comparison.png` – sjenčeni reljef originala i rezultata jedan do drugog (isti z-faktor za obje
  polovine), `<ime>_water_max_depth.png` – najveća dubina vode (gdje su tekli tokovi).
- `.npy` (float64): original, rezultat, razlika, najveća dubina vode; opciono `--csv` (17 značajnih cifara).
- `simulation_report.txt` i `.json`: svi parametri, broj koraka, Δt, vrijeme izvršavanja, bilansi vode i
  materijala, statistika promjene terena, provjere valjanosti, upozorenja.
- Ulazna datoteka se nikad ne mijenja; ako bi izlaz imao istu putanju kao ulaz, izvoz se odbija.

## 10. Kako je provjerena ispravnost

Program `prs_erosion_tests` (35 testova, 228 provjera; `tests/README.md`). Brojke ispod su iz stvarnog
pokretanja (GCC 13, Linux, Release); svaki put se ponovo izračunaju.

| Test (spec. 14) | Postavka | Šta se provjerava | Rezultat |
|---|---|---|---|
| 1 – bez kiše | hrapav teren, 0 L/m², 3 h + 1 h | teren bit-identičan, nema vode ni erozije | prolazi (2 koraka – suh teren se preskače) |
| 2 – ravan teren | 40×30, ravno, 80 mm / 2 h, zatvoreno | svi protoci i brzine tačno 0, voda svuda tačno 80 mm, teren nepromijenjen | prolazi |
| 3 – nagib | ravan pad 40 m na 790 m + šum, 60 mm / 1 h | voda teče niz nagib (ū > 0), > 90 % vode na nižoj četvrtini; padina se snižava, podnožje povisuje | ū = 0.37 m/s, 99 % vode dolje; padina −77 m³, podnožje +107 m³ |
| 4 – planina | sintetička planina 96×96 i stvarna mapa (primjer_1) | erozija lokalizovana (10 % ćelija nosi > 50 % sniženja), jače erodirane ćelije imaju dublju vodu, depozicija niže od erozije | 72 %; 0.078 > 0.072 m; 19.8 m < 29.5 m |
| 5 – količina kiše | ista planina, 10/50/100/200 mm za 1 h | bilansi, valjanost; više kiše → dublja voda i više erozije (bez zahtjeva proporcionalnosti) | sniženje 148 / 1202 / 2585 / 5555 m³ (nelinearno: 20× kiše → 38× erozije) |
| 6 – trajanje | 100 mm za 1/5/10 h, nagib, otvoren rub | vršni izlazni protok opada s trajanjem; ne prelazi intenzitet × površina | 5.35 / 1.07 / 0.53 m³/s |
| 7 – očuvanje vode | hrapav teren, zatvoreno, s erozijom | `voda = kiša` poslije **svakog** koraka | relativna greška ≤ 4.9·10⁻¹⁵ |
| 8 – očuvanje materijala | zatvoreno i otvoreno | `Σ(b − b0) + Σ s (+ izlaz) = 0` u svakom koraku | ≤ 1·10⁻⁸ m³ (rel. 2·10⁻¹¹) |
| 9 – stabilnost | strm hrapav teren, 200 mm / 1 h + 1 h | nema NaN/Inf, d ≥ 0, s ≥ 0, sniženje ≤ erodibilni sloj, nema eksplozije brzine | prolazi (26 468 koraka) |
| 10 – uvoz/izvoz | 16-bit, 8-bit, RGB, siva+alfa, JPEG, nekvadratne mape 37×23 i 45×28, primjeri iz priloga | tačne vrijednosti, dimenzije, orijentacija, ista skala, ulaz netaknut, zaštita od prepisivanja | prolazi |

Dodatno: jezero u mirovanju na neravnom dnu (protok tačno 0 – dokaz da se koristi η, a ne b), svojstva cijevi,
kapacitet, relaksacija bez preskakanja, stabilnost duboke bare, konvergencija u Δt, kubni korijen, parsiranje i
validacija unosa (simulirani korisnik koji upisuje tekst i negativne brojeve), bilans s infiltracijom i
isparavanjem.

Rezultati na tri priložene height mape (100 L/m², podrazumijevana skala) su u README-u (odjeljak "Izmjereni
rezultati") i dobijaju se skriptom `scripts/run_experiments.py`.

## 11. Razlike u odnosu na Mei (2007) i Šťava (2008)

| Element | Mei 2007 / Šťava 2008 | Ova implementacija | Razlog |
|---|---|---|---|
| Tok vode | virtuelne cijevi, izlazni protok po cijevi | **isto** | – |
| Presjek cijevi | konstantan A (Šťava: l²) | A = l · h_f (stvarna dubina toka) | tanak film i dubok tok ne smiju imati isti otpor |
| Trenje | nema | Manning, poluimplicitno (Bates 2010) | realne brzine u satima fizičkog vremena |
| Ograničenje odljeva K | da (jed. 4) | **isto** | – |
| Brzina | ΔW/(l·d̄) | **isto** | – |
| Kapacitet | K_c · sin α · \|v\| | K_c · sin α · \|v\| · d, α iz η, gornja granica c_max | jedinice (m), duboka mirna voda ne kopa |
| Erozija/depozicija | K_s(C − s), K_d(s − C) po koraku | (1 − e^(−KΔt))(C − s) | nezavisno od broja koraka, bez preskakanja |
| Transport sedimenta | semi-Lagrangeova advekcija (Šťava: MacCormack) | konzervativni upwind kroz cijevi | tačno očuvanje materijala |
| Isparavanje | d(1 − K_e Δt) | opciono mm/h (podrazumijevano 0) | test 7 traži zatvoren bilans |
| Termalna erozija / klizanje | Šťava: klizanje preko ugla mirovanja | nije implementirano | zahtjev je hidraulička erozija; vidi ograničenja |
| Slojevi materijala | Šťava: više slojeva različite tvrdoće | jedan sloj + "stijena" na dubini 5 m | jednostavnost |
| Δt | fiksan (GPU, interaktivno) | adaptivan (CFL), fizičko vrijeme | kiša u satima |

## 12. Ograničenja modela

- **Pojednostavljena hidraulika:** nema advekcijskog člana (u·∇)u u jednačini kretanja (lokalna inercijalna
  aproksimacija); dobro za tok po terenu i poplave, manje tačno za brze tokove s naglim promjenama (hidraulički
  skok).
- **Empirijska erozija:** K_c, K_s, K_d, c_max su koeficijenti modela (kao kod Mei i Šťave), a ne izmjerena
  svojstva tla; nisu kalibrisani na stvarnim mjerenjima. Apsolutne količine erozije zato treba tumačiti
  kvalitativno; podrazumijevane vrijednosti su odabrane tako da je efekat vidljiv.
- **Skala je pretpostavka:** slika ne nosi stvarne dimenzije; rezultati zavise od `cellSize` i visinskog raspona.
- **Jedan materijal**, bez vegetacije, bez infiltracije zavisne od tla (opciona je samo konstantna brzina), bez
  termalne erozije/klizanja kosina.
- **Ravnomjerna kiša** u prostoru i vremenu.
- **Kvantizacija ulaza:** 8-bitna mapa s rasponom 100 m ima korak 0.39 m – "stepenice" su za vodu stvarne
  neravnine; 16-bitni ulaz je mnogo bolji. JPEG dodaje artefakte kompresije.
- **Eksplicitna šema:** Δt je ograničen CFL uslovom; najdublja bara na mapi određuje korak za cijelu mrežu.
  Vrijeme izvršavanja raste s brojem ćelija i trajanjem (serijski: ~45 ns po ćeliji po koraku na testnom
  računaru) – upravo to je motivacija za paralelizaciju u narednoj fazi.
- **Rubovi:** zatvoreni rub pravi "jezera" uz rub; otvoreni je slobodan izliv bez povratnog toka.

## 13. Priprema za paralelizaciju (naredna faza)

Implementacija je serijska, ali je struktura izabrana tako da se može paralelizovati bez promjene numerike:

- **Structure of Arrays** (`SimulationState`): svako polje je jedan kontinuiran niz `double` – pogodno za
  vektorizaciju, za podjelu redova po dretvama i za prenos na GPU (jedan bafer po polju).
- **Svaka faza je jedna petlja po mreži u kojoj ćelija piše samo svoje vrijednosti** (protok, dubina,
  teren/sediment u drugi bafer). Unutar faze su ćelije nezavisne → `#pragma omp parallel for` po redovima ili
  jedan GPU thread po ćeliji; između faza je potrebna barijera (to je i granica GPU kernela).
- **Double buffering** već postoji za polja koja se čitaju kod susjeda – nema "race condition"-a.
- **Redukcije** (najveća brzina talasa za Δt, sume za bilanse) su izdvojene kao vrijednosti koje faza vraća –
  u paralelnoj verziji postaju `reduction(max:…)` / `reduction(+:…)`.
- Solver ne zavisi od konzole ni od biblioteke za slike, pa se može zamijeniti paralelnom verzijom iste klase.
- Kod faze 1 (SIMD/OpenMP benchmark istog modela cijevi) uklonjen je iz radnog stabla, ali ostaje u git historiji
  (commit `78e7b52`) i može poslužiti kao polazna tačka.

## 14. Literatura

- X. Mei, P. Decaudin, B.-G. Hu, "Fast Hydraulic Erosion Simulation and Visualization on GPU", *Pacific
  Graphics 2007*, IEEE, str. 47–56. DOI: 10.1109/PG.2007.15
- O. Šťava, B. Beneš, M. Brisbin, J. Křivánek, "Interactive Terrain Modeling Using Hydraulic Erosion",
  *Eurographics/ACM SIGGRAPH Symposium on Computer Animation (SCA) 2008*, str. 201–210.
  DOI: 10.2312/SCA/SCA08/201-210
- J. F. O'Brien, J. K. Hodgins, "Dynamic Simulation of Splashing Fluids", *Computer Animation '95*, IEEE, 1995
  (porijeklo modela virtuelnih cijevi).
- P. D. Bates, M. S. Horritt, T. J. Fewtrell, "A simple inertial formulation of the shallow water equations for
  efficient two-dimensional flood inundation modelling", *Journal of Hydrology* 387(1–2), 33–45, 2010.
  DOI: 10.1016/j.jhydrol.2010.03.027
- G. A. M. de Almeida, P. D. Bates, J. Freer, M. Souvignet, "Improving the stability of a simple formulation of
  the shallow water equations for 2-D flood modeling", *Water Resources Research* 48(5), W05528, 2012.
  DOI: 10.1029/2011WR011570 (analiza stabilnosti iste klase šema pri malom trenju)
- C. T. Yang, "Unit Stream Power and Sediment Transport", *Journal of the Hydraulics Division (ASCE)* 98(10),
  1805–1826, 1972. DOI: 10.1061/JYCEAJ.0003439
- V. T. Chow, *Open-Channel Hydraulics*, McGraw-Hill, 1959 (Manningova jednačina i vrijednosti n).
- Referentne implementacije pregledane prije izrade (kod nije kopiran): `dandrino/terrain-erosion-3-ways`
  (MIT; čestice/gradijent, nije model cijevi), `juanguillermo-dev/terrain-erosion-simulation` (samo DirectX
  prikaz terena, bez simulacije erozije), `lanlou123/Webgl-Erosion` (MIT; GPU model cijevi Mei 2007 s
  "ping-pong" teksturama, nefizikalne jedinice).
