# Pregled literature – Shallow-water and Terrain Erosion (2020–danas)

Svi bibliografski podaci provjereni su na stranici izdavača, DOI zapisu (Crossref/OpenAlex), arXiv-u ili
institucionalnom repozitoriju (novembar 2026). Oznaka pristupa: **[PUNI]** – analiziran kompletan tekst,
**[SAŽETAK]** – dostupan samo sažetak/stranica izdavača. Tvrdnje označene *(procjena)* su vlastiti zaključak,
a ne tvrdnja autora. BibTeX: `references/bibliography.bib`.

## Temeljni radovi (prije 2020)

- X. Mei, P. Decaudin, B.-G. Hu, *Fast Hydraulic Erosion Simulation and Visualization on GPU*, PG'07, IEEE, 2007, str. 47–56, doi:10.1109/PG.2007.15 – model virtuelnih cijevi i erozija zasnovana na kapacitetu (osnova implementacije).
- O. Šťava, B. Beneš, M. Brisbin, J. Křivánek, *Interactive Terrain Modeling Using Hydraulic Erosion*, SCA 2008, str. 201–210, doi:10.2312/SCA/SCA08/201-210.
- P. D. Bates, M. S. Horritt, T. J. Fewtrell, *A simple inertial formulation of the shallow water equations…*, J. Hydrology 387(1–2):33–45, 2010, doi:10.1016/j.jhydrol.2010.03.027 – lokalno-inercijalna (ACC) šema, strukturno srodna modelu cijevi.
- J. Neal i sar., *Optimisation of the two-dimensional hydraulic model LISFOOD-FP for CPU architecture* (naslov registrovan sa slovnom greškom), Environ. Model. Softw. 107:148–157, 2018, doi:10.1016/j.envsoft.2018.05.011 – vektorizovano jezgro i OpenMP; jednojezgreno ubrzanje 4,2–8,4×. [SAŽETAK]

## Radovi 2020–danas analizirani u dokumentu

| # | Rad | Pristup | Metoda | Paralelizacija | Rezultat koji navode autori | Veza sa radom |
|---|---|---|---|---|---|---|
| 1 | Nilles, Günther, Wagner, Müller, *3D Real-Time Hydraulic Erosion Simulation using Multi-Layered Heightmaps*, VMV 2024, doi:10.2312/vmv.20241211 | PUNI | Mei model cijevi + erozija; sediment kroz cijevi | CUDA, 9 kernela, nit po ćeliji, checkerboard | 1,44–1,61 ms/korak na 1024² (RTX 3070) | isti model; faze ≈ kerneli |
| 2 | Salamin, Szénási, *Simulating Hidraulic Erosion Utilizing Graphics Processors*, SISY 2024, str. 309–314, doi:10.1109/SISY62279.2024.10737605 | PUNI | Mei (5 faza, semi-Lagrange) | GPU, nit po ćeliji | 2048²: CPU 198,3 s, GPU 16,4 s (i5-13600K / GTX 1050 Ti) | isti algoritam; CPU referenca jednodretvena *(procjena)* |
| 3 | Shaw i sar., *LISFLOOD-FP 8.0…*, GMD 14(6):3577–3602, 2021, doi:10.5194/gmd-14-3577-2021 | PUNI | DG2, FV1, ACC | OpenMP po redovima; CUDA 16×16 | GPU 2,5–4× brži od 16 jezgri iznad 1 M elemenata | ACC ≈ model cijevi |
| 4 | Caviedes-Voullième i sar., *SERGHEI-SWE v1.0*, GMD 16(3):977–1008, 2023, doi:10.5194/gmd-16-977-2023 | PUNI | FV, augmentirani Roe | Kokkos (OpenMP/CUDA) + MPI | skaliranje na >20 000 CPU jezgri, do 256 GPU | halo, jedan izvor CPU/GPU |
| 5 | Morales-Hernández i sar., *TRITON*, Environ. Model. Softw. 141:105034, 2021, doi:10.1016/j.envsoft.2021.105034 | SAŽETAK + dokumentacija | FV, augmentirani Roe | OpenMP+MPI; CUDA+MPI | (OLCF) 6 GPU brže od 64 CPU čvora | dvije izvedbe istog modela |
| 6 | Hou i sar., *A GPU-based numerical model coupling hydrodynamical and morphological processes*, Int. J. Sediment Res. 35(4):386–394, 2020, doi:10.1016/j.ijsrc.2020.02.005 | SAŽETAK | Godunov FV, HLLC, MUSCL | C++/CUDA | 298,1 s GPU vs 4531,2 s CPU | sprega toka i morfologije |
| 7 | Cea i sar., *GPU-enhanced multiclass soil erosion model… Iber*, Environ. Model. Softw. 179:106098, 2024, doi:10.1016/j.envsoft.2024.106098 | SAŽETAK + dijelovi | FV (Roe/DHD), višeklasni sediment | CUDA, CUB | ~2 reda veličine vs sekvencijalni CPU | kiša → tok → erozija |
| 8 | Steffen, Amann, Hinkelmann, *Performance Improvement Strategies for an FVM-Based Shallow Water Flow Model on 2D Structured Grids*, IAHR 2022, str. 3804–3815, doi:10.3850/IAHR-39WC252171192022753 | SAŽETAK | MUSCL FV | OpenMP, MPI, SIMD (Eigen) | blok-paralelizacija najbolja (~1,3×), SIMD dodatno ubrzava | SIMD + OpenMP na mreži |
| 9 | Klöwer i sar., *Fluid Simulations Accelerated With 16 Bits…*, JAMES 14(2):e2021MS002684, 2022, doi:10.1029/2021MS002684 | PUNI | FD, Arakawa C, RK4 | SIMD, Float16 | Float16 do 3,8× vs Float64 (A64FX) | float, FTZ, memorijska ograničenost |
| 10 | Jain, Benes, Cordonnier, *Efficient Debris-flow Simulation for Steep Terrain Erosion*, ACM TOG 43(4), 2024, doi:10.1145/3658213 | PUNI | stream power + debris tok | PyTorch, GPU | 1600 iteracija na 1024² za 32 s | kontrast: globalne zavisnosti |

