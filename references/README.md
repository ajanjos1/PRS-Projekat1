# `references/` – bibliografija

BibTeX baza literature uz rad. Opšti opis projekta: [korijenski README](../README.md).

## Sadržaj

| Fajl | Zapisa | Opis |
|---|---|---|
| `bibliography.bib` | 21 | BibTeX zapisi svih radova koji se pominju u pregledu literature; podaci provjereni na stranici izdavača, preko DOI-ja (Crossref/OpenAlex), arXiv-a ili institucionalnog repozitorija |

### Grupe zapisa (ključevi u `.bib` fajlu)

| Grupa | Ključevi |
|---|---|
| Temeljni radovi (model cijevi, erozija, ACC) | `mei2007fast`, `stava2008interactive`, `bates2010simple`, `neal2018optimisation` |
| GPU erozija terena | `nilles2024multilayer`, `salamin2024erosion`, `schott2023large`, `schott2024amplification`, `jain2024debris`, `jain2024fastflow` |
| Plitke vode i poplave (CPU/GPU, HPC) | `shaw2021lisflood`, `sharifian2023lisflood81`, `caviedes2023serghei`, `morales2021triton`, `steffen2022performance`, `villalobos2025portability`, `vanzo2021basement`, `hou2020gpu`, `cea2024iber`, `khrapov2020gpu` |
| Numerika i SIMD | `klower2022float16` |

Osnova implementacije je rad **Mei, Decaudin, Hu, 2007** (`mei2007fast`).

## Ko referencira ovaj folder

| Ko | Kako |
|---|---|
| `docs/literature_review.md` | navodi „BibTeX: `references/bibliography.bib`" |
| `README.md` (korijen) | odjeljak 15 |
| `docs/ALGORITHM_EXPLANATION.md` | odjeljak 14 (literatura) |

Nijedan program ne parsira `.bib` fajl; služi kao izvor istine za ljude i za eventualni LaTeX/BibTeX rad.

## Napomene

- Fajl je mali (≈ 8,9 KB, 102 linije) čist tekst, pa je pogodan za verzionisanje u gitu.
