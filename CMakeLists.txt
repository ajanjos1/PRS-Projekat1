cmake_minimum_required(VERSION 3.16)
project(PRS_ShallowWater_Erosion VERSION 2.0 LANGUAGES CXX)

# PRS – Shallow-water and Terrain Erosion
#
# Faza 2: prs_erosion – konzolni simulator hidrauličke erozije metodom virtuelnih cijevi.
#   Ulaz: PNG/JPEG height mapa + kiša (L/m², h). Izlaz: erodirana 16-bitna PNG height mapa + izvještaj.
#   (Faza 1 – SIMD/OpenMP benchmark – uklonjena je iz radnog stabla; ostaje u git historiji, commit 78e7b52.)
#
# GRANA `paralelizacija`: dodana je optimizacija i paralelizacija na procesoru (OpenMP + `#pragma omp simd`).
#   Faza 2 je po zahtjevu AC7 bila namjerno serijska; sa -DPRS_ENABLE_OPENMP=OFF pragme se ignorišu, pa je
#   program ponovo čisto serijski (ali s optimizovanom aritmetikom, vidi ispod).
#   Teren je bit po bit identičan bez obzira na broj niti. U odnosu na fazu 2 postoji mala razlika u
#   zaokruživanju, jer formula trenja sada ima jedno dijeljenje umjesto dva; bilansi su nepromijenjeni.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PRS_BUILD_TESTS "Testovi (prs_erosion_tests)" ON)
# OFF vraća ponašanje faze 2 (čisto serijski program, bez ijedne niti).
option(PRS_ENABLE_OPENMP "Paralelizacija petlji po ćelijama (OpenMP)" ON)

# Runtime biblioteke se vezuju DINAMICKI (na Windowsu /MD, ne /MT). To je i CMake-ov podrazumijevani izbor,
# ali se ovdje pise izricito: profileri paralelnih regija traze dinamicki vezan OpenMP runtime, jer staticka
# verzija ne nosi oznake preko kojih profiler te regije prepoznaje.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

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

# ---------------------------------------------------------------- OpenMP (paralelizacija po redovima mreže)
# Faze jednog koraka su petlje u kojima ćelija piše samo svoje vrijednosti, pa se dijele po redovima.
# Bez OpenMP-a se program i dalje prevodi i radi serijski (pragme se tada ignorišu).
# macOS: libomp dolazi iz Homebrewa, pa se putanja prosljeđuje preko OpenMP_ROOT.
set(PRS_OPENMP_AKTIVAN FALSE)
if(PRS_ENABLE_OPENMP)
  if(MSVC)
    # Visual Studio: klasični /openmp je OpenMP 2.0 i NE podržava ni reduction(max:...) ni `omp simd`.
    # /openmp:llvm daje OpenMP 4.5, pa se kod prevodi isto kao na GCC-u i Clangu.
    target_compile_options(prs_erosion_core PUBLIC /openmp:llvm)
    set(PRS_OPENMP_AKTIVAN TRUE)
    message(STATUS "OpenMP: UKLJUCEN (/openmp:llvm)")
  else()
    if(APPLE AND NOT DEFINED OpenMP_ROOT)
      execute_process(COMMAND brew --prefix libomp OUTPUT_VARIABLE _brew_libomp
                      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
      if(_brew_libomp)
        set(OpenMP_ROOT "${_brew_libomp}")
      endif()
    endif()
    find_package(OpenMP)
    if(OpenMP_CXX_FOUND)
      target_link_libraries(prs_erosion_core PUBLIC OpenMP::OpenMP_CXX)
      set(PRS_OPENMP_AKTIVAN TRUE)
      message(STATUS "OpenMP: UKLJUCEN (${OpenMP_CXX_FLAGS})")
    endif()
  endif()
endif()
if(NOT PRS_OPENMP_AKTIVAN)
  if(MSVC)
    target_compile_options(prs_erosion_core PUBLIC /wd4068) # "unknown pragma" za #pragma omp
  else()
    target_compile_options(prs_erosion_core PUBLIC -Wno-unknown-pragmas)
  endif()
  message(STATUS "OpenMP: iskljucen - program radi serijski (ponasanje faze 2)")
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


# ---------------------------------------------------------------- Windows: biblioteke pored programa
# Posto se runtime vezuje dinamicki, Windows biblioteke trazi prvo pored .exe-a, pa tek onda kroz PATH.
# Program pokrenut ispod profilera ili prenesen na drugi racunar tada cesto ne krene. Zato se ono sto se
# da pronaci kopira pored programa odmah po prevodjenju.
# Na macOS-u i Linuxu ovo nije potrebno: puna putanja do libomp.dylib / libomp.so upisuje se u sam program.
if(WIN32)
  set(_prs_exe prs_erosion)
  if(PRS_BUILD_TESTS)
    list(APPEND _prs_exe prs_erosion_tests)
  endif()

  # 1) DLL-ovi uvezenih ciljeva (npr. kad se OpenMP nadje preko find_package).
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
    foreach(_tgt IN LISTS _prs_exe)
      add_custom_command(TARGET ${_tgt} POST_BUILD
        COMMAND "$<$<BOOL:$<TARGET_RUNTIME_DLLS:${_tgt}>>:${CMAKE_COMMAND};-E;copy_if_different;$<TARGET_RUNTIME_DLLS:${_tgt}>;$<TARGET_FILE_DIR:${_tgt}>>"
        COMMAND_EXPAND_LISTS)
    endforeach()
  endif()

  # 2) Uz /openmp:llvm runtime NE dolazi preko uvezenog cilja, nego ga kompajler sam ulinka, pa ga gornji
  #    korak ne vidi. Zato se libomp140 trazi u Visual Studio instalaciji i kopira ako se nadje.
  if(MSVC AND PRS_OPENMP_AKTIVAN)
    find_file(PRS_LIBOMP_DLL
      NAMES libomp140.x86_64.dll libomp140.x86.dll libomp140d.x86_64.dll
      HINTS "$ENV{VCToolsRedistDir}" "$ENV{VCToolsInstallDir}" "$ENV{VCINSTALLDIR}"
      PATH_SUFFIXES x64/Microsoft.VC143.OpenMP.LLVM x86/Microsoft.VC143.OpenMP.LLVM
                    x64/Microsoft.VC142.OpenMP.LLVM bin/Hostx64/x64 bin/Hostx86/x86
      NO_DEFAULT_PATH)
    if(PRS_LIBOMP_DLL)
      foreach(_tgt IN LISTS _prs_exe)
        add_custom_command(TARGET ${_tgt} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PRS_LIBOMP_DLL}" "$<TARGET_FILE_DIR:${_tgt}>")
      endforeach()
      message(STATUS "OpenMP DLL: kopira se pored programa (${PRS_LIBOMP_DLL})")
    else()
      message(STATUS "OpenMP DLL: libomp140 nije nadjen automatski. Ako program ne krene izvan Visual Studija, "
                     "kopiraj libomp140.x86_64.dll pored .exe-a ili ga podesi sa -DPRS_LIBOMP_DLL=<puna putanja>.")
    endif()
  endif()
endif()
