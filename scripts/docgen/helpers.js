// Pomoćne funkcije za generisanje Word dokumenta (docx-js): stilovi, naslovi, formule, tabele, slike, kod.
const fs = require("fs");
const path = require("path");
const d = require("docx");

const FONT = "Times New Roman";
const MONO = "Consolas";
const TEXT_WIDTH_DXA = 9070; // A4 (11906) - 2 x 1418 (2,5 cm margine)
const TEXT_WIDTH_PX = 600;   // približno 6,3 in pri 96 dpi

const BORDER = { style: d.BorderStyle.SINGLE, size: 4, color: "A6A5A0" };
const CELL_BORDERS = { top: BORDER, bottom: BORDER, left: BORDER, right: BORDER };

// ------------------------------------------------------------------ tekst sa jednostavnim markupom
// **podebljano**, *kurziv*, `kod`, ^{gornji}, _{donji}
function runs(text, base = {}) {
  const out = [];
  const re = /(\*\*[^*]+\*\*|\*[^*]+\*|`[^`]+`|\^\{[^}]+\}|_\{[^}]+\})/g;
  let last = 0;
  let m;
  while ((m = re.exec(text)) !== null) {
    if (m.index > last) out.push(new d.TextRun({ text: text.slice(last, m.index), ...base }));
    const tok = m[0];
    if (tok.startsWith("**")) out.push(new d.TextRun({ text: tok.slice(2, -2), bold: true, ...base }));
    else if (tok.startsWith("`")) out.push(new d.TextRun({ text: tok.slice(1, -1), font: MONO, size: (base.size || 24) - 3, ...stripFont(base) }));
    else if (tok.startsWith("^{")) out.push(new d.TextRun({ text: tok.slice(2, -1), superScript: true, ...base }));
    else if (tok.startsWith("_{")) out.push(new d.TextRun({ text: tok.slice(2, -1), subScript: true, ...base }));
    else out.push(new d.TextRun({ text: tok.slice(1, -1), italics: true, ...base }));
    last = m.index + tok.length;
  }
  if (last < text.length) out.push(new d.TextRun({ text: text.slice(last), ...base }));
  return out;
}
function stripFont(b) { const c = { ...b }; delete c.font; delete c.size; return c; }

function P(text, opts = {}) {
  return new d.Paragraph({
    alignment: opts.align || d.AlignmentType.JUSTIFIED,
    spacing: { after: opts.after ?? 120, before: opts.before ?? 0, line: opts.line ?? 360 },
    indent: opts.indent,
    keepNext: opts.keepNext,
    children: Array.isArray(text) ? text : runs(text, opts.run || {}),
  });
}

// ------------------------------------------------------------------ naslovi (numerisani)
const PAGE_BREAK_H1 = new Set(["Sažetak", "Uvod", "Literatura", "Prilog A. Struktura projekta i pokretanje"]);
function H(level, text, numbered = true) {
  const heading = [d.HeadingLevel.HEADING_1, d.HeadingLevel.HEADING_2, d.HeadingLevel.HEADING_3][level - 1];
  return new d.Paragraph({
    heading,
    numbering: numbered ? { reference: "headings", level: level - 1 } : undefined,
    pageBreakBefore: level === 1 && PAGE_BREAK_H1.has(text),
    keepNext: true,
    children: [new d.TextRun({ text })],
  });
}

// ------------------------------------------------------------------ liste
function bullets(items, level = 0) {
  return items.map((t) => new d.Paragraph({
    numbering: { reference: "bullets", level },
    alignment: d.AlignmentType.JUSTIFIED,
    spacing: { after: 60, line: 340 },
    children: runs(t),
  }));
}
let numListInstance = 0;
function numbered(items) {
  numListInstance += 1;
  return items.map((t) => new d.Paragraph({
    numbering: { reference: "numlist", level: 0, instance: numListInstance },
    alignment: d.AlignmentType.JUSTIFIED,
    spacing: { after: 60, line: 340 },
    children: runs(t),
  }));
}

// ------------------------------------------------------------------ matematika (OMML)
function marr(x) {
  if (x === undefined || x === null) return [];
  if (Array.isArray(x)) return x.flatMap(marr);
  if (typeof x === "string") return [new d.MathRun(x)];
  return [x];
}
const M = {
  frac: (n, den) => new d.MathFraction({ numerator: marr(n), denominator: marr(den) }),
  sub: (b, s) => new d.MathSubScript({ children: marr(b), subScript: marr(s) }),
  sup: (b, s) => new d.MathSuperScript({ children: marr(b), superScript: marr(s) }),
  subsup: (b, s, p) => new d.MathSubSuperScript({ children: marr(b), subScript: marr(s), superScript: marr(p) }),
  sqrt: (x) => new d.MathRadical({ children: marr(x) }),
  par: (...x) => new d.MathRoundBrackets({ children: marr(x) }),
  sq: (...x) => new d.MathSquareBrackets({ children: marr(x) }),
  sum: (body, lower, upper) => new d.MathSum({ children: marr(body), subScript: marr(lower), superScript: upper ? marr(upper) : undefined }),
  pd: (f, v) => new d.MathFraction({ numerator: marr(["∂", f]), denominator: marr(["∂", v]) }),
};

