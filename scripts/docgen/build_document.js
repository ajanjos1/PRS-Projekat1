// Generisanje seminarskog rada: node scripts/docgen/build_document.js [results] [izlaz.docx]
const fs = require("fs");
const path = require("path");
const H_ = require("./helpers");
const { d, P, H, FIG, runs, FONT, bullets, CODE } = H_;
const { REFS, order } = require("./references");
const A = require("./chapters_a");
const B = require("./chapters_b");
const C = require("./chapters_c");

const ROOT = path.resolve(__dirname, "..", "..");
const RES = path.resolve(process.argv[2] || path.join(ROOT, "results"));
const OUT = path.resolve(process.argv[3] || path.join(ROOT, "docs", "PRS_Shallow_Water_Terrain_Erosion_CPU.docx"));
const FIGDIR = path.join(RES, "figures");

// ------------------------------------------------------------------ naslovna strana
function titlePage() {
  const c = (text, size, opts = {}) => new d.Paragraph({
    alignment: d.AlignmentType.CENTER, spacing: { before: opts.before || 0, after: opts.after || 120, line: 300 },
    children: [new d.TextRun({ text, size, bold: opts.bold, italics: opts.italics, font: FONT })],
  });
  const field = (label) => new d.Paragraph({
    spacing: { before: 0, after: 160, line: 300 }, indent: { left: 1700 },
    children: [new d.TextRun({ text: label, size: 24, bold: true }), new d.TextRun({ text: " _______________________________________", size: 24 })],
  });
  return [
    c("UNIVERZITET U SARAJEVU", 28, { bold: true, before: 400 }),
    c("ELEKTROTEHNIČKI FAKULTET", 28, { bold: true, after: 1400 }),
    c("Predmet: Paralelni računarski sistemi", 26, { after: 1000 }),
    c("Shallow-water and Terrain Erosion", 40, { bold: true, after: 200 }),
    c("Simulacija plitkih voda i erozije terena", 30, { after: 600 }),
    c("Seminarski rad – CPU paralelizacija i vektorizacija", 26, { italics: true, after: 1800 }),
    field("Studenti:"),
    field(""),
    field("Profesor / asistent:"),
    field("Akademska godina:"),
    c("Sarajevo, novembar 2026.", 24, { before: 1200 }),
  ];
}

// ------------------------------------------------------------------ literatura (IEEE)
function literaturaSection() {
  const items = [H(1, "Literatura")];
  order.forEach((k, i) => {
    items.push(new d.Paragraph({
      alignment: d.AlignmentType.LEFT,
      spacing: { after: 80, line: 276 },
      indent: { left: 567, hanging: 567 },
      children: [new d.TextRun({ text: `[${i + 1}]\t`, size: 21 }), ...runs(REFS[k], { size: 21 })],
      tabStops: [{ type: d.TabStopType.LEFT, position: 567 }],
    }));
  });
  return items;
}

function prilog() {
  return [
    H(1, "Prilog A. Struktura projekta i pokretanje", false),
    P("Projekat se prevodi CMake-om (C++20, GCC/Clang/MSVC) i ne zavisi od vanjskih biblioteka osim OpenMP-a. Kompletan eksperimentalni postupak opisan je u datoteci README.md; osnovne komande su:"),
    ...CODE(`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/prs_tests results/correctness_report.csv        # testovi ispravnosti
./build/prs_swe info                                      # okruženje i varijante
./build/prs_swe bench --sizes 128,256,512,1024 --threads 1,2,4,8 --reps 10
./build/prs_swe simulate --size 512 --steps 6000 --variant G_omp_avx2 --threads 2
python3 scripts/visualize_simulation.py results/sim512 results/figures
python3 scripts/plot_results.py results results/figures
bash scripts/run_benchmarks.sh                            # sve navedeno odjednom`),
    P("Direktoriji: `include/` i `src/` (implementacija), `tests/` (testovi), `scripts/` (mjerenja, grafikoni i generisanje ovog dokumenta), `results/` (CSV rezultati, izvještaji kompajlera, slike), `references/bibliography.bib` (literatura u BibTeX formatu), `docs/` (ovaj dokument i pregled literature)."),
  ];
}

