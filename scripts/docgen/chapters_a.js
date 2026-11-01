// Poglavlja 1-4: uvod, problem, literatura, algoritmi.
const { d, P, H, bullets, numbered, M, EQ, TABLE, FIG } = require("./helpers");
const { cite } = require("./references");

function sazetak() {
  return [
    H(1, "Sažetak", false),
    P("U radu je razmatran problem simulacije plitkih voda (eng. *shallow water*) i hidrauličke erozije terena sa stanovišta paralelnog računarstva na procesoru. Na osnovu pregleda literature objavljene od 2020. godine odabran je model virtuelnih cijevi (eng. *virtual pipe model*) Meija i saradnika, eksplicitna šema na regularnoj mreži srodna lokalno-inercijalnoj aproksimaciji jednačina plitkih voda, proširen erozijom i depozicijom zasnovanom na kapacitetu transporta i konzervativnim transportom sedimenta kroz iste cijevi. Model je implementiran u jeziku C++20 u sedam osnovnih varijanti: skalarna referenca, automatska vektorizacija, OpenMP SIMD, AVX2 intrinsics, OpenMP višedretvena verzija te kombinacije dretvi sa OpenMP SIMD i AVX2 vektorizacijom, uz dodatne ablacijske varijante. Sve varijante dijele isti numerički model, raspored podataka (SoA, halo okvir, poravnati redovi) i redoslijed aritmetičkih operacija, pa daju rezultate identične skalarnoj referenci bit po bit, što je potvrđeno sa 335 automatskih testova (ekvivalencija, očuvanje mase, mirovanje jezera, simetrija, zatvoreni rubovi, stabilnost). Mjerenja na mrežama od 128 × 128 do 1024 × 1024 ćelija pokazuju ubrzanja reda veličine u odnosu na skalarnu referencu, pri čemu je skaliranje sa brojem dretvi ograničeno brojem fizičkih jezgri i memorijskom propusnošću. Arhitektura (kerneli po fazama, dekompozicija po ćelijama, dvostruko baferovanje) projektovana je tako da se direktno preslika na GPU u drugoj fazi rada."),
    P("**Ključne riječi:** plitke vode, hidraulička erozija, model virtuelnih cijevi, SIMD, AVX2, OpenMP, vektorizacija, paralelno računarstvo.", { after: 200 }),
  ];
}

function uvod() {
  return [
    H(1, "Uvod"),
    H(2, "Motivacija"),
    P("Voda koja teče preko terena oblikuje reljef: odnosi materijal sa strmih padina, usijeca kanale i taloži sediment u udolinama i jezerima. Numerička simulacija ovih procesa koristi se u hidrologiji i zaštiti od poplava, u procjeni erozije tla, ali i u računarskoj grafici i video igrama, gdje se erozijom proceduralno generišu uvjerljivi pejzaži " + cite("mei2007", "stava2008") + ". Zajednička karakteristika svih ovih primjena je velika računska zahtjevnost: realistične mreže imaju stotine hiljada do miliona ćelija, a simulacija zahtijeva hiljade vremenskih koraka."),
    P("Istovremeno, problem ima strukturu koja je gotovo idealna za paralelno računanje. Ažuriranje svake ćelije u jednom koraku zavisi samo od nje same i od četiri susjedne ćelije (stencil), pa se sve ćelije mogu računati nezavisno, uz sinhronizaciju između faza koraka. Zato se ova klasa problema prirodno preslikava i na SIMD jedinice savremenih procesora i na grafičke procesore " + cite("shaw2021", "caviedes2023") + "."),
    H(2, "Značaj simulacije plitkih voda i erozije"),
    P("Jednačine plitkih voda (eng. *Shallow Water Equations*, SWE) su osnova operativnih modela za poplave, kao što su LISFLOOD-FP, SERGHEI-SWE i TRITON " + cite("shaw2021", "caviedes2023", "morales2021") + ", dok spregnuti modeli toka i transporta sedimenta služe za procjenu promjena korita i erozije tla " + cite("hou2020", "cea2024") + ". U računarskoj grafici, hidraulička erozija na visinskim mapama (eng. *heightmap*) i dalje je aktivno područje istraživanja, od interaktivnih alata do višeslojnih modela sa prevjesima " + cite("nilles2024", "schott2023") + "."),
    H(2, "Ciljevi rada"),
    P("Ova faza seminarskog rada fokusirana je isključivo na procesor (CPU). Ciljevi su:"),
    ...numbered([
      "istražiti i kritički analizirati najmanje pet provjerenih naučnih radova iz perioda od 2020. godine do danas;",
      "odabrati i obrazložiti numerički algoritam za simulaciju toka vode i erozije koji je pogodan za CPU i kasniju GPU paralelizaciju;",
      "implementirati serijsku referencu i optimizovane varijante: automatsku vektorizaciju, OpenMP SIMD, AVX2 intrinsics, OpenMP višedretvenost i njihove kombinacije, sa zajedničkim interfejsom i ekvivalentnim numeričkim modelom;",
      "provjeriti numeričku ispravnost svih varijanti u odnosu na referencu i fizikalne zakone očuvanja;",
      "izmjeriti i analizirati performanse (ubrzanje, efikasnost, propusnost) na mrežama od 128 × 128 do 1024 × 1024 ćelija;",
      "pripremiti arhitekturu i plan za GPU implementaciju u drugoj fazi.",
    ]),
    P("Rad je organizovan na sljedeći način. Poglavlje 2 opisuje fizikalnu pozadinu i računarske izazove, poglavlje 3 daje pregled literature, a poglavlje 4 analizu algoritama i obrazloženje odabira. Poglavlja 5–7 opisuju implementaciju, vektorizaciju i višedretvenu paralelizaciju, poglavlja 8 i 9 eksperimentalne rezultate i diskusiju, a poglavlje 10 plan za GPU fazu.", { before: 120 }),
  ];
}