let eqCounter = 0;
function EQ(content, opts = {}) {
  eqCounter += 1;
  const n = eqCounter;
  return {
    n,
    para: new d.Paragraph({
      tabStops: [
        { type: d.TabStopType.CENTER, position: Math.round(TEXT_WIDTH_DXA / 2) },
        { type: d.TabStopType.RIGHT, position: TEXT_WIDTH_DXA },
      ],
      spacing: { before: 120, after: 120, line: 240, lineRule: d.LineRuleType.AT_LEAST },
      keepNext: opts.keepNext,
      children: [new d.TextRun({ text: "\t" }), new d.Math({ children: marr(content) }), new d.TextRun({ text: `\t(${n})` })],
    }),
  };
}
function inlineMath(content) { return new d.Math({ children: marr(content) }); }

// ------------------------------------------------------------------ tabele
let tableCounter = 0;
function caption(prefix, n, text) {
  return new d.Paragraph({
    alignment: d.AlignmentType.CENTER,
    spacing: { before: 60, after: 200, line: 276 },
    keepNext: prefix === "Tabela",
    children: [
      new d.TextRun({ text: `${prefix} `, bold: true, size: 20 }),
      new d.SimpleField(`SEQ ${prefix} \\* ARABIC`, String(n)),
      ...runs(`. ${text}`, { size: 20 }),
    ],
  });
}

function cellPara(text, opts) {
  const base = { size: opts.size || 18, bold: opts.bold, font: opts.mono ? MONO : undefined };
  return new d.Paragraph({
    keepNext: opts.keepNext,
    alignment: opts.align || d.AlignmentType.LEFT,
    spacing: { after: 0, before: 0, line: 240 },
    children: runs(String(text), base),
  });
}

// rows: niz nizova stringova; widths: udio (zbir 1) ili DXA; header: prvi red je zaglavlje
function TABLE(title, rows, widths, opts = {}) {
  tableCounter += 1;
  const n = tableCounter;
  const total = TEXT_WIDTH_DXA;
  const sum = widths.reduce((a, b) => a + b, 0);
  let cols = widths.map((w) => Math.floor((w / sum) * total));
  cols[cols.length - 1] += total - cols.reduce((a, b) => a + b, 0);
  const aligns = opts.align || [];
  const trs = rows.map((r, ri) => new d.TableRow({
    tableHeader: ri === 0,
    cantSplit: true,
    children: r.map((c, ci) => new d.TableCell({
      width: { size: cols[ci], type: d.WidthType.DXA },
      borders: CELL_BORDERS,
      shading: ri === 0 ? { type: d.ShadingType.CLEAR, color: "auto", fill: "E9E8E4" } : undefined,
      margins: { top: 50, bottom: 50, left: 80, right: 80 },
      verticalAlign: d.VerticalAlign.CENTER,
      children: String(c).split("\n").map((line) => cellPara(line, {
        bold: ri === 0, size: opts.size,
        align: ri === 0 ? d.AlignmentType.CENTER : (aligns[ci] === "r" ? d.AlignmentType.RIGHT : aligns[ci] === "c" ? d.AlignmentType.CENTER : d.AlignmentType.LEFT),
        mono: opts.monoCols && opts.monoCols.includes(ci) && ri > 0,
        keepNext: rows.length <= 9 && ri < rows.length - 1, // male tabele drži na jednoj strani
      })),
    })),
  }));
  const table = new d.Table({ width: { size: total, type: d.WidthType.DXA }, columnWidths: cols, rows: trs });
  return { n, items: [caption("Tabela", n, title), table, new d.Paragraph({ spacing: { after: 120 }, children: [] })] };
}

// ------------------------------------------------------------------ slike
function pngSize(file) {
  const buf = fs.readFileSync(file);
  return { w: buf.readUInt32BE(16), h: buf.readUInt32BE(20) };
}
let figCounter = 0;
function FIG(file, title, widthFrac = 1.0) {
  figCounter += 1;
  const n = figCounter;
  const { w, h } = pngSize(file);
  const width = Math.round(TEXT_WIDTH_PX * widthFrac);
  const height = Math.round((width * h) / w);
  return {
    n,
    items: [
      new d.Paragraph({
        alignment: d.AlignmentType.CENTER, keepNext: true,
        spacing: { before: 120, after: 0, line: 240, lineRule: d.LineRuleType.AT_LEAST },
        children: [new d.ImageRun({ type: "png", data: fs.readFileSync(file), transformation: { width, height },
          altText: { title: `Slika ${n}`, description: title, name: path.basename(file) } })],
      }),
      caption("Slika", n, title),
    ],
  };
}

// ------------------------------------------------------------------ kod
function CODE(text) {
  const lines = text.replace(/\s+$/, "").split("\n");
  return lines.map((l, i) => new d.Paragraph({
    shading: { type: d.ShadingType.CLEAR, color: "auto", fill: "F3F2EF" },
    border: i === 0 ? { top: { style: d.BorderStyle.SINGLE, size: 4, color: "C9C8C3", space: 2 } }
      : (i === lines.length - 1 ? { bottom: { style: d.BorderStyle.SINGLE, size: 4, color: "C9C8C3", space: 2 } } : undefined),
    spacing: { before: 0, after: i === lines.length - 1 ? 160 : 0, line: 228 },
    keepLines: true,
    keepNext: i < lines.length - 1,
    children: [new d.TextRun({ text: l.length ? l : " ", font: MONO, size: 16 })],
  }));
}

module.exports = { d, FONT, MONO, TEXT_WIDTH_DXA, runs, P, H, bullets, numbered, M, EQ, inlineMath, TABLE, FIG, CODE, caption };
