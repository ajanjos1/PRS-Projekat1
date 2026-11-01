// Poglavlja 5-7 i 10: implementacija, vektorizacija, paralelizacija, priprema za GPU.
const { d, P, H, bullets, numbered, TABLE, CODE, runs } = require("./helpers");
const { cite } = require("./references");
const fs = require("fs");

function implementacija() {
  const items = [];
  items.push(H(1, "Projektovanje i implementacija"));
  items.push(H(2, "Polazište: analiza laboratorijskih primjera"));
  items.push(P("Prije implementacije analizirani su primjeri sa laboratorijskih vježbi (`bench.hpp/cpp`, `benchmark_runner`, `vectorization_*`, `particle_update_*`, `simd_support.hpp`, `memory_kernels.hpp`, `measurement_kernels.hpp`). Primjeri ne simuliraju plitke vode, ali definišu programerski pristup koji je preuzet i prilagođen (Tabela 5). Posebno je uočeno da se na vježbama OpenMP koristi isključivo za SIMD (`#pragma omp simd`, `reduction(+:sum)`), a ne za višedretvenost – u ovom radu te dvije optimizacije su strogo razdvojene u zasebne varijante."));
  const t = TABLE("Principi iz laboratorijskih primjera i njihova primjena u ovom radu", [
    ["Tema", "Kako je realizovano na vježbama", "Primjena u simulaciji"],
    ["Serijska referenca", "`*_scalar.cpp` + `PERF_NO_VECTORIZE` + `-fno-tree-vectorize` po fajlu", "`kernels_scalar.cpp`; isti mehanizam, provjera izvještajem i disasemblerom (GCC 13 nema `GCC novector`, pa je zastavica fajla nužna)"],
    ["Auto-vektorizacija", "jednostavne petlje, `__restrict` (`fn_no_alias`), napomena o redukcijama bez `-ffast-math`", "`kernels_auto.cpp`; restrict parametri, uslovi kao min/max, analiza izvještaja"],
    ["OpenMP SIMD", "`#pragma omp simd`, `reduction(+:sum)`", "`kernels_omp_simd.cpp`; bez redukcija – nisu potrebne"],
    ["AVX2 intrinsics", "`_mm256_loadu_ps`, `cmp` + `blendv` maske, skalarni ostatak, runtime CPUID provjera, fallback", "`kernels_avx2.cpp`; isto, uz poravnate load/store za centralne redove"],
    ["AoS vs SoA", "čestice: `Particle` (32 B) vs `SoA` (samo potrebna polja)", "SoA: devet nizova; svaka faza čita samo potrebna polja"],
    ["Poravnanje", "`AlignedFloats` (32 B), poređenje aligned/unaligned", "`AlignedBuffer` (64 B), pitch višekratnik od 8, red počinje na 32 B"],
    ["Mjerenje", "`measure()`: warm-up, ponavljanja, `steady_clock`, medijan, `consume()`", "`benchmark.hpp`: + `prepare()` izvan mjerenja, min, max, srednja vrijednost, std. devijacija, CSV"],
    ["Ispravnost", "`check_vector`, `verify` sa relativnom tolerancijom", "poređenje svih polja sa referencom (aps. + rel. tolerancija) i fizikalni testovi"],
    ["Lokalnost", "red/kolona obilazak, 6 redoslijeda petlji MatMul, blokiranje", "row-major obilazak, unutrašnja petlja po x, susjedni redovi ostaju u kešu"],
  ], [1.2, 2.6, 3.2], { size: 16 });
  items.push(...t.items);

  items.push(H(2, "Arhitektura C++ projekta"));
  items.push(P("Ključna odluka je razdvajanje *kako* se računa jedan red mreže (skup kernela) od *ko* računa koje redove (driver). Svaki skup kernela sadrži tri funkcije – po jednu za svaku fazu koraka – sa identičnim potpisom, pa se varijante dobijaju kombinacijom (Tabela 6). Tako sve varijante dijele isti interfejs, a efekat dretvi i efekat SIMD-a se mogu mjeriti nezavisno: E se od A razlikuje samo po driveru, a F od C takođe."));
  const t2 = TABLE("Varijante implementacije: skup kernela × driver", [
    ["Varijanta", "Skup kernela (fajl)", "Driver", "Zastavice kernela"],
    ["A Scalar", "`kernels_scalar.cpp`", "serijski", "-O3, bez vektorizacije"],
    ["B Auto-vektorizacija", "`kernels_auto.cpp`", "serijski", "-O3 -mavx2 -fno-trapping-math"],
    ["C OpenMP SIMD", "`kernels_omp_simd.cpp`", "serijski", "-O3 -mavx2 -fopenmp(-simd) -fno-trapping-math"],
    ["D AVX2 intrinsics", "`kernels_avx2.cpp`", "serijski", "-O3 -mavx2"],
    ["E OpenMP dretve", "`kernels_scalar.cpp`", "`omp parallel for`", "kao A"],
    ["F OpenMP + SIMD", "`kernels_omp_simd.cpp`", "`omp parallel for`", "kao C"],
    ["G OpenMP + AVX2", "`kernels_avx2.cpp`", "`omp parallel for`", "kao D"],
    ["A2 (ablacija)", "`kernels_scalar_branchless.cpp`", "serijski", "izvor B, bez vektorizacije"],
    ["E2, F2 (ablacija)", "kao A, C", "`omp parallel` + `omp for`", "jedna regija za sve korake"],
    ["H (ablacija)", "`kernels_auto.cpp`", "`omp parallel for`", "kao B"],
  ], [1.5, 2.1, 1.6, 2.4], { size: 17 });
  items.push(...t2.items);
  items.push(P("Ostali moduli su: `simulation.hpp/.cpp` (stanje, parametri, CFL, dijagnostika, poređenje stanja), `terrain.cpp` (deterministički početni teren), `driver_serial.cpp` i `driver_openmp.cpp`, `variants.cpp` (registar varijanti i runtime provjera AVX2), `benchmark.hpp/.cpp`, `cpu_features.cpp` (CPUID/XGETBV, FTZ/DAZ, opis builda), `io_npy.cpp` (izvoz u NumPy format), `main.cpp` (načini rada `info`, `bench`, `simulate`, `stream`) i `tests/correctness_tests.cpp`. Python skripte služe isključivo za grafikone."));

  items.push(H(2, "Strukture podataka i organizacija memorije"));
  items.push(...bullets([
    "**Structure of Arrays.** Stanje je devet float nizova: dva bafera terena b, dubina d, sediment s, pomoćni niz σ (`sr`) i četiri protoka. Svaka faza čita samo potrebne nizove, kontinuirano po x, što je preduslov za SIMD i za GPU coalesced pristup.",
    "**Halo okvir i pitch.** Svaki niz ima jedan red iznad i ispod mreže i kolone lijevo i desno. Unutrašnja ćelija (x, y) je na indeksu (y + 1)·pitch + 8 + x; pitch je višekratnik od 8, a lijevo je ostavljeno 8 kolona, pa svaki unutrašnji red počinje na 32-bajtnoj granici (`_mm256_load_ps`). Susjedi x ± 1 su udaljeni jedan element, a y ± 1 jedan pitch; pitch nije stepen dvojke (npr. 1040 za W = 1024), čime se izbjegavaju konflikti skupova keša.",
    "**Rubni uslovi bez grananja.** Zatvoreni zidovi se ostvaruju množenjem novih protoka maskama `openL[x]`, `openR[x]` (0 na rubu, 1 inače) i skalarnim maskama za prvi i posljednji red. Halo vode, sedimenta i protoka je uvijek nula, a halo terena se prije faze 2 puni kopijom rubnih vrijednosti (Neumannov uslov), da bi nagib na rubu bio jednostran.",
    "**Dvostruko baferovanje terena.** Faza 2 čita nagib iz b_{src} (susjedi), a novu visinu piše u b_{dst}; nakon koraka baferi zamijene uloge zamjenom indeksa (bez kopiranja).",
    "**Poravnanje.** `AlignedBuffer` alocira memoriju poravnatu na 64 B (linija keša) i inicijalizuje je nulama.",
  ]));

  items.push(H(2, "Faze vremenskog koraka i zavisnosti"));
  items.push(P("Jedan korak se sastoji od tri faze; unutar faze svaka ćelija piše isključivo svoje elemente, pa su iteracije nezavisne. Tabela 7 je ujedno i dokaz odsustva race condition-a: nijedan niz koji se u fazi piše ne čita se na susjednoj ćeliji u istoj fazi."));
  const t3 = TABLE("Čitanje i pisanje nizova po fazama (c – vlastita ćelija, N – 4 susjeda)", [
    ["Faza", "Čita", "Piše", "Zašto je bez race condition-a"],
    ["1: kiša + protoci", "b (c, N), d (c, N), f^{L,R,T,B} (c)", "f^{L,R,T,B} (c)", "f se na mjestu ažurira samo za vlastitu ćeliju; susjedi čitaju f tek u fazi 2"],
    ["2: voda, brzina, erozija, isparavanje", "f (c, N), b_{src} (c, N), d (c), s (c)", "d (c), s (c), σ (c), b_{dst} (c)", "d i s susjeda se ne čitaju; teren se čita iz b_{src}, a piše u b_{dst}"],
    ["3: transport sedimenta", "σ (c, N), f (c, N), s (c)", "s (c)", "susjedi se čitaju iz σ, ne iz s"],
  ], [1.4, 2.0, 1.6, 2.6], { size: 17 });
  items.push(...t3.items);

  items.push(H(2, "Pseudokod"));
  items.push(...CODE(`za svaki korak:
    osvježi halo terena b_src (kopija rubnih vrijednosti)          // O(W + H)
    paralelno za svaki red y, za svaku ćeliju x:                     // FAZA 1
        d1 = d + r·Δt;  h = b + d1
        za smjer k ∈ {L, R, T, B}:
            f_k = max(0, f_k + Δt·A·g·(h − h_k)/l) · otvoren_k
        K = min(1, d1·lx·ly / max((f_L+f_R+f_T+f_B)·Δt, ε));  f_k ← f_k·K
    barijera
    paralelno za svaki red y, za svaku ćeliju x:                     // FAZA 2
        d2 = max(0, d1 + Δt·(Σ f_in − Σ f_out)/(lx·ly));  d̄ = (d1 + d2)/2
        u, v = ΔW_x/(ly·d̄), ΔW_y/(lx·d̄)   (0 ako d̄ ≤ d_min)
        sinα = max(|∇b_src|/√(1+|∇b_src|²), s_min)
        C = K_c·sinα·|v|·min(1, d̄/d_ero);  a = Δt·(C>s ? K_s : K_d)·(C − s)
        b_dst = b_src − a;  s = s + a;  σ = s·Δt/(d1·lx·ly);  d = d2·(1 − K_e·Δt)
    barijera
    paralelno za svaki red y, za svaku ćeliju x:                     // FAZA 3
        s = max(0, s − σ·Σ f_out + Σ_susjedi σ_n·f_{n→c})
    barijera;  zamijeni(b_src, b_dst)`));

  items.push(H(2, "Serijska implementacija"));
  items.push(P("Skalarna referenca je napisana tako da bude čitljiva „specifikacija“ modela: operacije nad jednom ćelijom su `inline` funkcije u `cell_ops.hpp` (`flux_cell`, `water_cell`, `sediment_cell`), sa prirodnim `if` naredbama, a red se obilazi petljom označenom sa `SW_NO_VECTORIZE`. Isti fajl koriste i AVX2 kerneli za skalarni ostatak reda. Svi izrazi imaju eksplicitan redoslijed sabiranja (npr. ((f^{L} + f^{R}) + f^{T}) + f^{B}), a projekat se prevodi sa `-ffp-contract=off` (bez spajanja množenja i sabiranja u FMA) i bez `-ffast-math`, pa sve varijante izvršavaju identične IEEE 754 operacije. Kompletan kod je u prilogu (direktorij `src/`)."));
  return items;
}