function problem() {
  const e1 = EQ([M.pd("h", "t"), "+", M.pd(["(hu)"], "x"), "+", M.pd(["(hv)"], "y"), "=", "R", "−", "E"]);
  const e2 = EQ([M.pd(["(hu)"], "t"), "+", M.pd(M.par(["h", M.sup("u", "2"), "+", M.frac("1", "2"), "g", M.sup("h", "2")]), "x"), "+", M.pd(["(huv)"], "y"), "=", "−gh", M.pd(M.sub("z", "b"), "x"), "−", M.frac(M.sub("τ", "bx"), "ρ")]);
  const e3 = EQ([M.pd(["(hv)"], "t"), "+", M.pd(["(huv)"], "x"), "+", M.pd(M.par(["h", M.sup("v", "2"), "+", M.frac("1", "2"), "g", M.sup("h", "2")]), "y"), "=", "−gh", M.pd(M.sub("z", "b"), "y"), "−", M.frac(M.sub("τ", "by"), "ρ")]);
  const e4 = EQ([M.par(["1", "−", "λ"]), M.pd(M.sub("z", "b"), "t"), "+", "∇·", M.sub("q", "s"), "=", "0"]);
  const tb = TABLE("Fizičke veličine u modelu plitkih voda i erozije", [
    ["Oznaka", "Veličina", "Jedinica", "Predstavljanje u implementaciji"],
    ["h, d", "dubina vode", "m", "niz `d` (float po ćeliji)"],
    ["z_{b}, b", "visina terena (dna)", "m", "dva niza `b` (dvostruki bafer)"],
    ["η = b + d", "nivo vodene površine", "m", "računa se u letu"],
    ["u, v", "brzina u smjeru x i y", "m/s", "izvodi se iz protoka i srednje dubine"],
    ["hu, hv", "jedinični protok", "m²/s", "protoci f_{L}, f_{R}, f_{T}, f_{B} kroz cijevi [m³/s]"],
    ["s", "suspendovani sediment", "m", "niz `s`"],
    ["R, E", "kiša i isparavanje", "m/s, 1/s", "parametri `rain_rate`, `evaporation`"],
  ], [1.1, 2.0, 0.9, 3.2]);
  return [
    H(1, "Predmet i problem proučavanja"),
    H(2, "Fizikalna pozadina: plitke vode"),
    P("Tok se naziva plitkim kada je horizontalna razmjera pojave (dužina talasa, širina rijeke, veličina poplavne ravnice) mnogo veća od dubine vode. Tada je vertikalno ubrzanje zanemarivo, pritisak je hidrostatički, a horizontalna brzina se može opisati prosjekom po dubini. Trodimenzionalne Navier–Stokesove jednačine se integracijom po dubini svode na dvodimenzionalni sistem u kojem su nepoznate samo dubina vode i dvije komponente brzine. Model se koristi za poplave i probijanje brana, tok u rijekama i estuarijima, površinsko oticanje kiše, cunamije, kao i za interaktivnu simulaciju vode u grafici."),
    P("Prednost SWE je u tome što zadržavaju bitnu fiziku (očuvanje mase i količine kretanja, talase, ulijevanje vode u depresije, suhe i mokre zone), a rade na dvodimenzionalnoj mreži, pa složenost raste sa kvadratom, a ne sa kubom rezolucije. Topografija se predstavlja visinskom mapom z_{b}(x, y), voda dubinom h(x, y), a tok brzinom (u, v) ili jediničnim protokom (hu, hv)."),
    H(2, "Matematički model"),
    P("U konzervativnom obliku, 2D jednačine plitkih voda sastoje se od zakona očuvanja mase (1) i zakona očuvanja količine kretanja u pravcima x (2) i y (3):", { keepNext: true }),
    e1.para, e2.para, e3.para,
    P("gdje je h dubina vode, u i v komponente brzine usrednjene po dubini, g gravitaciono ubrzanje, z_{b} visina dna, R intenzitet kiše, E isparavanje, a τ_{b}/ρ napon trenja na dnu podijeljen gustinom vode. Lijeve strane su advekcija i hidrostatički pritisak, a desne strane izvorni članovi: nagib dna pokreće vodu nizbrdo, a trenje je usporava. U kompaktnom obliku sistem glasi ∂U/∂t + ∂F(U)/∂x + ∂G(U)/∂y = S(U), sa vektorom stanja U = (h, hu, hv)^{T}, što je polazište za metode konačnih zapremina " + cite("caviedes2023") + "."),
    ...tb.items,
    H(2, "Hidraulička erozija i transport sedimenta"),
    P("Hidraulička erozija je odnošenje materijala sa površine terena djelovanjem vode koja teče. Voda koja teče brzo i niz strminu može nositi više materijala, tj. ima veći kapacitet transporta. Ako voda nosi manje sedimenta od kapaciteta, ona otkida materijal sa dna (erozija); ako nosi više, višak se taloži (depozicija). Suspendovani sediment se zatim kreće zajedno sa vodom, pa se materijal premješta sa strmih i brzih dijelova toka u mirne zone, jezera i podnožja padina."),
    P("U geonaukama se promjena visine dna opisuje Exnerovom jednačinom (4), u kojoj je λ poroznost, a q_{s} protok sedimenta, koji se računa empirijskim formulama za vučeni i suspendovani nanos " + cite("hou2020", "vanzo2021") + ". U računarskoj grafici koristi se jednostavniji, ali vizuelno uvjerljiv model kapaciteta C = K_{c} sin(α) |v|, gdje je α lokalni nagib terena, a |v| brzina toka " + cite("mei2007") + ". U oba pristupa teren više nije statičan: visina b se mijenja u svakom koraku, a promijenjeni teren utiče na tok vode u narednom koraku. Hidrodinamički model i model erozije su zato spregnuti preko dvije veličine: brzina toka određuje eroziju, a erozija mijenja topografiju koja određuje tok.", { keepNext: false }),
    e4.para,
    H(2, "Računarski izazovi"),
    ...bullets([
      "**Veliki broj ćelija.** Mreža 1024 × 1024 ima preko milion ćelija, a sa devet float nizova stanje zauzima oko 38 MB, što prevazilazi L2 keš većine procesora.",
      "**Veliki broj vremenskih koraka.** Eksplicitne šeme imaju korak ograničen CFL uslovom, pa je za nekoliko minuta simuliranog vremena potrebno hiljade koraka.",
      "**Zavisnosti između susjednih ćelija.** Ćelija čita vrijednosti četiri susjeda. Unutar jedne faze susjedi se samo čitaju, ali između faza postoje zavisnosti (protoci se moraju završiti prije ažuriranja dubine), pa je potrebna barijera.",
      "**Memorijska propusnost i lokalnost.** Po ćeliji se izvrši relativno malo operacija u odnosu na broj pročitanih i upisanih bajtova, pa je algoritam pretežno ograničen propusnošću memorije, kako je pokazano i za SERGHEI-SWE " + cite("villalobos2025") + ". Ključni su kontinuirani pristup (SoA), poravnanje i ponovna upotreba susjednih redova iz keša.",
      "**Granice paralelizacije.** Unutar faze nema zavisnosti, ali sinhronizacija između faza, serijski dijelovi (inicijalizacija terena, rubni uslovi) i zasićenje memorijske magistrale ograničavaju skaliranje (Amdahlov zakon).",
      "**Numerička stabilnost.** Prevelik vremenski korak izaziva oscilacije i negativne dubine; potrebni su CFL uslov i ograničenje izlaznog protoka.",
      "**Race condition.** Ako bi se teren ažurirao na mjestu dok susjedne ćelije čitaju njegov nagib, rezultat bi zavisio od redoslijeda izvršavanja dretvi. Rješenje je dvostruko baferovanje (eng. *double buffering*) i podjela koraka na faze.",
    ]),
    P("Problem je pogodan za paralelno računanje jer je rad ravnomjerno raspoređen po ćelijama (dobar balans opterećenja uz statičko raspoređivanje), unutar faze nema prenesenih zavisnosti ni redukcija, pristup memoriji je regularan, a svaka ćelija izvršava isti niz operacija nad različitim podacima, što je upravo SIMD/SIMT model izvršavanja.", { before: 120 }),
  ];
}

