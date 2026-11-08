#pragma once
// Minimalni okvir za testove (bez vanjskih biblioteka): registracija testova makroom PRS_TEST, provjere
// CHECK / CHECK_NEAR / CHECK_LE koje bilježe grešku i nastavljaju, i ispis "[ OK ]" / "[FAIL]".
// Program vraća 0 samo ako su sve provjere prošle (CTest to koristi).

#include "Grid2D.hpp"
#include "Heightmap.hpp"

#include <cmath>
#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace prs::test
{
    struct TestCase
    {
        std::string group;
        std::string name;
        std::function<void()> body;
    };

    std::vector<TestCase>& registry();

    struct Registrar
    {
        Registrar(const char* group, const char* name, void (*body)());
    };

    void check(bool condition, const std::string& expression, const char* file, int line);
    void checkNear(double actual, double expected, double tolerance, const std::string& expression, const char* file,
                   int line);
    void checkLessEqual(double actual, double limit, const std::string& expression, const char* file, int line);

    // Informativni ispis (npr. tabele za testove 5 i 6) – uvlači se ispod naziva testa.
    void note(const std::string& message);

    // ---------------------------------------------------------------- generatori sintetičkih terena
    // Deterministički (vlastiti LCG), da rezultati budu isti na svakom računaru/kompajleru.
    Heightmap flatTerrain(int width, int height, double elevation, double cellSize = 10.0);
    // Ravan nagnut u smjeru +x: visina pada od lijevog ka desnom rubu za "drop" metara.
    Heightmap slopeTerrain(int width, int height, double drop, double cellSize = 10.0, double noise = 0.0,
                           std::uint32_t seed = 1);
    // Nasumični hrapavi teren (zbir sinusa + šum) – za bilanse i stabilnost.
    Heightmap roughTerrain(int width, int height, double relief, double cellSize = 10.0, std::uint32_t seed = 7);
    // Planina: Gaussovo brdo s grebenima i dolinama.
    Heightmap mountainTerrain(int width, int height, double peak, double cellSize = 10.0, std::uint32_t seed = 3);

    std::string fmt(double value, int precision = 4);
}

#define PRS_TEST_CONCAT2(a, b) a##b
#define PRS_TEST_CONCAT(a, b) PRS_TEST_CONCAT2(a, b)
#define PRS_TEST(group, name)                                                                                \
    static void name();                                                                                      \
    static const ::prs::test::Registrar PRS_TEST_CONCAT(registrar_, name)(group, #name, &name);              \
    static void name()

#define CHECK(cond) ::prs::test::check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define CHECK_NEAR(actual, expected, tol) \
    ::prs::test::checkNear((actual), (expected), (tol), #actual " ~ " #expected, __FILE__, __LINE__)
#define CHECK_LE(actual, limit) ::prs::test::checkLessEqual((actual), (limit), #actual " <= " #limit, __FILE__, __LINE__)