// ------------------------------------------------------------------ podaci za poglavlje 6
function vecTables() {
  const initial = [
    ["kernels_scalar.cpp (A, E): sve 3 petlje", "nije vektorizovano (namjerno)", "nije vektorizovano (namjerno)"],
    ["kernels_auto.cpp (B): faza 1", "nije – „control flow in loop“", "da – 32 B vektori + 16 B epilog"],
    ["kernels_auto.cpp (B): faza 2", "nije – „control flow in loop“", "da – 32 B vektori + 16 B epilog"],
    ["kernels_auto.cpp (B): faza 3", "da, ali uz runtime provjeru preklapanja", "da – 32 B + 16 B, bez provjere"],
    ["kernels_omp_simd.cpp (C): faze 1 i 2", "nije – „control flow in loop“", "da – 32 B vektori"],
    ["kernels_omp_simd.cpp (C): faza 3", "da – 32 B vektori", "da – 32 B vektori"],
  ];
  const lines = fs.readFileSync(path.join(RES, "vectorization", "disassembly_summary.txt"), "utf8").trim().split("\n").slice(1);
  const disasm = [["Objektni fajl", "ymm instr.", "pakovane (…ps)", "skalarne (…ss)", "vsqrtps", "vdivps"]];
  lines.forEach((l) => { const v = l.trim().split(/\s+/); disasm.push([v[0] + ".o", v[1], v[2], v[3], v[4], v[5]]); });
  return { initial, disasm };
}

function figures() {
  const f = (name, title, w = 1.0) => FIG(path.join(FIGDIR, name), title, w);
  // redoslijed kreiranja = redoslijed pojavljivanja u dokumentu (numeracija slika)
  return {
    speedup: f("perf_speedup_variants.png", "Ubrzanje numeričkog jezgra u odnosu na skalarnu referencu A za sve mreže (medijan od 10 mjerenja; E, F i G sa dvije dretve)", 0.92),
    scaling: f("perf_thread_scaling.png", "Ubrzanje S(p) i efikasnost E(p) sa brojem OpenMP dretvi; osjenčeno područje: više dretvi nego fizičkih jezgri", 0.85),
    bandwidth: f("perf_bandwidth.png", "Efektivna memorijska propusnost (model 112 B/ćeliji/koraku) u odnosu na STREAM triad mjerenja", 0.82),
    e2e: f("perf_kernel_vs_e2e.png", "Ubrzanje numeričkog jezgra i kompletne simulacije u odnosu na A (E i G sa dvije dretve)", 0.85),
    terrain: f("sim_terrain_before_after.png", "Teren 512 × 512 prije i nakon 300 s simulirane erozije (osjenčeni prikaz visinske mape)", 0.85),
    diff: f("sim_terrain_difference.png", "Razlika visine terena: crveno erozija, plavo depozicija", 0.55),
    water: f("sim_water_velocity.png", "Dubina vode i brzina toka na kraju simulacije", 0.85),
    mass: f("sim_mass_balance.png", "Bilans vode i sedimenta (lijevo) i relativna promjena ukupne količine materijala Σ(b + s) (desno)", 0.82),
    branch: f("perf_branch_simd_decomposition.png", "Vrijeme po ćeliji i koraku: efekat uklanjanja grananja (A → A2) i SIMD vektorizacije (A2 → B)", 0.82),
  };
}

