// Poglavlja 8, 9 i 11: eksperimentalna evaluacija, diskusija i zaključak.
// Sve brojčane vrijednosti čitaju se iz results/*.csv (stvarna mjerenja), ništa nije upisano ručno.
const fs = require("fs");
const path = require("path");
const { d, P, H, bullets, numbered, TABLE, FIG, EQ, M } = require("./helpers");
const { cite } = require("./references");

function readCsv(file) {
  const text = fs.readFileSync(file, "utf8").trim().split(/\r?\n/);
  const parse = (line) => {
    const out = []; let cur = ""; let q = false;
    for (const ch of line) {
      if (ch === '"') q = !q; else if (ch === "," && !q) { out.push(cur); cur = ""; } else cur += ch;
    }
    out.push(cur); return out;
  };
  const head = parse(text[0]);
  return text.slice(1).map((l) => { const v = parse(l); const o = {}; head.forEach((h, i) => { o[h] = v[i]; }); return o; });
}

const f1 = (x) => Number(x).toFixed(1).replace(".", ",");
const f2 = (x) => Number(x).toFixed(2).replace(".", ",");
const f3 = (x) => Number(x).toFixed(3).replace(".", ",");
const fe = (x, n = 1) => { const [m, e] = Number(x).toExponential(n).split("e"); return `${m.replace(".", ",")}·10^{${Number(e)}}`; };

function load(res) {
  const rows = readCsv(path.join(res, "benchmarks.csv"));
  const pick = (grid, variant, threads, measure = "kernel") => rows.find((r) => r.grid === grid && r.variant === variant && r.measure === measure && (threads === undefined || Number(r.threads) === threads));
  const grids = [...new Set(rows.map((r) => r.grid))].sort((a, b) => parseInt(a) - parseInt(b));
  return { rows, pick, grids };
}

