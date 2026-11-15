// IJK vs IKJ: kako REDOSLIJED PETLJI mijenja brzinu množenja matrica, iako je posao isti.
//
// Množenje matrica je najkraći način da se vidi uticaj keša: svi redoslijedi rade tačno isti broj
// množenja i sabiranja (N³), a razlikuju se samo po tome kojim redom čitaju memoriju.
//
//   C[i][j] += A[i][k] * B[k][j]        matrice su po redovima (row-major), indeks = red * N + kolona
//
// U unutrašnjoj petlji je bitno da li se susjedne iteracije čitaju iz susjednih adresa:
//   petlja po k  -> A[i][k] susjedno, ali B[k][j] skače za N*8 B  (promašaj keša u svakoj iteraciji)
//   petlja po j  -> B[k][j] i C[i][j] susjedni                    (keš linija se iskoristi do kraja,
//                                                                  a kompajler može i vektorizovati)
//   petlja po i  -> A[i][k] i C[i][j] oba skaču za N*8 B          (najgore)
//
// Prevođenje i pokretanje (iz korijena projekta):
//   make -C bench ijk
//   ./build-prof/ijk_vs_ikj [N] [ponavljanja]      zadano: 512 3
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

using Sat = std::chrono::steady_clock;
using Mat = std::vector<double>;

namespace
{
    // Svih šest redoslijeda rade isti posao: C += A * B.
    void ijk(const Mat& A, const Mat& B, Mat& C, int N)
    {
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j)
            {
                double s = 0.0;
                for (int k = 0; k < N; ++k)
                    s += A[i * N + k] * B[k * N + j];
                C[i * N + j] += s;
            }
    }
    void jik(const Mat& A, const Mat& B, Mat& C, int N)
    {
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i)
            {
                double s = 0.0;
                for (int k = 0; k < N; ++k)
                    s += A[i * N + k] * B[k * N + j];
                C[i * N + j] += s;
            }
    }
    void ikj(const Mat& A, const Mat& B, Mat& C, int N)
    {
        for (int i = 0; i < N; ++i)
            for (int k = 0; k < N; ++k)
            {
                const double a = A[i * N + k];
                for (int j = 0; j < N; ++j)
                    C[i * N + j] += a * B[k * N + j];
            }
    }
    void kij(const Mat& A, const Mat& B, Mat& C, int N)
    {
        for (int k = 0; k < N; ++k)
            for (int i = 0; i < N; ++i)
            {
                const double a = A[i * N + k];
                for (int j = 0; j < N; ++j)
                    C[i * N + j] += a * B[k * N + j];
            }
    }
    void jki(const Mat& A, const Mat& B, Mat& C, int N)
    {
        for (int j = 0; j < N; ++j)
            for (int k = 0; k < N; ++k)
            {
                const double b = B[k * N + j];
                for (int i = 0; i < N; ++i)
                    C[i * N + j] += A[i * N + k] * b;
            }
    }
    void kji(const Mat& A, const Mat& B, Mat& C, int N)
    {
        for (int k = 0; k < N; ++k)
            for (int j = 0; j < N; ++j)
            {
                const double b = B[k * N + j];
                for (int i = 0; i < N; ++i)
                    C[i * N + j] += A[i * N + k] * b;
            }
    }

    // Najbolji redoslijed (ikj) paralelizovan po redovima matrice C – svaka nit ima svoje redove.
    void ikj_omp(const Mat& A, const Mat& B, Mat& C, int N)
    {
#pragma omp parallel for schedule(static)
        for (int i = 0; i < N; ++i)
            for (int k = 0; k < N; ++k)
            {
                const double a = A[i * N + k];
                for (int j = 0; j < N; ++j)
                    C[i * N + j] += a * B[k * N + j];
            }
    }

    struct Varijanta
    {
        const char* ime;
        const char* unutrasnja;
        const char* pristup;
        void (*f)(const Mat&, const Mat&, Mat&, int);
    };
}

