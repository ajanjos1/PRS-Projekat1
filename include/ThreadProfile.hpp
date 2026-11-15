#pragma once
// Mjerenje rada po NITIMA: koliko je svaka nit računala, a koliko čekala na barijeri.
//
// Ideja: u paralelnoj petlji svaka nit zabilježi kada je POČELA i kada je ZAVRŠILA svoj dio posla.
// Nit koja završi prva mora čekati na barijeri dok i posljednja ne završi:
//     zeleno (rad)   = kraj − početak
//     crveno (čekanje) = najkasniji kraj svih niti − vlastiti kraj
// Iz toga se vidi balans opterećenja: ako su crvene trake velike, posao je loše podijeljen.
//
// Uključuje se samo za mjerenje:  -DPRS_PROFIL_NITI=1 (normalan build ovo uopšte ne prevodi).
#include <array>
#include <atomic>

namespace prs::profil
{
    constexpr int kMaxNiti = 64;

    struct Interval
    {
        double pocetak = 0.0;
        double kraj = 0.0;
    };

    inline std::array<Interval, kMaxNiti> zadnjaFaza{};

    // Atomičan, jer ga pozivaju sve niti istovremeno. Obično `int` bi ovdje bio utrka: dvije niti mogu
    // pročitati istu staru vrijednost, pa bi se veća izgubila i neka nit bi nestala iz tabele.
    inline std::atomic<int> brojNiti{0};

    inline void zabiljezi(int nit, double pocetak, double kraj)
    {
        if (nit < 0 || nit >= kMaxNiti)
            return;
        zadnjaFaza[static_cast<std::size_t>(nit)] = {pocetak, kraj}; // svaka nit piše SVOJE polje
        int stari = brojNiti.load(std::memory_order_relaxed);
        while (nit + 1 > stari && !brojNiti.compare_exchange_weak(stari, nit + 1, std::memory_order_relaxed))
        {
        }
    }

    inline void ocisti()
    {
        zadnjaFaza = {};
        brojNiti.store(0, std::memory_order_relaxed);
    }
}