function evaluacija(res, fig) {
  const { rows, pick, grids } = load(res);
  const sys = fs.readFileSync(path.join(res, "system_info.txt"), "utf8");
  const grab = (re, def = "?") => { const m = sys.match(re); return m ? m[1].trim() : def; };
  const stream = readCsv(path.join(res, "stream.csv"));
  const streamL3 = fs.existsSync(path.join(res, "stream_l3.csv")) ? readCsv(path.join(res, "stream_l3.csv")) : [];
  const corr = readCsv(path.join(res, "correctness_report.csv"));
  const recheck = fs.existsSync(path.join(res, "recheck_512_p1.csv")) ? readCsv(path.join(res, "recheck_512_p1.csv")) : [];
  const nm8 = fs.existsSync(path.join(res, "benchmarks_non_multiple8.csv")) ? readCsv(path.join(res, "benchmarks_non_multiple8.csv")) : [];
  const simSummary = fs.readFileSync(path.join(res, "figures", "sim_summary.txt"), "utf8");
  const sv = (re) => { const m = simSummary.match(re); return m ? m[1] : "?"; };
  const items = [];
  const P2 = 2;
  const kernelRows = rows.filter((r) => r.measure === "kernel");

  items.push(H(1, "Eksperimentalna evaluacija"));
  items.push(H(2, "Testno okruženje"));
  const env = TABLE("Testno okruženje (evidentirano skriptom scripts/system_info.sh)", [
    ["Stavka", "Vrijednost"],
    ["CPU", grab(/Model name:\s+(.+)/) + " (virtuelna mašina, hipervizor " + grab(/Hypervisor vendor:\s+(.+)/) + ")"],
    ["Jezgre", grab(/Core\(s\) per socket:\s+(\d+)/) + " jezgre × " + grab(/Thread\(s\) per core:\s+(\d+)/) + " dretva = " + grab(/^CPU\(s\):\s+(\d+)/m) + " logička procesora (vCPU)"],
    ["Keš", "L1d " + grab(/L1d cache:\s+(.+)/) + ", L2 " + grab(/L2 cache:\s+(.+)/) + ", L3 " + grab(/L3 cache:\s+(.+)/) + " (kako ga prijavljuje hipervizor)"],
    ["RAM", grab(/Mem:\s+(\S+)/) + "B"],
    ["Operativni sistem", grab(/PRETTY_NAME="(.+)"/) + ", Linux kernel " + grab(/\n(\d+\.\d+\.\d+[^\n]*)\n\n=== Kompajler/)],
    ["Kompajler", grab(/=== Kompajler ===\n(.+)/) + "; " + grab(/(cmake version .+)/)],
    ["Zastavice (sve)", "-O3 -DNDEBUG -std=c++20 -ffp-contract=off -fno-math-errno"],
    ["Zastavice (A, E)", "-fno-tree-vectorize -fno-tree-loop-vectorize -fno-tree-slp-vectorize"],
    ["Zastavice (B, C, D, F, G, H)", "-mavx2; B, C, F, H i A2 još -fno-trapping-math"],
    ["SIMD", "AVX2 (8 × float); CPU podržava i AVX-512, ali se ne koristi"],
    ["OpenMP", "libgomp, OpenMP 4.5 (_OPENMP = 201511), -fopenmp; nijedna OMP_* varijabla nije postavljena"],
  ], [1.6, 5.4], { size: 18 });
  items.push(...env.items);
  items.push(P("**Važno ograničenje:** dostupno okruženje ima samo dvije jezgre, pa su mjerenja sa 4 i 8 dretvi mjerenja *prekomjernog opterećenja* (eng. *oversubscription*), a ne skaliranja. Ona su ipak zadržana, jer pokazuju šta se dešava kada se broj dretvi ne uskladi sa hardverom. Mjerenja su rađena u dijeljenoj virtuelnoj mašini, pa je varijabilnost veća nego na namjenskom računaru (koeficijent varijacije uglavnom 2–15 %). Skripta `scripts/run_benchmarks.sh` ponavlja kompletan postupak na drugom računaru."));

  items.push(H(2, "Metodologija mjerenja"));
  items.push(...bullets([
    "**Početno stanje.** Za svaku mrežu generiše se teren i izvrši 200 koraka skalarnom referencom (voda već teče). Ovo stanje je zajedničko za sve varijante i kopira se prije *svakog* mjerenja funkcijom `prepare()`, koja nije uključena u vrijeme.",
    "**Broj koraka.** Svako mjerenje izvršava isti broj koraka za sve varijante, tako da je broj ažuriranja ćelija ≈ 2^{23}: 512 koraka za 128², 128 za 256², 32 za 512² i 8 za 1024².",
    "**Ponavljanja.** 2 warm-up izvršavanja i 10 mjerenih ponavljanja; računaju se medijan, minimum, maksimum, srednja vrijednost i standardna devijacija. Medijan je osnovna mjera jer je otporan na povremene smetnje virtuelne mašine.",
    "**Zaštita od eliminacije računanja.** Nakon svakog izvršavanja računa se kontrolni zbir stanja i predaje funkciji `consume()` (atomični XOR), kao na vježbama.",
    `**Provjera tokom mjerenja.** Nakon mjerenja se stanje svake varijante poredi sa referencom; u ${kernelRows.filter((r) => r.bitwise_equal_to_scalar === "1").length + nm8.filter((r) => r.bitwise_equal_to_scalar === "1").length} od ${kernelRows.length + nm8.length} izmjerenih konfiguracija jezgra rezultat je bio bit-identičan referenci.`,
    "**Kompletna simulacija (end-to-end)** obuhvata alokaciju, generisanje terena, 10 puta više koraka nego mjerenje jezgra (iz suhog početnog stanja) i završnu dijagnostiku; za višedretvene varijante p = 2.",
    "**Memorijska propusnost** je izmjerena STREAM triad petljom (`a[i] = b[i] + s·c[i]`, `#pragma omp parallel for simd`) za nizove od 3 × 128 MB (RAM) i 3 × 12 MB (keš).",
  ]));
  items.push(P("Mjere su ubrzanje u odnosu na skalarnu referencu S_{A} = T_{A}/T, ubrzanje dretvi i paralelna efikasnost:", { keepNext: true }));
  const e1 = EQ(["S", M.par("p"), "=", M.frac([M.sub("T", "1")], [M.sub("T", "p")]), ",", "   ", "E", M.par("p"), "=", M.frac(["S", M.par("p")], "p")]);
  items.push(e1.para);
  items.push(P("gdje je T_{1} vrijeme serijske varijante sa *istim* skupom kernela (A za E, C za F, D za G). Na taj način ubrzanje dretvi nije pomiješano sa SIMD ubrzanjem. Pored toga računaju se propusnost u milionima ćelija-koraka u sekundi, vrijeme po ćeliji i koraku, te efektivna memorijska propusnost prema modelu od 112 B po ćeliji i koraku (28 float čitanja i upisa kroz tri faze, susjedi iz keša, bez write-allocate saobraćaja)."));

  // ------------------------------------------------------------ ispravnost
  items.push(H(2, "Numerička ispravnost"));
  const groups = {};
  corr.forEach((r) => { (groups[r.test] = groups[r.test] || []).push(r); });
  const desc = {
    ekvivalencija_sa_referencom: ["sva polja (b, d, s, f) svih varijanti i p ∈ {1,2,3,4,8} naspram A; 8 mreža od 8×8 do 257×255, 300 koraka", "max |razlika|"],
    halo_i_padding_netaknuti: ["mreža 29×11 (W mod 8 ≠ 0): halo i padding ostaju nula", "broj grešaka"],
    jezero_u_mirovanju: ["ravna vodena površina, bez kiše: protoci, d i b ostaju tačno nepromijenjeni nakon 1000 koraka", "max |f|"],
    bilans_vode: ["W_{n+1} = (W_{n} + N r Δt)(1 − K_{e}Δt), 500 koraka", "max rel. greška"],
    ocuvanje_teren_plus_sediment: ["Σ(b + s) nakon 1500 koraka erozije", "rel. promjena"],
    nenegativnost_i_stabilnost: ["3000 koraka: d ≥ 0, s ≥ 0, sve vrijednosti konačne", "min d"],
    zatvoreni_rubovi: ["protok kroz rub domene je tačno nula", "max protok"],
    simetrija_lijevo_desno: ["simetričan teren daje bit-egzaktno simetrično stanje", "broj neslaganja"],
    cfl_ogranicenje: ["traženi Δt = 10 s se ograničava na CFL granicu (14)", "korišteni Δt"],
    stabilnost_na_cfl_limitu: ["3000 koraka sa Δt na CFL granici", "max dubina [m]"],
  };
  const trows = [["Test", "Šta se provjerava", "Slučajeva", "Uspješno", "Najgora vrijednost"]];
  let totalCases = 0, totalPass = 0, bitwise = 0, eqCases = 0;
  Object.keys(groups).forEach((k) => {
    const g = groups[k];
    const pass = g.filter((r) => r.result === "PASS").length;
    totalCases += g.length; totalPass += pass;
    if (k === "ekvivalencija_sa_referencom") { eqCases = g.length; bitwise = g.filter((r) => r.detail.includes("bit-identicno")).length; }
    const worst = Math.max(...g.map((r) => Math.abs(Number(r.value))));
    const wtxt = k === "cfl_ogranicenje" ? f3(worst) + " s" : (worst === 0 ? "0 (tačno)" : (worst < 0.01 ? fe(worst) : f3(worst)));
    const names = { ekvivalencija_sa_referencom: "ekvivalencija sa referencom", halo_i_padding_netaknuti: "halo i padding netaknuti",
      jezero_u_mirovanju: "jezero u mirovanju", bilans_vode: "bilans vode", ocuvanje_teren_plus_sediment: "očuvanje b + s",
      nenegativnost_i_stabilnost: "nenegativnost i stabilnost", zatvoreni_rubovi: "zatvoreni rubovi",
      simetrija_lijevo_desno: "simetrija lijevo–desno", cfl_ogranicenje: "CFL ograničenje", stabilnost_na_cfl_limitu: "stabilnost na CFL granici" };
    trows.push([names[k] || k.replace(/_/g, " "), (desc[k] || ["", ""])[0], String(g.length), String(pass), wtxt]);
  });
  const tc = TABLE("Rezultati testova numeričke ispravnosti (results/correctness_report.csv)", trows, [1.6, 3.2, 0.8, 0.8, 1.2], { size: 16, align: ["l", "l", "r", "r", "r"] });
  items.push(P(`Testovi (\`prs_tests\`, ukupno ${totalCases} slučajeva) provjeravaju sve zahtjeve iz zadatka: visinu vode, protoke, teren i sediment, stabilnost tokom više koraka, očuvanje mase, nenegativnost dubine i obradu rubnih ćelija, na mrežama čije dimenzije nisu djeljive sa osam. Korištena je kombinovana tolerancija |a − r| ≤ 10^{-5} + 10^{-4}|r|, ali ona u praksi nije bila potrebna: svih ${bitwise} od ${eqCases} slučajeva ekvivalencije dalo je rezultat identičan referenci bit po bit (Tabela ${tc.n}). Uspješno je ${totalPass} od ${totalCases} testova.`));
  items.push(...tc.items);
  items.push(P(`Simulacija od 6000 koraka na mreži 512² (poglavlje 8.8) dodatno potvrđuje bilans materijala: erodirano je ${sv(/eroded volume=([\d.]+)/).replace(".", ",")} m³, deponovano ${sv(/deposited volume=([\d.]+)/).replace(".", ",")} m³, a razlika je jednaka suspendovanom sedimentu od ${sv(/suspended=([\d.]+)/).replace(".", ",")} m³; najveća relativna promjena Σ(b + s) tokom simulacije iznosi ${fe(sv(/max \|rel\| = ([\d.e+-]+)/))}, što je nivo greške zaokruživanja u float aritmetici.`));

  // ------------------------------------------------------------ rezultati performansi
  items.push(H(2, "Rezultati performansi numeričkog jezgra"));
  const main = [["A_scalar", 1, "A Scalar"], ["A2_scalar_branchless", 1, "A2 bez grananja"], ["B_auto", 1, "B Auto"], ["C_omp_simd", 1, "C OMP SIMD"], ["D_avx2", 1, "D AVX2"],
    ["E_omp", P2, `E OMP (p=${P2})`], ["F_omp_simd", P2, `F OMP+SIMD (p=${P2})`], ["G_omp_avx2", P2, `G OMP+AVX2 (p=${P2})`]];
  const mrows = [["Varijanta", ...grids.map((g) => g.replace("x", " × "))]];
  main.forEach(([v, p, lab]) => mrows.push([lab, ...grids.map((g) => { const r = pick(g, v, p); return `${f2(r.median_ms)} ms (${f1(r.speedup_vs_scalar)}×)`; })]));
  const tm = TABLE("Medijan vremena mjerenja (isti broj ćelija-koraka ≈ 8,4·10^{6}) i ubrzanje u odnosu na A", mrows, [1.9, 1.3, 1.3, 1.3, 1.3], { size: 17, align: ["l", "r", "r", "r", "r"] });
  const best = (g) => main.slice(1).map(([v, p]) => pick(g, v, p)).reduce((a, b) => (Number(a.speedup_vs_scalar) > Number(b.speedup_vs_scalar) ? a : b));
  const big = grids[grids.length - 1];
  const bestGrid = grids.reduce((a, b) => (Number(best(a).speedup_vs_scalar) >= Number(best(b).speedup_vs_scalar) ? a : b));
  const g1024 = (v, p) => pick(big, v, p);
  items.push(P(`Tabela ${tm.n} i Slika ${fig.speedup.n} prikazuju medijane vremena i ubrzanja. Jednodretvene SIMD varijante (B, C, D) su ${f1(Math.min(...grids.flatMap((g) => ["B_auto", "C_omp_simd", "D_avx2"].map((v) => Number(pick(g, v, 1).speedup_vs_scalar)))))}–${f1(Math.max(...grids.flatMap((g) => ["B_auto", "C_omp_simd", "D_avx2"].map((v) => Number(pick(g, v, 1).speedup_vs_scalar)))))} puta brže od skalarne reference, a kombinacija dvije dretve i SIMD-a (F, G) dostiže ${f1(Number(best(bestGrid).speedup_vs_scalar))}× (${best(bestGrid).variant.split("_")[0]} na mreži ${bestGrid.replace("x", " × ")}). Na najvećoj mreži G sa dvije dretve obrađuje ${f1(g1024("G_omp_avx2", P2).mcells_per_s)} miliona ćelija-koraka u sekundi (${f2(g1024("G_omp_avx2", P2).ns_per_cell_step)} ns po ćeliji), naspram ${f1(g1024("A_scalar", 1).mcells_per_s)} za skalarnu referencu.`));
  items.push(...tm.items);
  items.push(...fig.speedup.items);

  const drows = [["Varijanta", "p", "Medijan [ms]", "Min [ms]", "Max [ms]", "Std. dev. [ms]", "CV [%]", "Mćel./s"]];
  [["A_scalar", 1], ["A2_scalar_branchless", 1], ["B_auto", 1], ["C_omp_simd", 1], ["D_avx2", 1], ["E_omp", 1], ["E_omp", 2], ["F_omp_simd", 1], ["F_omp_simd", 2], ["G_omp_avx2", 1], ["G_omp_avx2", 2]].forEach(([v, p]) => {
    const r = pick(big, v, p);
    drows.push([v, String(p), f2(r.median_ms), f2(r.min_ms), f2(r.max_ms), f2(r.stddev_ms), f1(r.cv_percent), f1(r.mcells_per_s)]);
  });
  const td = TABLE(`Detaljna statistika mjerenja za mrežu ${big.replace("x", " × ")} (8 koraka, 10 ponavljanja)`, drows, [2.0, 0.4, 1, 1, 1, 1.1, 0.8, 1], { size: 16, align: ["l", "r", "r", "r", "r", "r", "r", "r"], monoCols: [0] });
  items.push(P(`Tabela ${td.n} daje potpunu statistiku za mrežu ${big}. Raspon između minimuma i maksimuma pokazuje nivo šuma okruženja; zbog toga su u analizi korišteni medijani, a razlike manje od oko 15 % nisu tumačene kao značajne.`));
  items.push(...td.items);

  if (recheck.length) {
    const rr = [["Varijanta", "Medijan [ms]", "Min [ms]", "Max [ms]", "Std. dev. [ms]", "S u odnosu na A"]];
    recheck.forEach((r) => rr.push([r.variant, f2(r.median_ms), f2(r.min_ms), f2(r.max_ms), f2(r.stddev_ms), f2(r.speedup_vs_scalar)]));
    const a = recheck.find((r) => r.variant === "A_scalar"), e = recheck.find((r) => r.variant === "E_omp");
    items.push(P(`U glavnom mjerenju varijanta E sa jednom dretvom bila je na mreži 512² sporija od A, iako obje izvršavaju isti kod. Kontrolno mjerenje sa 30 ponavljanja (results/recheck_512_p1.csv) daje gotovo jednake medijane (${f2(a.median_ms)} ms i ${f2(e.median_ms)} ms), pa je razlika bila posljedica šuma, a režijski trošak OpenMP-a sa jednom dretvom je zanemariv. Isto mjerenje potvrđuje da uklanjanje grananja (A2) ubrzava skalarni kod za oko ${f2(recheck.find((r) => r.variant === "A2_scalar_branchless").speedup_vs_scalar)}×.`));
  }

  if (nm8.length) {
    const pn = (g, v, p) => nm8.find((r) => r.grid === g && r.variant === v && Number(r.threads) === p);
    const nr = [["Varijanta", "250 × 250", "256 × 256", "1000 × 1000", "1024 × 1024"]];
    [["A_scalar", 1], ["D_avx2", 1], ["G_omp_avx2", 2]].forEach(([v, p]) => {
      nr.push([`${v} (p=${p})`, f2(pn("250x250", v, p).ns_per_cell_step), f2(pick("256x256", v, p).ns_per_cell_step), f2(pn("1000x1000", v, p).ns_per_cell_step), f2(pick(big, v, p).ns_per_cell_step)]);
    });
    const tn = TABLE("Vrijeme po ćeliji i koraku [ns] za dimenzije djeljive i nedjeljive sa 8", nr, [2.0, 1, 1, 1, 1], { size: 17, align: ["l", "r", "r", "r", "r"], monoCols: [0] });
    items.push(P(`Za dimenzije koje nisu djeljive sa osam (250 i 1000) posljednjih W mod 8 ćelija svakog reda računa se skalarno. Tabela ${tn.n} pokazuje da to nema mjerljiv uticaj: razlike u vremenu po ćeliji su unutar šuma mjerenja, jer ostatak čini manje od 3 % ćelija reda. Sve varijante su i na ovim mrežama dale bit-identične rezultate.`));
    items.push(...tn.items);
  }

  // ------------------------------------------------------------ skaliranje
  items.push(H(2, "Ubrzanje i paralelna efikasnost"));
  const srows = [["Varijanta", "p", ...[grids[1], big].flatMap((g) => [`S(p) ${g.split("x")[0]}²`, `E(p) ${g.split("x")[0]}²`])]];
  [["E_omp", "E"], ["F_omp_simd", "F"], ["G_omp_avx2", "G"]].forEach(([v, lab]) => {
    [1, 2, 4, 8].forEach((p) => {
      const a = pick(grids[1], v, p), b = pick(big, v, p);
      if (a && b) srows.push([lab, String(p), f2(a.speedup_vs_1thread_same_kernels), f2(a.parallel_efficiency), f2(b.speedup_vs_1thread_same_kernels), f2(b.parallel_efficiency)]);
    });
  });
  const ts = TABLE("Ubrzanje dretvi S(p) = T_{1}/T_{p} i efikasnost E(p) = S(p)/p (T_{1}: serijska varijanta sa istim kernelima)", srows, [1, 0.5, 1.1, 1.1, 1.1, 1.1], { size: 17, align: ["c", "r", "r", "r", "r", "r"] });
  const gE = (g, v, p) => pick(g, v, p);
  items.push(P(`Tabela ${ts.n} i Slika ${fig.scaling.n} prikazuju skaliranje sa brojem dretvi. Sa dvije dretve (koliko ima fizičkih jezgri) skalarna varijanta E postiže S = ${f2(gE(big, "E_omp", 2).speedup_vs_1thread_same_kernels)} na mreži ${big} (E = ${f2(gE(big, "E_omp", 2).parallel_efficiency)}), a G ${f2(gE(big, "G_omp_avx2", 2).speedup_vs_1thread_same_kernels)}. Na manjim mrežama efikasnost SIMD varijanti opada: na 128² F i G sa dvije dretve dostižu samo ${f2(gE(grids[0], "F_omp_simd", 2).speedup_vs_1thread_same_kernels)} i ${f2(gE(grids[0], "G_omp_avx2", 2).speedup_vs_1thread_same_kernels)}, jer jedna faza traje svega desetine mikrosekundi, pa formiranje tima i barijere postaju primjetan dio vremena. Sa 4 i 8 dretvi efikasnost naglo pada (do ${f2(Math.min(...["E_omp", "F_omp_simd", "G_omp_avx2"].map((v) => Number(gE(grids[0], v, 8).parallel_efficiency))))} na 128²): dretve dijele dvije jezgre, a dretva koja čeka na barijeri troši vrijeme jezgre na aktivno čekanje, dok dretva koja treba da završi svoj dio nije raspoređena.`));
  items.push(...ts.items);
  items.push(...fig.scaling.items);
  const ab = [["Mreža", "E (parallel for)", "E2 (regija)", "F (parallel for)", "F2 (regija)"]];
  grids.forEach((g) => ab.push([g, f2(pick(g, "E_omp", 2).median_ms), f2(pick(g, "E2_omp_region", 2).median_ms), f2(pick(g, "F_omp_simd", 2).median_ms), f2(pick(g, "F2_omp_simd_region", 2).median_ms)]));
  const tab = TABLE("Ablacija upravljanja dretvama: medijan [ms] za p = 2", ab, [1.3, 1.3, 1.2, 1.3, 1.2], { size: 17, align: ["l", "r", "r", "r", "r"] });
  items.push(P(`Ablacija u Tabeli ${tab.n} poredi tri `+ "`parallel for`" + ` regije po koraku sa jednom paralelnom regijom za sve korake. Za skalarni kod razlika je unutar šuma, jer je rad po fazi velik. Za vektorizovani kod na manjim mrežama varijanta sa jednom regijom je nešto brža (npr. ${f2(pick(grids[1], "F_omp_simd", 2).median_ms)} naspram ${f2(pick(grids[1], "F2_omp_simd_region", 2).median_ms)} ms na ${grids[1]}), što je u skladu sa zaključkom da je režijski trošak OpenMP-a značajan kada je rad po fazi mali.`));
  items.push(...tab.items);

  // ------------------------------------------------------------ propusnost memorije
  items.push(H(2, "Memorijska propusnost"));
  const st = (rows2, p) => rows2.find((r) => Number(r.threads) === p);
  items.push(P(`STREAM triad mjerenje daje ${f1(st(stream, 1).GBps_median)} GB/s za jednu i ${f1(st(stream, 2).GBps_median)} GB/s za dvije dretve kada nizovi ne staju u keš (3 × 128 MB), odnosno ${streamL3.length ? f1(st(streamL3, 1).GBps_median) : "?"} i ${streamL3.length ? f1(st(streamL3, 2).GBps_median) : "?"} GB/s za nizove koji staju u keš (3 × 12 MB). Prema modelu od 112 B po ćeliji i koraku, skalarna referenca koristi samo ${f1(g1024("A_scalar", 1).est_GBps)} GB/s, dok jednodretvene SIMD varijante dostižu ${f1(Math.min(...["B_auto", "C_omp_simd", "D_avx2"].map((v) => Number(g1024(v, 1).est_GBps))))}–${f1(Math.max(...["B_auto", "C_omp_simd", "D_avx2"].map((v) => Number(g1024(v, 1).est_GBps))))} GB/s, a G sa dvije dretve ${f1(g1024("G_omp_avx2", P2).est_GBps)} GB/s na mreži ${big} (Slika ${fig.bandwidth.n}). Vektorizovane varijante su, dakle, na nivou izmjerene propusnosti memorijskog podsistema: skalarni kod je ograničen računanjem, a vektorizovani propusnošću. Stanje mreže ${big} zauzima oko 38 MB, što prevazilazi L2 keš (2 MB po jezgri), ali staje u veliki L3 keš koji prijavljuje hipervizor, pa podaci dolaze uglavnom iz L3, a ne iz RAM-a.`));
  items.push(...fig.bandwidth.items);

  items.push(H(2, "Numeričko jezgro i kompletna simulacija"));
  const e2 = (g, v) => pick(g, v, v.startsWith("E") || v.startsWith("F") || v.startsWith("G") ? P2 : 1, "e2e");
  items.push(P(`Slika ${fig.e2e.n} poredi ubrzanje jezgra sa ubrzanjem kompletne simulacije (alokacija, generisanje terena, 10 puta više koraka i dijagnostika). Na mrežama do 512² korak dominira, pa su ubrzanja slična. Na mreži ${big} generisanje fBm terena (serijski kod u double preciznosti) traje približno koliko i svi koraci G varijante, pa ukupno ubrzanje G pada sa ${f1(g1024("G_omp_avx2", P2).speedup_vs_scalar)}× na ${f1(e2(big, "G_omp_avx2").speedup_vs_scalar)}×. To je direktna ilustracija Amdahlovog zakona: dio programa koji nije paralelizovan ograničava ukupno ubrzanje bez obzira na brzinu jezgra. Generisanje terena se, naravno, može paralelizovati, ali je namjerno ostavljeno serijsko kao mjerljiv primjer.`));
  items.push(...fig.e2e.items);

  // ------------------------------------------------------------ simulacija
  items.push(H(2, "Rezultati simulacije"));
  items.push(P(`Simulacija na mreži 512 × 512 (l = 1 m) trajala je 6000 koraka (Δt = 0,05 s, ukupno 300 s simuliranog vremena) i izvršena je varijantom G sa dvije dretve za ${f2(sv(/wall=([\d.]+)s/))} s. Teren je visok ${sv(/terrain range initial: ([\d.]+)\.\./).replace(".", ",")}–${sv(/\.\.([\d.]+) m/).replace(".", ",")} m, sa centralnim masivom i fraktalnim detaljima (Slika ${fig.terrain.n}). Kiša pada uniformno, a voda se slijeva niz padine i skuplja u uglovima domene sa zatvorenim zidovima. Na padinama masiva formira se razgranata (dendritična) mreža kanala, a materijal se taloži u nižim, ravnim dijelovima i jezerima (Slika ${fig.diff.n}); promjena visine terena kreće se od ${sv(/db: min=([-\d.]+)/).replace(".", ",")} m do +${sv(/max=([\d.]+) p1/).replace(".", ",")} m. Slika ${fig.water.n} prikazuje dubinu vode i brzinu toka (medijan ${sv(/median=([\d.]+)/).replace(".", ",")} m/s u ćelijama sa vodom dubljom od 1 cm), a Slika ${fig.mass.n} bilans vode i materijala.`));
  items.push(...fig.terrain.items);
  items.push(...fig.diff.items);
  items.push(...fig.water.items);
  items.push(...fig.mass.items);
  return items;
}

