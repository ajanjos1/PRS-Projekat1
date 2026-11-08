# third_party/stb

Biblioteke za čitanje i pisanje slika iz projekta **stb** (Sean Barrett i saradnici), preuzete bez izmjena:

| Fajl | Verzija | Upotreba u projektu |
|---|---|---|
| `stb_image.h` | v2.30 | učitavanje PNG (8 i 16 bita) i JPEG height mapa (`src/stb_image_impl.cpp`) |
| `stb_image_write.h` | v1.16 | zlib kompresija za vlastiti 8/16-bitni PNG zapis i JPEG zapis za testove (`src/ImageWriter.cpp`) |

- Izvor: https://github.com/nothings/stb (commit `4e9b368bfc071e97206ea5c6adba68dd0cbf8897`).
- Licenca: dvojna – **MIT ili Public Domain (Unlicense)**, po izboru korisnika; tekst je u `LICENSE` i na kraju
  svakog zaglavlja. Obje opcije dozvoljavaju upotrebu, izmjenu i distribuciju u ovom projektu.
- Implementacija se prevodi u tačno jednom `.cpp` fajlu po biblioteci (`STB_IMAGE_IMPLEMENTATION`,
  `STB_IMAGE_WRITE_IMPLEMENTATION`); ostatak projekta ne uključuje ova zaglavlja.