// ------------------------------------------------------------------ sastavljanje
async function main() {
  const front = [...A.sazetak()];
  const body = [];
  body.push(...A.uvod());
  body.push(...A.problem());
  body.push(...A.literatura());
  body.push(...A.literaturaTabela());
  body.push(...A.algoritmi());
  body.push(...B.implementacija());
  const vt = vecTables();
  body.push(...B.vektorizacija(vt.initial, vt.initial, vt.disasm));
  body.push(...B.paralelizacija());
  const figs = figures();
  body.push(...C.evaluacija(RES, figs));
  body.push(...C.diskusija(RES, figs));
  body.push(...B.gpu());
  body.push(...C.zakljucak(RES));
  body.push(...literaturaSection());

  const doc = new d.Document({
    creator: "PRS seminarski rad",
    title: "Shallow-water and Terrain Erosion – CPU paralelizacija i vektorizacija",
    description: "Seminarski rad, Paralelni računarski sistemi, ETF Sarajevo",
    features: { updateFields: true },
    styles: {
      default: { document: { run: { font: FONT, size: 24 }, paragraph: { spacing: { line: 360 } } } },
      paragraphStyles: [
        { id: "Heading1", name: "Heading 1", basedOn: "Normal", next: "Normal", quickFormat: true,
          run: { font: FONT, size: 32, bold: true }, paragraph: { spacing: { before: 240, after: 200, line: 300 }, outlineLevel: 0 } },
        { id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true,
          run: { font: FONT, size: 28, bold: true }, paragraph: { spacing: { before: 280, after: 120, line: 300 }, outlineLevel: 1 } },
        { id: "Heading3", name: "Heading 3", basedOn: "Normal", next: "Normal", quickFormat: true,
          run: { font: FONT, size: 24, bold: true, italics: true }, paragraph: { spacing: { before: 200, after: 100, line: 300 }, outlineLevel: 2 } },
      ],
    },
    numbering: {
      config: [
        { reference: "headings", levels: [
          { level: 0, format: d.LevelFormat.DECIMAL, text: "%1.", alignment: d.AlignmentType.LEFT, suffix: d.LevelSuffix.SPACE },
          { level: 1, format: d.LevelFormat.DECIMAL, text: "%1.%2.", alignment: d.AlignmentType.LEFT, suffix: d.LevelSuffix.SPACE },
          { level: 2, format: d.LevelFormat.DECIMAL, text: "%1.%2.%3.", alignment: d.AlignmentType.LEFT, suffix: d.LevelSuffix.SPACE },
        ] },
        { reference: "bullets", levels: [
          { level: 0, format: d.LevelFormat.BULLET, text: "•", alignment: d.AlignmentType.LEFT, style: { paragraph: { indent: { left: 567, hanging: 283 } } } },
          { level: 1, format: d.LevelFormat.BULLET, text: "–", alignment: d.AlignmentType.LEFT, style: { paragraph: { indent: { left: 1134, hanging: 283 } } } },
        ] },
        { reference: "numlist", levels: [
          { level: 0, format: d.LevelFormat.DECIMAL, text: "%1.", alignment: d.AlignmentType.LEFT, style: { paragraph: { indent: { left: 567, hanging: 340 } } } },
        ] },
      ],
    },
    sections: [
      { properties: { page: { size: { width: 11906, height: 16838 }, margin: { top: 1418, bottom: 1418, left: 1418, right: 1418 } } },
        children: titlePage() },
      { properties: { page: { size: { width: 11906, height: 16838 }, margin: { top: 1418, bottom: 1418, left: 1418, right: 1418 } } },
        children: [
          ...front,
          new d.Paragraph({ pageBreakBefore: true, spacing: { after: 240 }, children: [new d.TextRun({ text: "Sadržaj", bold: true, size: 32 })] }),
          new d.TableOfContents("Sadržaj", { hyperlink: true, headingStyleRange: "1-3" }),
        ] },
      { properties: { page: { size: { width: 11906, height: 16838 }, margin: { top: 1418, bottom: 1418, left: 1418, right: 1418 }, pageNumbers: { start: 1 } } },
        footers: { default: new d.Footer({ children: [new d.Paragraph({ alignment: d.AlignmentType.CENTER,
          children: [new d.TextRun({ children: [d.PageNumber.CURRENT], size: 20 })] })] }) },
        children: body },
    ],
  });
  const buf = await d.Packer.toBuffer(doc);
  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  fs.writeFileSync(OUT, buf);
  console.log("Zapisano:", OUT, `(${(buf.length / 1024).toFixed(0)} KB), referenci: ${order.length}`);
}

main().catch((e) => { console.error(e); process.exit(1); });