function vektorizacija(vecInitial, vecFinal, disasm) {
  const items = [];
  items.push(H(1, "CPU vektorizacija"));
  items.push(H(2, "Analiza zavisnosti između iteracija"));
  items.push(P("Unutrašnja petlja svake faze ide po x unutar jednog reda. Iteracija x čita elemente x − 1, x, x + 1 tekućeg reda i element x susjednih redova, a piše samo element x nizova koji se u toj fazi ne čitaju na susjednim pozicijama (Tabela 7). Zato nema prenesenih zavisnosti (eng. *loop-carried dependency*): osam uzastopnih iteracija može se izvršiti kao jedna AVX2 operacija. Jedini „uslovni“ dijelovi – max(0, ·), faktor K, odabir K_{s}/K_{d}, uslovi za d_{min} i σ – pretvaraju se u SIMD maske i operacije min/max/blend. Model nema redukcija, za razliku od skalarnog proizvoda sa vježbi, pa nije potrebno mijenjati asocijativnost sabiranja. Faza bi bila nesigurna za SIMD jedino kada bi se teren ažurirao na mjestu (iteracija x bi čitala b[x − 1] koji je iteracija x − 1 već promijenila u skalarnom, ali ne i u vektorskom izvršavanju) – upravo to sprečava dvostruki bafer."));
  items.push(H(2, "Automatska vektorizacija i analiza izvještaja kompajlera"));
  items.push(P("Prva verzija `kernels_auto.cpp` imala je SoA pristup, jednostavne petlje, lokalne `__restrict` pokazivače na redove i uslove zapisane kao `std::min`/`std::max`. Izvještaj GCC 13 (`-fopt-info-vec-optimized -fopt-info-vec-missed`) pokazao je da je vektorizovana samo petlja faze 3, i to uz runtime provjeru preklapanja, dok faze 1 i 2 nisu vektorizovane. Ista je situacija bila i sa OpenMP SIMD varijantom. Ovo potvrđuje upozorenje iz zadatka da se uspješnost automatske vektorizacije ne smije pretpostaviti."));
  const rows = [["Fajl / petlja", "Prva verzija", "Konačna verzija"]];
  rows.push(...vecInitial);
  const t = TABLE("Izvještaj GCC 13.3 o vektorizaciji (sažeto; potpuni izvještaji u results/vectorization)", rows.concat([]), [2.0, 2.6, 2.4], { size: 16 });
  // zamijeni treću kolonu konačnim stanjem
  items.push(...t.items);
  items.push(P("Analiza GIMPLE ispisa (`-fdump-tree-ifcvt-details`, `-fdump-tree-vect-details`) otkrila je dva uzroka:"));
  items.push(...numbered([
    "**Preklapanje pokazivača.** GCC ne koristi pouzdano `__restrict` na lokalnim pokazivačima izvedenim iz drugih pokazivača; za 13 nizova bilo bi potrebno previše runtime provjera („versioning for alias required“). Rješenje: tijela petlji su izdvojena u funkcije čiji su pokazivači na redove `__restrict` *parametri*.",
    "**„Control flow in loop“ zbog `-ftrapping-math`.** Kompajler je rezultat izraza poput `std::min(k, 1)` koristio granajući se (npr. grana u kojoj je k = 1 pa je `nl * k` jednako `nl`), a množenje koje može izazvati FP izuzetak ne smije se spekulativno izvršiti dok je uključen podrazumijevani `-ftrapping-math`. Zbog toga if-konverzija nije uspjela čak ni uz `#pragma omp simd`. Rješenje: vektorski fajlovi se prevode sa `-fno-trapping-math`, koji dozvoljava spekulaciju, ali ne mijenja nijednu izračunatu vrijednost (nema reasocijacije ni zanemarivanja NaN/Inf, za razliku od `-ffast-math`). Clang 18 ima ovo ponašanje po defaultu i vektorizuje sve tri petlje bez dodatnih zastavica.",
  ]));
  items.push(P("Uz to, `-fno-math-errno` je potreban da bi se `std::sqrt` mogao vektorizovati (inače poziv može postaviti `errno`), a dijeljenja su zapisana bezuslovno sa sigurnim imeniocem i nakon toga birana maskom (npr. `1/(L·max(d̄, d_min))`, pa odabir po uslovu d̄ > d_{min}). Nakon izmjena GCC prijavljuje sve tri petlje kao vektorizovane 32-bajtnim (AVX2) vektorima, uz 16-bajtni epilog za ostatak reda. Disasembler (Tabela 9) potvrđuje da skalarni fajlovi ne sadrže nijednu pakovanu instrukciju, a vektorski sadrže `ymm` instrukcije, uključujući `vsqrtps` i `vdivps`."));
  const dt = TABLE("Broj instrukcija u objektnim fajlovima kernela (objdump)", disasm, [2.2, 1, 1.4, 1.6, 1, 1], { size: 17, align: ["l", "r", "r", "r", "r", "r"] });
  items.push(...dt.items);

  items.push(H(2, "OpenMP SIMD"));
  items.push(P("Varijanta C sadrži isti izvorni kod kao B, ali bez `__restrict`, sa direktivom `#pragma omp simd` ispred unutrašnjih petlji. Direktiva je tvrdnja programera da su iteracije nezavisne, pa kompajler ne analizira preklapanje i ne primjenjuje cost model. Primijenjena je samo na petlje za koje je nezavisnost pokazana u Tabeli 7; redukcije (`reduction`) nisu potrebne. Pokazalo se da direktiva ne zaobilazi ograničenje `-ftrapping-math`, pa i C koristi `-fno-trapping-math`. Važno je razlikovati `omp simd` (vektorizacija u jednoj dretvi) od `omp parallel for` (raspodjela iteracija na dretve) – C je jednodretvena varijanta."));
  items.push(...CODE(`SW_OMP_SIMD                              // _Pragma("omp simd")
for (int x = 0; x < W; ++x)
{
    const float d1 = d[x] + rain;
    const float h  = b[x] + d1;
    ...
    const float nl = std::max(fl[x] + kf * (h - hl), 0.0f) * open_l[x];
    ...
    const float k = std::min(vol / std::max(out, tiny), 1.0f);
    fl[x] = nl * k;  fr[x] = nr * k;  ft[x] = nt * k;  fb[x] = nb * k;
}`));

  items.push(H(2, "AVX2 intrinsics"));
  items.push(P("Varijanta D eksplicitno koristi 256-bitne registre (`__m256`, 8 float vrijednosti) iz `<immintrin.h>`. Centralni elementi i susjedi gore/dolje učitavaju se poravnatim `_mm256_load_ps`, jer red počinje na 32-bajtnoj granici i pitch je višekratnik od 8, a susjedi lijevo i desno neporavnatim `_mm256_loadu_ps`. Uslovi su realizovani maskama: `_mm256_cmp_ps` daje masku, a `_mm256_blendv_ps` i `_mm256_and_ps` biraju vrijednosti. Redoslijed operanada u `_mm256_max_ps`/`_mm256_min_ps` odabran je tako da se poklapa sa semantikom `std::max`/`std::min` (vraća drugi operand kada poređenje nije tačno), čime se dobija bit-identičan rezultat. Ostatak reda (W mod 8 ≠ 0) računa se skalarnom referencom. Fajl se prevodi sa `-mavx2`, a AVX2 varijante (i B, C, F, H, prevedene sa `-mavx2` radi iste širine vektora) pozivaju se samo ako runtime provjera (CPUID leaf 7, bit AVX2, i XGETBV za čuvanje YMM stanja) potvrdi podršku; na ne-x86 arhitekturama D/G se automatski svode na auto-vektorizovanu verziju, pa nepodržane instrukcije nikada nisu izvršene."));
  items.push(...CODE(`const __m256 s0     = _mm256_load_ps(s + x);
const __m256 diff   = _mm256_sub_ps(capacity, s0);
const __m256 erode  = _mm256_cmp_ps(diff, zero, _CMP_GT_OQ);     // maska C > s
const __m256 rate   = _mm256_blendv_ps(dtkd, dtks, erode);        // K_d ili K_s
const __m256 amount = _mm256_mul_ps(rate, diff);
_mm256_store_ps(bd + x, _mm256_sub_ps(_mm256_load_ps(bs + x), amount));
const __m256 s1 = _mm256_add_ps(s0, amount);
_mm256_store_ps(s + x, s1);
const __m256 has_water = _mm256_cmp_ps(d1, seps, _CMP_GT_OQ);
const __m256 ratio = _mm256_div_ps(_mm256_mul_ps(s1, dt),
                                   _mm256_mul_ps(_mm256_max_ps(seps, d1), area));
_mm256_store_ps(sr + x, _mm256_and_ps(has_water, ratio));     // σ = 0 ako nema vode`));
  return items;
}