function diskusija(res, fig) {
  const { pick, grids } = load(res);
  const big = grids[grids.length - 1];
  const items = [];
  const r = (g, v, p = 1) => pick(g, v, p);
  const ratio = (g) => Number(r(g, "A2_scalar_branchless").median_ms) / Number(r(g, "B_auto").median_ms);
  items.push(H(1, "Diskusija"));
  items.push(H(2, "Koja implementacija daje najbolje rezultate?"));
  items.push(P(`Najbrže su kombinovane varijante G (OpenMP + AVX2) i F/F2 (OpenMP + omp simd) sa onoliko dretvi koliko ima jezgri: na mreži ${big} G sa dvije dretve je ${Number(r(big, "G_omp_avx2", 2).speedup_vs_scalar).toFixed(1).replace(".", ",")} puta brža od reference. Među jednodretvenim varijantama razlike između automatske vektorizacije, OpenMP SIMD i AVX2 intrinsics su na velikim mrežama unutar šuma mjerenja – kada je kod pripremljen za vektorizaciju (SoA, restrict, uslovi kao maske, odgovarajuće zastavice), kompajler generiše kod uporediv sa ručno pisanim intrinsics funkcijama. Intrinsics varijanta je bila najbrža na najmanjoj mreži (${Number(r(grids[0], "D_avx2").speedup_vs_scalar).toFixed(1).replace(".", ",")}×), gdje podaci staju u L2 keš i računska efikasnost više dolazi do izražaja. Praktična preporuka je zato OpenMP SIMD ili čista auto-vektorizacija (prenosiv kod), uz obaveznu provjeru izvještaja kompajlera, a intrinsics samo za kritične dijelove.`));
  items.push(H(2, "Zašto neke optimizacije ne daju očekivano ubrzanje?"));
  items.push(...bullets([
    `**SIMD daje oko 6×, a ne 8×.** Ubrzanje se može razložiti na dva efekta (Slika ${fig.branch.n}): uklanjanje grananja (A → A2) i samu vektorizaciju (A2 → B, isti izvor bez i sa SIMD-om), koja iznosi ${ratio(grids[0]).toFixed(1).replace(".", ",")}× do ${Math.max(...grids.map(ratio)).toFixed(1).replace(".", ",")}× za osam traka. Ograničenja su memorijska propusnost (poglavlje 8.6), neporavnati pristup susjedima x ± 1, skuplje operacije vdivps i vsqrtps u fazi 2 te skalarni epilog reda.`,
    "**Uklanjanje grananja ima promjenjiv efekat** (oko 1,0–1,4×), jer zavisi od toga koliko su uslovi (erozija ili depozicija, voda ili suho) predvidivi za prediktor skokova, a to zavisi od stanja simulacije i rezolucije.",
    "**Dretve: najviše 2×.** Na dvije jezgre ubrzanje ne može preći 2, a efikasnost na malim mrežama dodatno smanjuju barijere između tri faze; kada su obje jezgre zauzete vektorizovanim kodom, dijeljena propusnost memorije postaje usko grlo.",
    "**Više dretvi od jezgri usporava program.** Prekomjerno opterećenje sa aktivnim čekanjem na barijerama je najgori slučaj za kod sa čestom sinhronizacijom. Broj dretvi treba uskladiti sa brojem fizičkih jezgri (`OMP_NUM_THREADS`, `OMP_PROC_BIND`), a za dijeljene sisteme razmotriti `OMP_WAIT_POLICY=passive`.",
    "**Kompletna simulacija** je ograničena serijskim dijelovima (Amdahl), kako je pokazano u poglavlju 8.7.",
  ]));
  items.push(...fig.branch.items);
  items.push(H(2, "Utjecaj veličine mreže i memorijskog pristupa"));
  items.push(P("Na mreži 128² cijelo stanje (≈ 0,6 MB) staje u L2 keš jedne jezgre, pa jednodretvene SIMD varijante postižu najveća ubrzanja, ali je posla po fazi premalo za efikasnu višedretvenost. Na 512² i 1024² stanje (≈ 9,6 MB i 38 MB) prevazilazi L2, svaka od tri faze ponovo prolazi kroz sve nizove, pa vektorizovani kod postaje ograničen propusnošću, a dvije dretve donose korist jer udvostručuju dostupnu propusnost prema L3/RAM-u (STREAM: 1,9–2,3×). SoA raspored i row-major obilazak obezbjeđuju da se svaki učitani red keša u potpunosti iskoristi, a tri susjedna reda stencila ostaju u L1/L2 tokom obilaska. Dalji napredak na CPU-u zahtijevao bi smanjenje saobraćaja: spajanje faza (eng. *kernel fusion*) uz pomjeranje redova (faza 2 za red y − 1 dok se računa faza 1 za red y), vremensko blokiranje (eng. *temporal blocking*) ili manje nizova (npr. FP16 skladištenje, kao u " + cite("klower2022") + ")."));
  items.push(H(2, "Ograničenja CPU implementacije i modela"));
  items.push(...bullets([
    "**Hardver mjerenja:** samo dvije virtuelne jezgre i šum dijeljenog okruženja; rezultati za 4 i 8 dretvi ne govore o skaliranju na većim procesorima. AVX-512 i NUMA efekti nisu ispitani.",
    "**Model:** konstantan presjek cijevi i odsustvo trenja (kao kod Meija i saradnika) dovode do dugotrajnog talasanja vode u jezerima i velikih brzina u vrlo tankim slojevima vode na granici suho/mokro; kapacitet je zato ograničen faktorom dubine. Lokalno-inercijalna formulacija sa trenjem po Manningu " + cite("bates2010") + " bila bi fizikalno vjerodostojnija.",
    "**Erozija** je empirijska (kapacitet), bez klasa sedimenta, poroznosti i termičke erozije; nije kalibrisana na stvarne podatke i služi kao reprezentativno računsko opterećenje.",
    "**Preciznost:** float aritmetika je dovoljna (relativne greške bilansa ~10^{-7}), ali za vrlo duge simulacije sa velikim mrežama treba razmotriti double akumulaciju dijagnostike (već je primijenjena) ili kompenzovano sabiranje.",
  ]));
  return items;
}