## Dodatni provjereni radovi (citirani ili kao podrška)

- Schott, Paris, Fournier, Guérin, Galin, *Large-scale Terrain Authoring through Interactive Erosion Simulation*, ACM TOG 42(5):1–15, 2023, doi:10.1145/3592787 [SAŽETAK] – zakon snage toka, interaktivno.
- Schott, Galin, Guérin, Paris, Peytavie, *Terrain Amplification using Multi Scale Erosion*, ACM TOG 43(4):1–12, 2024, doi:10.1145/3658200 [SAŽETAK] – GPU compute shaderi.
- Jain, Kerbl, Gain, Finley, Cordonnier, *FastFlow*, Comput. Graph. Forum 43(7):e15243, 2024, doi:10.1111/cgf.15243 [SAŽETAK].
- Sharifian i sar., *LISFLOOD-FP 8.1*, GMD 16(9):2391–2413, 2023, doi:10.5194/gmd-16-2391-2023 [PUNI] – GPU ACC solver; ACC slabiji za brze plitke tokove na strmim terenima.
- Villalobos, Caviedes-Voullième, Rizzi, Meneses, *Towards Portability at Scale…*, SBAC-PAD 2025, str. 180–191, doi:10.1109/SBAC-PAD66369.2025.00025 [SAŽETAK] – roofline: ključni kerneli ograničeni propusnošću memorije.
- Vanzo i sar., *BASEMENT v3*, Environ. Model. Softw. 143:105102, 2021, doi:10.1016/j.envsoft.2021.105102 [PUNI, arXiv preprint] – OP2: sekvencijalni, OpenMP i CUDA backend; Exner + vučeni nanos.
- Khrapov, Khoperskov, Lobachevskii J. Math. 41(8):1475–1484, 2020, doi:10.1134/S1995080220080089 [SAŽETAK] – hibridni OpenMP-CUDA SWE + sediment (nije citiran u dokumentu).
- Ostali pronađeni radovi manje relevantnosti (nisu citirani u dokumentu): Dazzi & Ferrari, Computers & Fluids 312:107050, 2026; Sharif i sar., PASC '20, doi:10.1145/3394277.3401852; Chaplygin i sar., Supercomputing Frontiers and Innovations 8(4), 2021; Morales-Hernández i sar., J. Hydroinformatics 22(5), 2020; Fernández-Pato i sar., Water 15(18):3268, 2023; Aureli i sar., Water 12(3):637, 2020; Hartley i sar., The Visual Computer 40(7), 2024.

## Ograničenja provjere

HAL (Schott 2023/2024) i OSTI (TRITON) nisu dozvolili automatski pristup punom tekstu, a ScienceDirect
stranica za Hou 2020 nije bila dostupna (sažetak preuzet iz bibliotečkog kataloga). Za te radove u dokumentu
se navode samo tvrdnje iz sažetka/zvanične dokumentacije.
