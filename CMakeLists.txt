cmake_minimum_required(VERSION 3.16)
project(PRS_ShallowWater_Erosion VERSION 1.0 LANGUAGES CXX)

# Organizacija preuzeta iz CMakeLists.txt sa vježbi (lab1_perf_simd), prilagođena simulaciji.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PRS_ENABLE_OPENMP "OpenMP: višedretvenost (parallel for) i omp simd" ON)
option(PRS_ENABLE_AVX2 "AVX2: intrinsics varijanta i -mavx2 za vektorske varijante (uz runtime provjeru)" ON)
option(PRS_VECTORIZATION_REPORTS "Izvještaji kompajlera o vektorizaciji" OFF)
option(PRS_BUILD_TESTS "Testovi numeričke ispravnosti" ON)

set(PRS_SCALAR_SOURCES src/kernels_scalar.cpp)
set(PRS_VECTOR_SOURCES src/kernels_auto.cpp src/kernels_omp_simd.cpp)
# A2 (ablacija): isti izvor i iste zastavice kao B, ali sa isključenom vektorizacijom
set(PRS_BRANCHLESS_SCALAR_SOURCES src/kernels_scalar_branchless.cpp)
set(PRS_AVX2_SOURCES src/kernels_avx2.cpp)

add_library(prs_core STATIC
  src/cpu_features.cpp
  src/simulation.cpp
  src/terrain.cpp
  src/variants.cpp
  src/driver_serial.cpp
  src/driver_openmp.cpp
  src/benchmark.cpp
  src/io_npy.cpp
  ${PRS_SCALAR_SOURCES}
  ${PRS_BRANCHLESS_SCALAR_SOURCES}
  ${PRS_VECTOR_SOURCES}
  ${PRS_AVX2_SOURCES}
)
target_include_directories(prs_core PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/generated)

# ---------------------------------------------------------------- zajedničke zastavice
# -ffp-contract=off: bez spajanja a*b+c u FMA -> sve varijante izvršavaju identične operacije
#                    (bit-identični rezultati, skalar ostaje stroga referenca).
# -fno-math-errno:   sqrt ne postavlja errno -> petlje sa sqrt se mogu vektorizovati (ne mijenja rezultate).
# NE koristi se -ffast-math (mijenja asocijativnost i semantiku IEEE 754).
if(MSVC)
  # /utf-8: izvorni fajlovi su UTF-8 (dijakritici u porukama), inače MSVC koristi code page 1252
  target_compile_options(prs_core PUBLIC /W4 /EHsc /fp:precise /utf-8)
  set(PRS_COMMON_FLAGS "/fp:precise")
else()
  target_compile_options(prs_core PUBLIC -Wall -Wextra -Wpedantic -ffp-contract=off -fno-math-errno)
  set(PRS_COMMON_FLAGS "-ffp-contract=off -fno-math-errno")
endif()

# ---------------------------------------------------------------- A/E: bez vektorizacije
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(PRS_SCALAR_FLAGS -fno-tree-vectorize -fno-tree-loop-vectorize -fno-tree-slp-vectorize)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(PRS_SCALAR_FLAGS -fno-vectorize -fno-slp-vectorize)
else()
  set(PRS_SCALAR_FLAGS "") # MSVC: #pragma loop(no_vector) u kodu (SW_NO_VECTORIZE)
endif()
if(PRS_SCALAR_FLAGS)
  set_property(SOURCE ${PRS_SCALAR_SOURCES} ${PRS_BRANCHLESS_SCALAR_SOURCES} APPEND PROPERTY COMPILE_OPTIONS ${PRS_SCALAR_FLAGS})
endif()

# ---------------------------------------------------------------- B/C (i F, H): auto i omp simd
# GCC (podrazumijevano -ftrapping-math) ne može if-konvertovati petlje u kojima se rezultat
# min/max/ternarnog izraza dalje množi/dijeli ("control flow in loop"), čak ni uz "omp simd".
# -fno-trapping-math dozvoljava spekulativno izvršavanje FP operacija; izračunate vrijednosti se NE
# mijenjaju (nema reasocijacije ni FTZ), što potvrđuju bit-identični testovi. Clang ovo ima po defaultu.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(PRS_VECTOR_EXTRA_FLAGS -fno-trapping-math)
  set_property(SOURCE ${PRS_VECTOR_SOURCES} ${PRS_BRANCHLESS_SCALAR_SOURCES} APPEND PROPERTY COMPILE_OPTIONS ${PRS_VECTOR_EXTRA_FLAGS})
endif()