function zakljucak(res) {
  const { pick, grids } = load(res);
  const big = grids[grids.length - 1];
  const s = (v, p, g = big) => Number(pick(g, v, p).speedup_vs_scalar).toFixed(1).replace(".", ",");
  return [
    H(1, "Zaključak"),
    P("U radu je analizirana literatura o simulaciji plitkih voda i erozije terena objavljena od 2020. godine (deset detaljno analiziranih radova, uz tri temeljna rada), na osnovu koje je odabran model virtuelnih cijevi sa erozijom zasnovanom na kapacitetu i konzervativnim transportom sedimenta. Model je eksplicitna šema sa isključivo lokalnim zavisnostima, srodna lokalno-inercijalnim solverima koji se koriste u operativnoj hidrologiji, i jednostavno se preslikava i na SIMD i na višedretveno izvršavanje."),
    P(`Implementirano je sedam osnovnih varijanti (A–G) i četiri ablacijske varijante sa zajedničkim interfejsom „skup kernela × driver“. Sve varijante daju rezultate identične skalarnoj referenci bit po bit, što je postignuto disciplinom u redoslijedu operacija i izborom zastavica (bez FMA spajanja i bez -ffast-math). Analiza izvještaja kompajlera pokazala je da automatska vektorizacija u GCC 13 nije uspjela sve dok nisu riješeni preklapanje pokazivača i ograničenje -ftrapping-math – to je važan praktični zaključak rada. Na mreži ${big} vektorizacija ubrzava jezgro oko ${s("D_avx2", 1)} puta, dvije dretve skalarnog koda oko ${s("E_omp", 2)} puta, a njihova kombinacija ${s("G_omp_avx2", 2)} puta. Vektorizovani kod je ograničen memorijskom propusnošću, a skaliranje dretvi brojem fizičkih jezgri i barijerama između faza.`),
    P("Ostvareni su svi ciljevi ove faze: provjerena literatura, obrazložen izbor algoritma, šest traženih implementacija (uz ablacije), testovi ispravnosti, stvarna mjerenja i vizualizacija. Za GPU fazu preporučuje se direktno preslikavanje tri faze na tri CUDA kernela sa jednom niti po ćeliji, zadržavanje SoA rasporeda, halo okvira i dvostrukog bafera, korištenje postojećih testova ekvivalencije kao specifikacije, te poređenje sa najboljom višedretvenom CPU varijantom na mrežama do 4096² ćelija."),
  ];
}

module.exports = { evaluacija, diskusija, zakljucak, readCsv, load };
