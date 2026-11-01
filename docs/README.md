# `docs/` – dokumentacija rada

Tekstualna dokumentacija uz kod: ručno pisani pregled literature i generisani Word dokument seminarskog rada.
Opšti opis projekta: [korijenski README](../README.md).

## Sadržaj

| Fajl | Veličina | Nastanak | Opis |
|---|---|---|---|
| `literature_review.md` | 6,6 KB (45 linija) | **ručno pisan** | Pregled literature 2020–danas: temeljni radovi prije 2020, tabela od 10 analiziranih radova (pristup, metoda, paralelizacija, rezultat, veza s radom), dodatni provjereni radovi, ograničenja provjere. Oznake pristupa: `[PUNI]` (analiziran cijeli tekst) i `[SAŽETAK]` |
| `PRS_Shallow_Water_Terrain_Erosion_CPU.docx` | 2,6 MB | **generisan** (`scripts/docgen`) | Kompletan seminarski rad: uvod, model, literatura, algoritmi, implementacija, vektorizacija, paralelizacija, priprema za GPU, eksperimentalna evaluacija, literatura (IEEE), prilog |

`literature_review.md` je običan tekst koji se može čitati i uređivati bez alata. `.docx` se **ne uređuje ručno** (osim
popunjavanja polja naslovne strane): nastaje iz `scripts/docgen/*.js` i podataka iz `results/`, pa će ručne izmjene nestati
pri sljedećem generisanju. Dokument se regeneriše naredbom:

```bash
npm install docx
node scripts/docgen/build_document.js results docs/PRS_Shallow_Water_Terrain_Erosion_CPU.docx
```

U Wordu prihvatiti ažuriranje polja da se popuni automatski sadržaj.

## Ko referencira ovaj folder

| Ko | Kako |
|---|---|
| `README.md` (korijen) | odjeljci 13 i 15: struktura repozitorija, naredba za regeneraciju, link na `literature_review.md` |
| `scripts/docgen/build_document.js` | zadana izlazna putanja: `docs/PRS_Shallow_Water_Terrain_Erosion_CPU.docx`; tekst priloga pominje `docs/` |
| `scripts/README.md`, `scripts/docgen/README.md` | opis generatora |

`literature_review.md` upućuje na [`references/bibliography.bib`](../references/README.md) (BibTeX svih radova). Nijedan program
ne čita `docs/`; fajlovi su namijenjeni ljudima.

## Napomene

- `.docx` (≈ 2,6 MB) je generisan artefakt (sadrži ugrađene slike) i nije namijenjen verzionisanju u gitu.
- `literature_review.md` i poglavlje „Pregled naučne literature" u dokumentu obrađuju istu temu, ali se održavaju odvojeno;
  literatura u dokumentu dolazi iz `scripts/docgen/references.js`.
