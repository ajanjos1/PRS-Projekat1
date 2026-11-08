cmake_minimum_required(VERSION 3.16)
project(PRS_ShallowWater_Erosion VERSION 2.0 LANGUAGES CXX)

# PRS – Shallow-water and Terrain Erosion
#
# Faza 2 (aktuelna): prs_erosion – SERIJSKI konzolni simulator hidrauličke erozije metodom virtuelnih cijevi.
#   Ulaz: PNG/JPEG height mapa + kiša (L/m², h). Izlaz: erodirana 16-bitna PNG height mapa + izvještaj.
#   Namjerno BEZ OpenMP-a, bez SIMD intrinsics/AVX2 zastavica, bez dretvi i GPU-a (zahtjev AC7).
#   (Faza 1 – SIMD/OpenMP benchmark – uklonjena je iz radnog stabla; ostaje u git historiji, commit 78e7b52.)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PRS_BUILD_TESTS "Testovi (prs_erosion_tests)" ON)

# ---------------------------------------------------------------- faza 2: biblioteka simulatora
# Numerika (VirtualPipesSolver, HydraulicErosion, ErosionSimulation) ne zavisi od konzole ni od biblioteke
# za slike; HeightmapIO/ImageWriter/ResultExport su odvojeni slojevi za ulaz i izlaz.
add_library(prs_erosion_core STATIC
  src/Heightmap.cpp
  src/Rainfall.cpp
  src/SimulationConfig.cpp
  src/VirtualPipesSolver.cpp
  src/HydraulicErosion.cpp
  src/ErosionSimulation.cpp
  src/HeightmapIO.cpp
  src/ImageWriter.cpp
  src/stb_image_impl.cpp
  src/ResultExport.cpp
  src/ConsoleInput.cpp
  src/TerrainMetadata.cpp
  src/LocalTerrainServer.cpp
)
target_include_directories(prs_erosion_core PUBLIC include)
if(WIN32)
  target_link_libraries(prs_erosion_core PUBLIC ws2_32) # Winsock: lokalni server za birač terena (127.0.0.1)
endif()
if(MSVC)
  # /utf-8: izvorni fajlovi su UTF-8 (dijakritici u komentarima); /permissive-: standardni C++
  target_compile_options(prs_erosion_core PUBLIC /W4 /EHsc /utf-8 /permissive-)
else()
  target_compile_options(prs_erosion_core PUBLIC -Wall -Wextra -Wpedantic)
endif()

add_executable(prs_erosion src/main.cpp)
target_link_libraries(prs_erosion PRIVATE prs_erosion_core)
if(WIN32)
  target_link_libraries(prs_erosion PRIVATE shell32) # CommandLineToArgvW (UTF-8 argumenti)
endif()

if(PRS_BUILD_TESTS)
  enable_testing()
  add_executable(prs_erosion_tests
    tests/test_main.cpp
    tests/HeightmapTests.cpp
    tests/RainfallTests.cpp
    tests/WaterConservationTests.cpp
    tests/ErosionTests.cpp
    tests/ScenarioTests.cpp
    tests/TerrainMetadataTests.cpp
    tests/TerrainReceiverTests.cpp
  )
  target_link_libraries(prs_erosion_tests PRIVATE prs_erosion_core)
  # Radni folder = korijen projekta, da testovi nađu assets/heightmaps (primjeri height mapa).
  add_test(NAME erosion_tests COMMAND prs_erosion_tests WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

