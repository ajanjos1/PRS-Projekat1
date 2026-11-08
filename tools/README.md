# tools/

## terrain_picker.html – birač terena na karti

Samostalna HTML stranica (bez servera i instalacije): odaberi dio Zemlje na karti, a ona napravi
16-bitnu height mapu i `.terrain.json` sa stvarnom razmjerom (veličina ćelije, najniža i najviša visina u metrima).
`prs_erosion` ih zajedno učita.

Otvara se iz programa (meni **[1] Odaberi teren na karti**): program je servira sa svog lokalnog servera
(`http://127.0.0.1:<port>/?token=...`, vidi `include/LocalTerrainServer.hpp`) i stranica nakon „Generiši teren“ sama
šalje PNG i JSON programu. Otvorena dvoklikom (kao fajl) radi samostalno – tada se fajlovi preuzimaju ručno. Potreban je internet i noviji
Chrome, Edge ili Firefox (koristi `CompressionStream` i `createImageBitmap`).

- Visine: AWS Terrain Tiles (Mapzen), Terrarium, https://registry.opendata.aws/terrain-tiles/
  (SRTM data courtesy of the U.S. Geological Survey; Produced using Copernicus data and information funded by the
  European Union - EU-DEM layers).
- Karta: OpenTopoMap (CC-BY-SA, © OpenStreetMap contributors) i Esri (topografska/satelitska); biblioteka Leaflet 1.9.4 (BSD-2) sa cdnjs-a.
  Standardne OSM pločice se ne koriste: tile.openstreetmap.org odbija zahtjeve bez Referera, a stranica otvorena
  kao lokalni fajl ga ne šalje.
- Jezgro (preuzimanje, uzorkovanje, kodiranje PNG-a) je dostupno kao `window.TerrainPicker` i testirano je u
  headless pregledniku s lažnim serverom pločica (poznata analitička funkcija visine: najveća greška 2 mm).
- Format metapodataka: vidi `include/TerrainMetadata.hpp`; primjer: `tests/data/terrain_picker_sample.terrain.json`.