function literatura() {
  const items = [];
  items.push(H(1, "Pregled naučne literature"));
  items.push(H(2, "Metodologija pretrage"));
  items.push(P("Pretraga je obuhvatila Google Scholar, IEEE Xplore, ACM Digital Library, ScienceDirect, SpringerLink, Eurographics Digital Library, arXiv i časopise Copernicus (GMD) i MDPI, uz ključne riječi *shallow water equations*, *hydraulic erosion*, *virtual pipe model*, *sediment transport GPU*, *OpenMP shallow water* i *SIMD vectorization finite volume*. Uslov je bio da je rad objavljen od 1. 1. 2020. Bibliografski podaci svakog rada provjereni su na stranici izdavača ili u Crossref/OpenAlex zapisu. Za radove sa otvorenim pristupom (GMD, VMV, dio ACM i IEEE radova) analiziran je kompletan tekst; za ostale je dostupan bio samo sažetak, što je u nastavku naglašeno. Pored radova iz traženog perioda, citirana su i tri temeljna rada na kojima se odabrani algoritam zasniva " + cite("mei2007", "stava2008", "bates2010") + "."));
  items.push(H(2, "Analiza odabranih radova"));

  const paper = (title, body) => {
    items.push(new d.Paragraph({ keepNext: true, spacing: { before: 160, after: 60, line: 300 },
      children: require("./helpers").runs("**" + title + "**") }));
    body.forEach((t) => items.push(P(t)));
  };

  paper("Nilles, Günther, Wagner, Müller (2024): 3D Real-Time Hydraulic Erosion Simulation using Multi-Layered Heightmaps " + cite("nilles2024"), [
    "*Problem i metoda:* proširenje erozije na višeslojne visinske mape, kako bi se mogli prikazati prevjesi, lukovi i pećine. Rad direktno preuzima model virtuelnih cijevi i model erozije i depozicije iz rada Meija i saradnika, ali sediment transportuje kroz cijevi umjesto semi-Lagrangeove advekcije, jer je interpolacija na višeslojnim mapama teško izvodljiva; dodaje i horizontalnu eroziju i provjeru oslonca materijala. *Paralelizacija:* CUDA, 9 kernela, jedna nit po ćeliji; race condition-i se izbjegavaju podjelom na više kernela i šahovskim (eng. *checkerboard*) ažuriranjem. *Rezultati:* na RTX 3070 jedan korak na mreži 1024² traje oko 1,4–1,6 ms. *Ograničenja:* osnovna vizualizacija, ograničen broj slojeva. *Relevantnost:* najbliži savremeni rad našoj implementaciji – i mi transportujemo sediment kroz cijevi, a podjela na faze odgovara njihovoj podjeli na kernele. (Analiziran kompletan tekst.)",
  ]);
  paper("Salamin, Szénási (2024): Simulating Hidraulic Erosion Utilizing Graphics Processors " + cite("salamin2024"), [
    "*Problem i metoda:* GPU implementacija klasičnog postupka iz rada Meija i saradnika u pet faza (kiša, tok kroz cijevi, erozija i depozicija sa C = K_{c} sin α |v|, semi-Lagrangeova advekcija sedimenta, isparavanje). *Paralelizacija:* jedna GPU nit po ćeliji. *Rezultati:* poređenje sa CPU-om (i5-13600K) i GPU-om GTX 1050 Ti; na mreži 2048² CPU verzija traje 198,3 s, a GPU 16,4 s. *Ograničenja:* CPU verzija sa kojom se poredi je, prema našoj procjeni, jednodretvena i nevektorizovana, pa poređenje precjenjuje prednost GPU-a. *Relevantnost:* isti algoritam kao u ovom radu; naša CPU faza popunjava upravo prazninu koju rad ostavlja – višedretvenu i SIMD optimizovanu CPU verziju kao fer referencu. (Analiziran kompletan tekst.)",
  ]);
  paper("Shaw, Kesserwani, Neal, Bates, Sharifian (2021): LISFLOOD-FP 8.0 " + cite("shaw2021"), [
    "*Problem i metoda:* novi DG2 (diskontinuirani Galerkin drugog reda) i FV1 (konačne zapremine prvog reda) solveri u modelu za poplave LISFLOOD-FP, upoređeni sa postojećim lokalno-inercijalnim ACC solverom " + cite("bates2010") + ". *Paralelizacija:* CPU verzija koristi OpenMP paralelizaciju po redovima mreže (sa redukcijom za globalni vremenski korak), a GPU verzija CUDA sa blokovima 16 × 16. *Rezultati:* CPU solveri su najefikasniji ispod oko 0,1 miliona elemenata, a GPU iznad 1 miliona, gdje su 2,5–4 puta brži od 16-jezgrenog CPU-a; skaliranje CPU verzije je blizu idealnog samo do oko 4 jezgre. *Ograničenja:* DG2 je 4–12 puta skuplji po elementu od FV1. *Relevantnost:* ACC šema ažurira protoke na stranicama ćelija iz gradijenta vodene površine, a zatim dubinu po ćeliji – strukturno vrlo slično modelu cijevi; rad je najjasniji objavljeni primjer OpenMP i CUDA implementacije te klase šema. (Analiziran kompletan tekst.)",
  ]);
  paper("Caviedes-Voullième, Morales-Hernández, Norman, Özgen-Xian (2023): SERGHEI-SWE v1.0 " + cite("caviedes2023"), [
    "*Problem i metoda:* 2D SWE solver za hidrologiju; metoda konačnih zapremina prvog reda na pravougaonoj mreži sa augmentiranim Roeovim Riemannovim solverom (dobro balansirana šema, pozitivna dubina, suhe i mokre zone) i eksplicitnim Eulerovim korakom (CFL ≤ 0,5). *Paralelizacija:* Kokkos (OpenMP i CUDA backend iz istog koda) i MPI dekompozicija domene sa halo ćelijama; petlje po ćelijama formiraju podatke o stranicama unutar kernela. *Rezultati:* dobro skaliranje na više od 20 000 CPU jezgri i do 256 GPU-ova; za 552 miliona ćelija oko 89 % efikasnosti pri prelasku sa 128 na 160 A100 GPU-ova. *Ograničenja:* prvi red tačnosti, samo pravougaone mreže, moduli za transport sedimenta u razvoju. *Relevantnost:* uzor za arhitekturu „jedan izvor – CPU i GPU“ i za dekompoziciju sa halo okvirom koju koristimo. (Analiziran kompletan tekst.)",
  ]);
  paper("Morales-Hernández i saradnici (2021): TRITON " + cite("morales2021"), [
    "*Problem i metoda:* otvoreni 2D hidrodinamički model za poplave; eksplicitne konačne zapremine prvog reda sa augmentiranim Roeovim solverom. *Paralelizacija:* prema zvaničnoj dokumentaciji, CPU build koristi OpenMP + MPI, a GPU build CUDA + MPI. *Rezultati:* sažetak ne navodi brojeve; prema izvještaju centra OLCF, simulacija uragana Harvey na jednom čvoru sa 6 GPU-ova bila je brža od 64 CPU čvora. *Ograničenja:* prvi red tačnosti, komunikacija i ulaz/izlaz ograničavaju skalabilnost. *Relevantnost:* primjer jednog koda sa OpenMP i CUDA varijantom za eksplicitnu šemu na mreži. (Dostupan samo sažetak i dokumentacija.)",
  ]);
  paper("Hou, Kang, Hu, Tong, Pan, Xia (2020): GPU model sprege hidrodinamike i morfologije " + cite("hou2020"), [
    "*Problem i metoda:* spregnuta simulacija toka, transporta sedimenta i promjene korita; Godunovljeva šema konačnih zapremina sa HLLC Riemannovim solverom, MUSCL rekonstrukcijom i tačkasto-implicitnim trenjem. *Paralelizacija:* C++ i CUDA. *Rezultati:* 298,1 s na GPU-u naspram 4531,2 s na CPU-u (oko 15 puta). *Ograničenja:* sažetak ih ne navodi; CPU referenca je, prema našoj procjeni, serijska. *Relevantnost:* geonaučni ekvivalent faze erozije u našem modelu. (Dostupan samo sažetak.)",
  ]);
  paper("Cea, García-Feal, Nord, Piton, Legout (2024): erozija tla u softveru Iber " + cite("cea2024"), [
    "*Problem i metoda:* višeklasni model erozije tla kišom i oticanjem, zasnovan na 2D SWE (konačne zapremine na nestrukturiranoj mreži, poluimplicitno trenje), sa vučenim i suspendovanim nanosom po klasi sedimenta. *Paralelizacija:* C++/CUDA, podaci stalno na GPU-u, biblioteka CUB za redukcije. *Rezultati:* oko dva reda veličine brže od sekvencijalne CPU verzije. *Relevantnost:* fizikalno detaljnija verzija istog lanca kiša → tok → erozija koji simuliramo. (Sažetak i dijelovi teksta.)",
  ]);
  paper("Steffen, Amann, Hinkelmann (2022): strategije poboljšanja performansi FVM modela " + cite("steffen2022"), [
    "*Problem i metoda:* eksplicitni MUSCL solver konačnih zapremina u C++ na strukturiranim mrežama. *Paralelizacija:* OpenMP i MPI (MIMD) te SIMD preko biblioteke Eigen; upoređena su tri mjesta uvođenja paralelizma. *Rezultati:* paralelizacija po blokovima mreže bila je najbolja (do oko 1,3 puta bolja od alternativa), a vektorizacija protoka na stranicama dala je dodatno značajno ubrzanje; varijanta koja svaki protok računa dva puta bila je najsporija. *Relevantnost:* najdirektniji rad o kombinaciji SIMD i OpenMP za SWE na strukturiranoj mreži – ista pitanja (blokovi, dupliranje računanja protoka, vektorizacija) javljaju se i kod nas. (Dostupan samo sažetak.)",
  ]);
  paper("Klöwer, Hatfield, Croci, Düben, Palmer (2022): simulacija fluida u 16-bitnoj preciznosti " + cite("klower2022"), [
    "*Problem i metoda:* model plitkih voda ShallowWaters.jl (konačne razlike na Arakawa C-mreži, RK4). *Rezultati:* na procesoru A64FX Float16 je do 3,8 puta brži od Float64, a Float32 blizu 2 puta; autori ubrzanje pripisuju smanjenom memorijskom saobraćaju i većem broju elemenata po SIMD registru, jer je model ograničen memorijom; subnormalni brojevi se postavljaju na nulu. *Relevantnost:* argument za float (8 traka u AVX2 umjesto 4 za double) i za FTZ/DAZ podešavanje koje i mi koristimo. (Analiziran kompletan tekst.)",
  ]);
  paper("Radovi iz računarske grafike sa drugačijim pristupom eroziji " + cite("schott2023", "schott2024", "jain2024debris"), [
    "Schott i saradnici " + cite("schott2023") + " koriste zakon snage toka (eng. *stream power law*) i tektonsko izdizanje za interaktivno oblikovanje velikih terena, a u " + cite("schott2024") + " višeskalnu eroziju za povećanje rezolucije terena pomoću GPU compute shadera. Jain, Benes i Cordonnier " + cite("jain2024debris") + " kombinuju fluvijalnu eroziju sa modelom tokova debrisa na strmim terenima u PyTorch-u na GPU-u (1600 iteracija na mreži 1024² za 32 s). Ovi modeli vodu ne simuliraju eksplicitno, nego je apstrahuju kroz površinu sliva (eng. *drainage area*), što omogućava geološke vremenske skale, ali uvodi globalne zavisnosti (rutiranje toka) koje se teže paralelizuju " + cite("jain2024fastflow") + ". Za naš rad su važni kao kontrast: model cijevi ima isključivo lokalne zavisnosti.",
  ]);
  return items;
}

