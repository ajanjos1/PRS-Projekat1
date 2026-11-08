#pragma once
// Stanje simulacije: sva polja po ćelijama, svako u svom kontinuiranom nizu (Structure of Arrays).
//
// Oznake prate Mei i dr. (2007):
//   b  – visina terena                         [m]
//   d  – dubina vode                           [m]
//   s  – suspendovani sediment, izražen kao debljina sloja materijala terena koji je u vodi [m]
//        (zapremina materijala po jedinici površine ćelije, m³/m²). Zato je b + s fizički konzistentan
//        zbir: oba su visina stuba ISTOG materijala, pa je ukupni materijal  Σ (b + s) · l²  [m³].
//   fL, fR, fT, fB – izlazni zapreminski protok kroz lijevu/desnu/gornju/donju virtuelnu cijev [m³/s]
//   u, v – komponente brzine vode u smjeru +x (desno) i +y (dolje) [m/s]
//
// Dvostruko baferovanje (double buffering): polja koja se u istoj fazi čitaju kod SUSJEDA, a pišu za
// trenutnu ćeliju (teren u fazi erozije, sediment u fazi transporta), imaju drugi bafer (*Next). Faza čita
// samo stari bafer, piše samo novi, pa nijedna ćelija ne vidi djelimično ažurirane vrijednosti susjeda.
// Polja koja ćelija piše samo za sebe, a susjedi ih u toj fazi ne čitaju (protoci u fazi protoka, d u fazi
// vode), ažuriraju se na mjestu – to je sigurno i u budućoj paralelnoj verziji.

#include "Grid2D.hpp"

namespace prs
{
    struct SimulationState
    {
        int width = 0;
        int height = 0;
        double cellSize = 1.0; // l = lx = ly [m]

        Grid2D<double> terrain;      // b
        Grid2D<double> terrainNext;  // drugi bafer za b
        Grid2D<double> bedrock;      // najniža dozvoljena visina terena (b0 - debljina erodibilnog sloja)
        Grid2D<double> water;        // d
        Grid2D<double> waterBeforeFlow; // d1: dubina nakon kiše, prije razmjene vode (za transport sedimenta)
        Grid2D<double> sediment;     // s
        Grid2D<double> sedimentNext; // drugi bafer za s
        Grid2D<double> sedimentPerOutflow; // pomoćno: s * dt / (d1 * l²) – udio sedimenta po jedinici protoka
        Grid2D<double> fluxLeft, fluxRight, fluxTop, fluxBottom; // f^L, f^R, f^T, f^B
        Grid2D<double> velocityX, velocityY;                     // u, v
        Grid2D<double> maxWaterDepth;  // dijagnostika: najveća dubina vode u toku simulacije [m]

        void allocate(int w, int h, double cell)
        {
            width = w;
            height = h;
            cellSize = cell;
            for (Grid2D<double>* g : {&terrain, &terrainNext, &bedrock, &water, &waterBeforeFlow, &sediment,
                                      &sedimentNext, &sedimentPerOutflow, &fluxLeft, &fluxRight, &fluxTop,
                                      &fluxBottom, &velocityX, &velocityY, &maxWaterDepth})
                g->resize(w, h, 0.0);
        }

        std::size_t cellCount() const { return terrain.size(); }
        double cellArea() const { return cellSize * cellSize; }
    };
}