function paralelizacija() {
  const items = [];
  items.push(H(1, "CPU paralelizacija"));
  items.push(H(2, "OpenMP i dekompozicija domene"));
  items.push(P("Višedretvene varijante dijele mrežu na trake redova: petlja po redovima y svake faze označena je sa `#pragma omp parallel for schedule(static)`, pa svaka od p dretvi dobija kontinuiran blok od približno H/p redova. Statičko raspoređivanje je prirodan izbor jer svaki red ima isti broj ćelija, a kod je bez grananja (u vektorskim varijantama), pa je opterećenje ravnomjerno i nije potreban dinamički raspored sa svojim troškom. Kontinuirani blokovi čuvaju lokalnost: dretva čita najviše jedan red iz tuđeg bloka na svakoj granici. Ovakva dekompozicija odgovara pristupu LISFLOOD-FP (OpenMP po redovima) " + cite("shaw2021") + "."));
  items.push(...CODE(`for (int step = 0; step < steps; ++step)
{
    sim.refresh_terrain_halo();                  // serijski, O(W + H)
    const float* b = sim.terrain();  float* b_next = sim.terrain_next();
#pragma omp parallel for schedule(static) num_threads(threads)
    for (int y = 0; y < H; ++y) k.flux(F, c, b, y);            // faza 1
#pragma omp parallel for schedule(static) num_threads(threads)
    for (int y = 0; y < H; ++y) k.water(F, c, b, b_next, y);   // faza 2
#pragma omp parallel for schedule(static) num_threads(threads)
    for (int y = 0; y < H; ++y) k.sediment(F, c, y);           // faza 3
    sim.swap_terrain();                          // double buffering
}`));
  items.push(H(2, "Upravljanje dretvama i sinhronizacija"));
  items.push(...bullets([
    "**Sinhronizacija.** Jedina potrebna sinhronizacija su implicitne barijere na kraju svake `parallel for` petlje; nema kritičnih sekcija, atomika ni zaključavanja, jer svaka dretva piše isključivo svoje redove.",
    "**Race conditions.** Isključeni su konstrukcijom (Tabela 7): faze razdvajaju čitanje i pisanje, a teren je dvostruko baferovan. Testovi potvrđuju da su rezultati za p = 1, 2, 3, 4 i 8 dretvi bit-identični referenci – što ne bi bilo moguće sa trkom podataka.",
    "**False sharing.** Dretve pišu u disjunktne blokove redova; linija keša (64 B) može biti dijeljena samo na granici dva bloka, i to najviše jedna po nizu, pa je efekat zanemariv. Za mrežu 128 × 128 i 8 dretvi svaka dretva dobija 16 redova (≈ 8 KB po nizu).",
    "**Fork-join naspram jedne regije.** `parallel for` u svakoj fazi formira tim dretvi tri puta po koraku (libgomp ponovo koristi iste dretve, pa je trošak mali). Ablacijske varijante E2/F2 koriste jednu `#pragma omp parallel` regiju za sve korake, `#pragma omp for` za faze i `#pragma omp single` za halo i zamjenu bafera.",
    "**FTZ/DAZ.** Sediment u suhim ćelijama eksponencijalno opada (s ← s(1 − K_{d}Δt)) i nakon ~1500 koraka ulazi u opseg subnormalnih brojeva, čija obrada je na x86 višestruko sporija. Zato svaka dretva postavlja FTZ/DAZ bitove registra MXCSR; to utiče samo na vrijednosti ispod 1,18·10^{-38} i isto se primjenjuje u svim varijantama (slično je urađeno i u " + cite("klower2022") + ").",
  ]));
  items.push(H(2, "Kombinacija OpenMP i SIMD"));
  items.push(P("Varijante F i G kombinuju oba nivoa paralelizma na dva nivoa petlje: vanjska petlja po redovima raspoređuje se na dretve (`omp parallel for`), a unutrašnja petlja po kolonama se vektorizuje (`omp simd` u F, AVX2 u G). Kombinovana direktiva `#pragma omp parallel for simd` odnosi se na jednu petlju; primijenjena na petlju po redovima vektorizovala bi rad *preko redova*, sa korakom pitch između SIMD traka, što bi dalo gather pristup umjesto kontinuiranog. Varijanta sa `collapse(2)` bi spojila obje petlje, ali bi zbog halo kolona morala ponovo računati (x, y) iz linearnog indeksa i preskakati halo ćelije. Zbog toga je uslov „sigurnog paralelnog i SIMD izvršavanja“ ispunjen na najprirodniji način podjelom uloga: dretve preko redova, SIMD duž reda. Literalna direktiva `#pragma omp parallel for simd` korištena je tamo gdje je petlja jednodimenzionalna i kontinuirana – u mjerenju memorijske propusnosti (STREAM triad, poglavlje 8)."));
  return items;
}