# ---------------------------------------------------------------- OpenMP
set(PRS_OPENMP_DESC "off")
if(PRS_ENABLE_OPENMP)
  if(MSVC)
    target_compile_options(prs_core PUBLIC /openmp:experimental)
    target_compile_definitions(prs_core PUBLIC PRS_HAS_OPENMP=1 PRS_HAS_OMP_SIMD=1)
    set(PRS_OPENMP_DESC "/openmp:experimental")
  else()
    find_package(OpenMP COMPONENTS CXX QUIET)
    if(OpenMP_CXX_FOUND)
      target_link_libraries(prs_core PUBLIC OpenMP::OpenMP_CXX)
      target_compile_definitions(prs_core PUBLIC PRS_HAS_OPENMP=1 PRS_HAS_OMP_SIMD=1)
      set(PRS_OPENMP_DESC "${OpenMP_CXX_FLAGS} (OpenMP ${OpenMP_CXX_VERSION})")
    else()
      # bez runtime-a: "omp simd" i dalje radi uz -fopenmp-simd, višedretvene varijante se preskaču
      target_compile_definitions(prs_core PUBLIC PRS_HAS_OMP_SIMD=1)
      set_property(SOURCE src/kernels_omp_simd.cpp APPEND PROPERTY COMPILE_OPTIONS -fopenmp-simd)
      message(STATUS "OpenMP runtime nije pronađen: višedretvene varijante neće biti dostupne")
      set(PRS_OPENMP_DESC "-fopenmp-simd only")
    endif()
  endif()
endif()

# ---------------------------------------------------------------- AVX2 (samo x86, uz runtime provjeru)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" PRS_CPU)
set(PRS_VECTOR_DESC "baseline ISA")
if(PRS_ENABLE_AVX2 AND PRS_CPU MATCHES "^(x86_64|amd64|x64|i[3-6]86)$")
  if(MSVC)
    set(PRS_AVX2_FLAG /arch:AVX2)
  else()
    set(PRS_AVX2_FLAG -mavx2)
  endif()
  # B, C, D (i F, G) koriste istu širinu vektora (256 bita) radi fer poređenja.
  set_property(SOURCE ${PRS_VECTOR_SOURCES} ${PRS_AVX2_SOURCES} ${PRS_BRANCHLESS_SCALAR_SOURCES} APPEND PROPERTY COMPILE_OPTIONS ${PRS_AVX2_FLAG})
  target_compile_definitions(prs_core PUBLIC PRS_HAS_AVX2_BUILD=1 PRS_VECTOR_REQUIRES_AVX2=1)
  set(PRS_VECTOR_DESC "${PRS_AVX2_FLAG}")
else()
  message(STATUS "AVX2 varijanta nije prevedena (ne-x86 ili isključeno); D/G koriste auto-vektorizaciju")
endif()

# ---------------------------------------------------------------- izvještaji o vektorizaciji
if(PRS_VECTORIZATION_REPORTS)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_property(SOURCE ${PRS_SCALAR_SOURCES} ${PRS_BRANCHLESS_SCALAR_SOURCES} ${PRS_VECTOR_SOURCES} APPEND PROPERTY COMPILE_OPTIONS
      -fopt-info-vec-optimized -fopt-info-vec-missed)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set_property(SOURCE ${PRS_SCALAR_SOURCES} ${PRS_VECTOR_SOURCES} APPEND PROPERTY COMPILE_OPTIONS
      -Rpass=loop-vectorize -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize)
  elseif(MSVC)
    set_property(SOURCE ${PRS_SCALAR_SOURCES} ${PRS_VECTOR_SOURCES} APPEND PROPERTY COMPILE_OPTIONS /Qvec-report:2)
  endif()
endif()

# ---------------------------------------------------------------- informacije o buildu
string(TOUPPER "${CMAKE_BUILD_TYPE}" PRS_BT_UPPER)
set(PRS_BUILD_COMPILER "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
set(PRS_BUILD_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${PRS_BT_UPPER}} ${PRS_COMMON_FLAGS} OpenMP=${PRS_OPENMP_DESC}")
string(REPLACE ";" " " PRS_BUILD_SCALAR_FLAGS "${PRS_SCALAR_FLAGS}")
string(REPLACE ";" " " PRS_VECTOR_EXTRA_STR "${PRS_VECTOR_EXTRA_FLAGS}")
set(PRS_BUILD_VECTOR_FLAGS "${PRS_VECTOR_DESC} ${PRS_VECTOR_EXTRA_STR}")
configure_file(include/build_info.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.hpp @ONLY)

# ---------------------------------------------------------------- izvršni programi
add_executable(prs_swe src/main.cpp)
target_link_libraries(prs_swe PRIVATE prs_core)

if(PRS_BUILD_TESTS)
  enable_testing()
  add_executable(prs_tests tests/correctness_tests.cpp)
  target_link_libraries(prs_tests PRIVATE prs_core)
  add_test(NAME correctness COMMAND prs_tests)
endif()