function literaturaTabela() {
  const t = TABLE("Uporedni pregled analiziranih radova (2020–danas)", [
    ["Rad", "God.", "Problem", "Numerička metoda", "Paralelizacija", "Ključni rezultat", "Veza sa ovim radom"],
    ["Nilles i sar. " + cite("nilles2024"), "2024", "erozija, višeslojni teren", "model cijevi + erozija Mei; sediment kroz cijevi", "CUDA, 9 kernela", "≈1,5 ms/korak (1024², RTX 3070)", "isti model; faze = kerneli"],
    ["Salamin, Szénási " + cite("salamin2024"), "2024", "hidraulička erozija", "model cijevi, semi-Lagrange", "GPU, nit po ćeliji", "2048²: 198,3 s CPU / 16,4 s GPU", "isti algoritam; serijska CPU referenca"],
    ["LISFLOOD-FP 8.0 " + cite("shaw2021"), "2021", "poplave", "DG2, FV1, ACC (lokalno-inercijalna)", "OpenMP po redovima; CUDA", "GPU 2,5–4× > 16 jezgri za >1 M ćelija", "ACC ≈ model cijevi"],
    ["SERGHEI-SWE " + cite("caviedes2023"), "2023", "hidrologija", "FV, augmentirani Roe", "Kokkos (OpenMP/CUDA) + MPI", "≈89 % efikasnosti na A100", "halo, jedan izvor CPU/GPU"],
    ["TRITON " + cite("morales2021"), "2021", "poplave", "FV, augmentirani Roe", "OpenMP+MPI; CUDA+MPI", "6 GPU > 64 CPU čvora (Harvey)", "dvije izvedbe istog modela"],
    ["Hou i sar. " + cite("hou2020"), "2020", "tok + morfologija", "Godunov FV, HLLC, MUSCL", "CUDA", "≈15× u odnosu na CPU", "sprega toka i erozije"],
    ["Cea i sar. " + cite("cea2024"), "2024", "erozija tla", "FV (Roe/DHD), višeklasni sediment", "CUDA, CUB", "≈100× u odnosu na serijski CPU", "lanac kiša→tok→erozija"],
    ["Steffen i sar. " + cite("steffen2022"), "2022", "performanse SWE", "MUSCL FV", "OpenMP, MPI, SIMD (Eigen)", "blokovi najbolji; SIMD dodatno ubrzava", "SIMD + OpenMP na mreži"],
    ["Klöwer i sar. " + cite("klower2022"), "2022", "preciznost i brzina", "FD, Arakawa C, RK4", "SIMD, nisko-precizna aritm.", "Float16 3,8×, Float32 ≈2× vs Float64", "float i FTZ"],
    ["Jain i sar. " + cite("jain2024debris"), "2024", "erozija strmih terena", "stream power + debris tok", "PyTorch, GPU", "1600 iter. 1024² za 32 s", "kontrast: globalne zavisnosti"],
  ], [1.35, 0.5, 1.05, 1.45, 1.2, 1.45, 1.3], { size: 15 });
  return [
    H(2, "Uporedna tabela"),
    P("Tabela 2 sažima analizirane radove. Kolona „Ključni rezultat“ sadrži samo vrijednosti koje su autori eksplicitno naveli."),
    ...t.items,
    H(2, "Kritički osvrt"),
    P("Iz literature proizlaze tri zaključka važna za ovaj rad. Prvo, savremeni hidrološki solveri (LISFLOOD-FP, SERGHEI-SWE, TRITON, BASEMENT " + cite("vanzo2021") + ") svi koriste eksplicitne šeme na mreži sa lokalnim stencilom i održavaju CPU (OpenMP) i GPU (CUDA) izvedbu istog modela – to je potvrda da je odabrani pristup prenosiv. Drugo, mnogi radovi o GPU ubrzanju porede GPU sa jednodretvenom, nevektorizovanom CPU verzijom " + cite("salamin2024", "hou2020") + ", pa prijavljena ubrzanja (15–100 puta) miješaju dobit od arhitekture sa dobiti od optimizacije. Zbog toga je u ovom radu posebna pažnja posvećena tome da CPU referenca bude i višedretvena i vektorizovana, a da se efekti dretvi i SIMD-a mjere odvojeno. Treće, sistematska analiza SIMD vektorizacije SWE solvera na CPU-u je rijetka (izdvaja se " + cite("steffen2022") + "), a radovi koji je pominju ističu da su ovi kodovi ograničeni memorijskom propusnošću " + cite("klower2022", "villalobos2025") + " – što daje hipotezu koju provjeravamo eksperimentalno."),
  ];
}