int main(int argc, char** argv)
{
    const int N = argc > 1 ? std::atoi(argv[1]) : 512;
    const int ponavljanja = argc > 2 ? std::atoi(argv[2]) : 3;
    if (N < 2 || ponavljanja < 1)
    {
        std::fprintf(stderr, "Upotreba: ijk_vs_ikj [N >= 2] [ponavljanja >= 1]\n");
        return 1;
    }

    Mat A(static_cast<std::size_t>(N) * N), B(A.size()), C(A.size());
    for (std::size_t i = 0; i < A.size(); ++i)
    {
        A[i] = std::sin(0.001 * static_cast<double>(i));
        B[i] = std::cos(0.002 * static_cast<double>(i));
    }

    const Varijanta varijante[] = {
        {"ijk", "k", "A susjedno, B skace", ijk},
        {"jik", "k", "A susjedno, B skace", jik},
        {"ikj", "j", "B i C susjedni", ikj},
        {"kij", "j", "B i C susjedni", kij},
        {"jki", "i", "A i C oba skacu", jki},
        {"kji", "i", "A i C oba skacu", kji},
    };

    const double operacija = 2.0 * static_cast<double>(N) * N * N; // N³ množenja + N³ sabiranja
    std::printf("Mnozenje matrica %d x %d (double), %d ponavljanja, 1 nit\n", N, N, ponavljanja);
    std::printf("Radni skup: 3 x %.1f MB\n\n", static_cast<double>(N) * N * 8 / (1024 * 1024));
    std::printf("%-6s %-10s %-22s %10s %10s %9s\n", "red.", "unutr. p.", "pristup memoriji",
                "vrijeme", "GFLOP/s", "vs ikj");
    std::printf("%-6s %-10s %-22s %10s %10s %9s\n", "-----", "---------", "----------------------",
                "----------", "----------", "---------");

    double najbolje[6] = {}, provjera[6] = {};
    for (int v = 0; v < 6; ++v)
    {
        double t = 1e9;
        for (int r = 0; r < ponavljanja; ++r)
        {
            std::fill(C.begin(), C.end(), 0.0);
            const auto t0 = Sat::now();
            varijante[v].f(A, B, C, N);
            const double dt = std::chrono::duration<double>(Sat::now() - t0).count();
            t = std::min(t, dt);
        }
        najbolje[v] = t;
        provjera[v] = C[0] + C[A.size() / 2] + C[A.size() - 1]; // kontrola da svi daju isto
    }

    const double tIkj = najbolje[2];
    for (int v = 0; v < 6; ++v)
        std::printf("%-6s %-10s %-22s %9.3f s %10.2f %8.2fx\n", varijante[v].ime, varijante[v].unutrasnja,
                    varijante[v].pristup, najbolje[v], operacija / najbolje[v] / 1e9, najbolje[v] / tIkj);

    double najveceOdstupanje = 0.0;
    for (int v = 1; v < 6; ++v)
        najveceOdstupanje = std::max(najveceOdstupanje, std::fabs(provjera[v] - provjera[0]));
    std::printf("\nProvjera: svi redoslijedi daju isti rezultat (najvece odstupanje %.3e)\n", najveceOdstupanje);

#ifdef _OPENMP
    double tPar = 1e9;
    for (int r = 0; r < ponavljanja; ++r)
    {
        std::fill(C.begin(), C.end(), 0.0);
        const auto t0 = Sat::now();
        ikj_omp(A, B, C, N);
        tPar = std::min(tPar, std::chrono::duration<double>(Sat::now() - t0).count());
    }
    std::printf("ikj na %d niti: %.3f s (%.2f GFLOP/s), ubrzanje %.2fx u odnosu na ikj na 1 niti\n",
                omp_get_max_threads(), tPar, operacija / tPar / 1e9, tIkj / tPar);
    std::printf("Ukupno ijk -> ikj + niti: %.2fx\n", najbolje[0] / tPar);
#endif
    return 0;
}