function gpu() {
  const items = [];
  items.push(H(1, "Priprema za GPU paralelizaciju"));
  items.push(P("Ovo poglavlje je konceptualno; GPU implementacija je predmet druge faze. Arhitektura CPU rješenja je namjerno projektovana tako da se preslika na CUDA bez promjene numeričkog modela."));
  items.push(H(2, "Dijelovi algoritma za GPU i CUDA dekompozicija"));
  items.push(...bullets([
    "**Sve tri faze su kerneli.** Svaka funkcija `flux_row`/`water_row`/`sediment_row` postaje CUDA kernel u kojem jedna nit obrađuje jednu ćeliju (x = blockIdx.x·blockDim.x + threadIdx.x, analogno y), kao u " + cite("nilles2024", "salamin2024") + ". Granica između kernela zamjenjuje OpenMP barijeru.",
    "**Blokovi 32 × 8 ili 16 × 16 niti.** Širina bloka od 32 niti odgovara warpu, pa su pristupi redu kontinuirani (coalesced); LISFLOOD-FP koristi blokove 16 × 16 " + cite("shaw2021") + ". Halo ćelije bloka (stencil) mogu se učitati u dijeljenu memoriju, iako za samo 4 susjeda L1/L2 keš često daje sličan učinak.",
    "**Halo terena i zamjena bafera** postaju mali kernel (ili se rub tretira uslovom unutar kernela) i zamjena pokazivača na strani domaćina.",
    "**Dijagnostika** (zbirovi mase, maksimumi) zahtijeva paralelne redukcije (npr. CUB, kao u " + cite("cea2024") + "), ali nije na kritičnom putu svakog koraka.",
  ]));
  items.push(H(2, "Strukture podataka koje se zadržavaju"));
  items.push(P("SoA raspored, float preciznost, pitch poravnat na 32 B (na GPU-u se preporučuje višekratnik od 128 B, što daje `cudaMallocPitch`), halo okvir, maske otvorenosti rubova i dvostruki bafer terena prenose se direktno. Skalarna referenca i testovi ekvivalencije ostaju „specifikacija“: GPU rezultat se može porediti sa CPU referencom istim kodom za poređenje stanja (`compare_states`), uz napomenu da kompajler za GPU podrazumijevano spaja množenje i sabiranje u FMA (`-fmad=true`), pa se za bit-identično poređenje mora isključiti, ili koristiti tolerancija."));
  items.push(H(2, "Očekivani izazovi"));
  items.push(...bullets([
    "**Propusnost memorije.** Kernel je ograničen memorijom i na CPU-u (poglavlje 9); na GPU-u je propusnost red veličine veća, ali je zbog male aritmetičke gustine ključna fuzija faza i smanjenje broja nizova (npr. faza 3 i faza 1 narednog koraka mogu se spojiti uz dodatni halo).",
    "**Prenos podataka.** Stanje treba držati na GPU-u tokom cijele simulacije; prenos na domaćina samo za izlaz i dijagnostiku " + cite("cea2024") + ".",
    "**Divergencija warpova** je mala jer su kerneli bez grananja; uslovi su već pretvoreni u select operacije.",
    "**Mali problemi.** Ispod ~10^{5} ćelija GPU nije efikasan (LISFLOOD-FP: CPU je bolji ispod 0,1 M elemenata) " + cite("shaw2021") + ", pa poređenje treba raditi i za 2048² i 4096².",
    "**Fer poređenje.** Ubrzanje GPU-a treba prikazati u odnosu na najbolju CPU varijantu (G sa svim jezgrama), a ne samo u odnosu na skalarnu referencu " + cite("salamin2024") + ".",
  ]));
  return items;
}

module.exports = { implementacija, vektorizacija, paralelizacija, gpu };