function algoritmi() {
  const items = [];
  items.push(H(1, "Algoritmi za simulaciju plitkih voda i erozije"));
  items.push(H(2, "Pregled metoda iz literature"));
  items.push(P("**Metoda konačnih zapremina (FVM) Godunovljevog tipa.** Domena se dijeli na ćelije, a promjena srednje vrijednosti U u ćeliji jednaka je zbiru numeričkih protoka kroz stranice. Protoci se računaju približnim Riemannovim solverima: HLL (dva talasa), HLLC (dodaje kontaktni talas) ili Roeov solver (linearizacija). Uz MUSCL rekonstrukciju dobija se drugi red tačnosti " + cite("hou2020") + ". Metoda je konzervativna, robusna za udarne talase i suhe/mokre zone, ali po stranici izvršava desetine operacija, grananja za suhe ćelije i korjenovanja, a za dobro balansiranje potrebne su posebne korekcije izvornog člana " + cite("caviedes2023", "morales2021") + "."));
  items.push(P("**Metoda konačnih razlika (FDM) i lokalno-inercijalna aproksimacija.** Bates i saradnici " + cite("bates2010") + " zanemaruju advekcijski član u jednačini količine kretanja, pa se protok na stranici ažurira eksplicitno iz gradijenta vodene površine i trenja, a dubina iz divergencije protoka (staggered mreža). Šema je jednostavna i brza, ali slabija za brze, plitke tokove na strmim terenima " + cite("sharifian2023") + "."));
  items.push(P("**Model virtuelnih cijevi.** Mei i saradnici " + cite("mei2007") + " posmatraju svaku ćeliju kao rezervoar povezan sa četiri susjeda cijevima; protok kroz cijev ubrzava razlika hidrostatičkog pritiska (razlika ukupnih visina b + d). Izlazni protoci se ograničavaju tako da ćelija ne izgubi više vode nego što ima, što garantuje nenegativnu dubinu. Model je u suštini lokalno-inercijalna šema bez trenja i sa konstantnim presjekom cijevi, proširena modelom erozije zasnovanim na kapacitetu."));
  items.push(P("**Čestične metode i zakon snage toka.** SPH i modeli zasnovani na površini sliva " + cite("schott2023", "jain2024debris") + " imaju nepravilne ili globalne zavisnosti (pretraga susjeda, rutiranje toka), pa nisu pogodni kao prvi korak za ovaj rad."));

  const t = TABLE("Uporedna analiza kandidatskih metoda (N – broj ćelija)", [
    ["Kriterij", "FVM Godunov (HLL/HLLC/Roe)", "Lokalno-inercijalna (ACC)", "Model virtuelnih cijevi"],
    ["Matematička osnova", "kompletne SWE, Riemannov problem na stranici", "SWE bez advekcije, staggered protoci", "hidrostatički pritisak između rezervoara"],
    ["Struktura podataka", "U = (h, hu, hv) po ćeliji, protoci po stranici", "h po ćeliji, q_{x}, q_{y} po stranici", "b, d, s i 4 izlazna protoka po ćeliji (SoA)"],
    ["Složenost po koraku", "O(N), ≈100–300 operacija/ćelija", "O(N), ≈30–50 op./ćeliji", "O(N), ≈60 op./ćeliji (sa erozijom)"],
    ["Memorija", "O(N), 3 + pomoćna polja", "O(N)", "O(N), 9 nizova"],
    ["Stabilnost", "CFL ≤ 0,5–1 (po brzini talasa)", "CFL, uslov po √(gh)", "CFL po √(gA/l) + ograničenje izlaza K"],
    ["Rubovi", "halo / fantomske ćelije", "halo", "maske otvorenosti, halo za nagib"],
    ["Zavisnosti", "u fazi nema; između faza barijera", "dvije faze po koraku", "tri faze po koraku"],
    ["SIMD", "otežano (grananja, suhe ćelije)", "dobro", "vrlo dobro (bez grananja uz maske)"],
    ["OpenMP", "dobro", "dobro", "vrlo dobro (statički po redovima)"],
    ["GPU", "dobro, veći pritisak na registre", "vrlo dobro", "vrlo dobro (nit po ćeliji)"],
    ["Erozija", "Exner + empirijske formule", "nije dio šeme", "kapacitet C = K_{c} sin α |v|"],
  ], [1.3, 2.0, 1.75, 2.0], { size: 16 });
  items.push(...t.items);

  // ---------------------------------------------------------------- model cijevi
  items.push(H(2, "Model virtuelnih cijevi: matematički opis i diskretizacija"));
  items.push(P("Domena je pravougaona mreža W × H kvadratnih ćelija stranice l_{x} = l_{y} = l. Ćelija (x, y) čuva visinu terena b, dubinu vode d, suspendovani sediment s i četiri izlazna protoka f^{L}, f^{R}, f^{T}, f^{B} [m³/s] prema lijevom, desnom, gornjem i donjem susjedu. Jedan vremenski korak Δt sastoji se od sljedećih koraka " + cite("mei2007") + "."));
  items.push(P("**Kiša.** Dubina se povećava za intenzitet kiše r:", { keepNext: true }));
  const e5 = EQ([M.sub("d", "1"), "=", M.sub("d", "t"), "+", "Δt", "·", "r"]); items.push(e5.para);
  items.push(P("**Izlazni protoci.** Za smjer L (analogno R, T, B), sa razlikom ukupnih visina Δh^{L} = (b + d_{1}) − (b^{L} + d_{1}^{L}), presjekom cijevi A i dužinom l:", { keepNext: true }));
  const e6 = EQ([M.subsup("f", "t+Δt", "L"), "=", "max", M.par(["0", ",", " ", M.subsup("f", "t", "L"), "+", "Δt", "·", "A", "·", M.frac(["g", "·", "Δ", M.sup("h", "L")], "l")])]); items.push(e6.para);
  items.push(P("Na zatvorenom rubu domene protok kroz rub je nula. Da ćelija ne bi izgubila više vode nego što ima, svi izlazni protoci se množe faktorom:", { keepNext: true }));
  const e7 = EQ(["K", "=", "min", M.par(["1", ",", " ", M.frac([M.sub("d", "1"), M.sub("l", "x"), M.sub("l", "y")], [M.par([M.sup("f", "L"), "+", M.sup("f", "R"), "+", M.sup("f", "T"), "+", M.sup("f", "B")]), "Δt"])])]); items.push(e7.para);
  items.push(P("**Ažuriranje dubine.** Promjena zapremine je razlika ulaznih protoka (izlaznih protoka susjeda prema ćeliji) i izlaznih protoka:", { keepNext: true }));
  const e8 = EQ(["Δ", "V", "=", "Δt", M.par([M.sum(M.sub("f", "in"), "", ""), "−", M.sum(M.sub("f", "out"), "", "")]), ",", "   ", M.sub("d", "2"), "=", M.sub("d", "1"), "+", M.frac("ΔV", [M.sub("l", "x"), M.sub("l", "y")])]); items.push(e8.para);
  items.push(P("**Brzina.** Iz protoka kroz ćeliju u pravcu x i srednje dubine d̄ = (d_{1} + d_{2})/2:", { keepNext: true }));
  const e9 = EQ(["Δ", M.sub("W", "x"), "=", M.frac("1", "2"), M.par([M.sup("f", "R"), "(x−1,y)", "−", M.sup("f", "L"), "(x,y)", "+", M.sup("f", "R"), "(x,y)", "−", M.sup("f", "L"), "(x+1,y)"]), ",", "   ", "u", "=", M.frac(["Δ", M.sub("W", "x")], [M.sub("l", "y"), " ", "d̄"])]); items.push(e9.para);
  items.push(P("i analogno za v u pravcu y. Uslov d̄ > d_{min} (u implementaciji 1 mm) sprečava dijeljenje sa veoma malom dubinom."));

  items.push(H(2, "Erozija, depozicija i transport sedimenta"));
  items.push(P("Nagib terena se računa centralnim razlikama, ∇b ≈ ((b^{R} − b^{L})/2l, (b^{B} − b^{T})/2l), a sinus ugla nagiba kao sin α = |∇b| / √(1 + |∇b|^{2}). Kapacitet transporta je:", { keepNext: true }));
  const e10 = EQ(["C", "=", M.sub("K", "c"), "·", "max", M.par(["sin", " ", "α", ",", " ", M.sub("s", "min")]), "·", "|", "v", "|", "·", "min", M.par(["1", ",", " ", M.frac("d̄", M.sub("d", "ero"))])]); items.push(e10.para);
  items.push(P("Ako je C > s, voda otkida materijal, a ako je C ≤ s, višak sedimenta se taloži:", { keepNext: true }));
  const e11 = EQ(["a", "=", "Δt", "·", "κ", "·", M.par(["C", "−", "s"]), ",", "   ", M.sup("b", "t+Δt"), "=", "b", "−", "a", ",", "   ", M.sub("s", "1"), "=", "s", "+", "a"]); items.push(e11.para);
  items.push(P("gdje je κ = K_{s} za C > s (erozija, a > 0) i κ = K_{d} za C ≤ s (depozicija, a ≤ 0)."));
  items.push(P("Zbir b + s se u ovom koraku tačno čuva, jer se ista količina a oduzima od terena i dodaje sedimentu. **Transport sedimenta** je izveden konzervativno, kroz iste cijevi kroz koje teče voda: ćelija šalje susjedu onaj udio sedimenta koji odgovara udjelu vode koja je kroz tu cijev napustila ćeliju:", { keepNext: true }));
  const e12 = EQ([M.sub("s", "2"), "=", M.sub("s", "1"), "−", M.sub("σ", "c"), M.sum(M.sub("f", "out"), "", ""), "+", M.sum([M.sub("σ", "n"), M.sub("f", "n→c")], "n", ""), ",", "   ", M.sub("σ", "c"), "=", M.frac([M.sub("s", "1"), "Δt"], [M.sub("d", "1"), M.sub("l", "x"), M.sub("l", "y")])]); items.push(e12.para);
  items.push(P("Pošto faktor K iz (7) garantuje Δt Σf_{out} ≤ d_{1} l_{x} l_{y}, ćelija nikad ne pošalje više sedimenta nego što ima, a isti proizvod σ_{n} f_{n→c} se oduzima kod pošiljaoca i dodaje kod primaoca, pa je ukupna količina sedimenta očuvana. **Isparavanje** zatvara korak:", { keepNext: true }));
  const e13 = EQ([M.sub("d", "t+Δt"), "=", M.sub("d", "2"), M.par(["1", "−", M.sub("K", "e"), "Δt"])]); items.push(e13.para);

  items.push(H(2, "Numerička stabilnost i CFL uslov"));
  items.push(P("Linearizacijom (6) i (8) za male oscilacije oko ravnoteže dobija se talasna jednačina za protok po jedinici širine q = f/l: ∂q/∂t = −(A/l) g ∂η/∂x i ∂d/∂t = −∂q/∂x. Model se ponaša kao plitka voda efektivne dubine A/l, sa brzinom talasa c = √(gA/l). Šema „naprijed-nazad“ (protok iz starih visina, visina iz novih protoka) na 5-tačkastom stencilu u 2D stabilna je za:", { keepNext: true }));
  const e14 = EQ(["Δt", "≤", M.sub("C", "CFL"), M.frac("l", [M.sqrt("2"), "·", M.sqrt(M.frac(["g", "A"], "l"))]), ",", "   ", M.sub("C", "CFL"), "≤", "1"]); items.push(e14.para);
  items.push(P("Pošto je brzina talasa u ovom modelu konstantna, granica se računa jednom: za l = 1 m, A = 1 m² i C_{CFL} = 0,9 dobija se Δt_{max} = 0,203 s, a koristi se Δt = 0,05 s. Funkcija `make_step_constants` automatski skraćuje traženi korak na ovu granicu (test u poglavlju 8.3 traži Δt = 10 s i dobija 0,203 s). Dodatno se provjerava da su relaksacije erozije, depozicije i isparavanja stabilne (Δt K_{s}, Δt K_{d}, Δt K_{e} ≤ 1). Nenegativnost dubine obezbjeđuje faktor K, a sediment se nakon transporta ograničava odozdo nulom zbog greške zaokruživanja."));

  items.push(H(2, "Odabir algoritma"));
  items.push(P("Odabran je model virtuelnih cijevi sa erozijom zasnovanom na kapacitetu, jer zadovoljava sve kriterije: (1) direktno je povezan sa provjerenim radovima " + cite("nilles2024", "salamin2024") + " i strukturno sa lokalno-inercijalnim solverom LISFLOOD-FP " + cite("shaw2021", "bates2010") + "; (2–3) jednostavno se implementira u C++ kao serijska referenca; (4) svaka faza je petlja bez grananja nakon pretvaranja uslova u maske, što je idealno za SIMD; (5) redovi se mogu statički podijeliti dretvama; (6) složenost odgovara seminarskom radu; (7) jedna ćelija = jedna GPU nit, kao u " + cite("nilles2024") + "; (8) obuhvata i tok vode i eroziju/depoziciju. Godunovljeve šeme sa HLLC solverom bile bi fizikalno tačnije, ali bi zbog grananja i znatno više operacija po stranici zamaglile cilj rada – analizu paralelizacije."));
  const t2 = TABLE("Odnos implementacije i izvornog modela Meija i saradnika " + cite("mei2007"), [
    ["Komponenta", "Status", "Opis"],
    ["Kiša, protoci kroz 4 cijevi, faktor K, ažuriranje dubine, brzina", "zadržano", "jednačine (5)–(9), bez izmjena"],
    ["Kapacitet C = K_{c} sin α |v|, erozija/depozicija", "zadržano uz izmjene", "donja granica sin α ≥ s_{min} (erozija i na ravnom), faktor dubine min(1, d̄/d_{ero}) za vrlo plitku vodu; stope pomnožene sa Δt"],
    ["Transport sedimenta", "izmijenjeno", "konzervativni upwind kroz cijevi (12) umjesto semi-Lagrangeove advekcije: tačno očuvanje mase, isti stencil, bez interpolacije (isto kao u " + cite("nilles2024") + ")"],
    ["Isparavanje", "zadržano", "d ← d(1 − K_{e}Δt)"],
    ["Rubni uslovi", "precizirano", "zatvoreni zidovi (maske), Neumannov halo za nagib terena"],
    ["Kiša", "pojednostavljeno", "uniformna po domeni (u izvornom radu i slučajne kapi / izvori)"],
    ["Termička erozija, slojevi materijala, vizualizacija na GPU-u", "izostavljeno", "nije u fokusu ove faze"],
    ["CFL kontrola koraka", "dodano", "izvedena granica (14) i automatsko ograničenje Δt"],
  ], [2.2, 1.1, 3.7], { size: 17 });
  items.push(...t2.items);
  items.push(P("Implementacija, dakle, nije potpuna reprodukcija izvornog rada, nego jasno definisana varijanta modela cijevi čije su izmjene motivisane očuvanjem mase i pogodnošću za paralelizaciju."));
  return items;
}

module.exports = { sazetak, uvod, problem, literatura, literaturaTabela, algoritmi };
