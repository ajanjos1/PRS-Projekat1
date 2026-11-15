# Mjerni programi (infrastruktura). Ne diraju CMakeLists.txt projekta.
#   make -C bench          prevede bench_faze u build-prof/
#   make -C bench run      prevede i pokrene
#
# Linka se na vec izgradjenu biblioteku iz build-prof (Release + debug simboli):
#   cmake -S . -B build-prof -DCMAKE_BUILD_TYPE=RelWithDebInfo \
#     -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O3 -g -DNDEBUG -fno-omit-frame-pointer"
#   cmake --build build-prof -j
KORIJEN := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
BUILD   := $(KORIJEN)/build-prof
LIB     := $(BUILD)/libprs_erosion_core.a
SISTEM  := $(shell uname -s)
ARH     := $(shell uname -m)

ifeq ($(SISTEM),Darwin)
  CXX     := clang++
  SDK     := $(shell xcrun --show-sdk-path)
  # Command Line Tools bez libc++ zaglavlja -> uzimaju se iz SDK-a (problem postoji samo na macOS-u).
  FIX     := $(if $(wildcard /Library/Developer/CommandLineTools/usr/include/c++/v1/iostream),,-nostdinc++ -isystem $(SDK)/usr/include/c++/v1)
  ARHFLAG := $(if $(filter arm64,$(ARH)),-mcpu=apple-m1,-march=native)
  # Na macOS-u OpenMP dolazi iz zasebnog paketa (Homebrew libomp), jer ga Apple clang nema ugradjenog.
  LIBOMP  := $(shell brew --prefix libomp 2>/dev/null)
  OMP     := $(if $(LIBOMP),-Xpreprocessor -fopenmp -I$(LIBOMP)/include -L$(LIBOMP)/lib -lomp)
else
  # Linux (GCC ili Clang): OpenMP je dio kompajlera, a -march=native ukljucuje AVX2 gdje postoji.
  CXX     := $(if $(shell command -v g++ 2>/dev/null),g++,clang++)
  FIX     :=
  ARHFLAG := -march=native
  LIBOMP  := ugradjen
  OMP     := -fopenmp
endif
CXXFLAGS := -O3 -g -std=c++20 $(ARHFLAG) $(FIX) -I$(KORIJEN)/include

all: provjeri-omp $(BUILD)/bench_faze

# Bez libomp-a bi linkanje palo na gomili nerazumljivih __kmpc_* simbola, pa se javlja odmah i jasno.
# (Ako je jezgro gradjeno sa -DPRS_ENABLE_OPENMP=OFF, libomp ne treba: tad podesi PRS_BEZ_OMP=1.)
provjeri-omp:
ifeq ($(strip $(LIBOMP))$(PRS_BEZ_OMP),)
	@echo "Nedostaje libomp (OpenMP). Na macOS-u se instalira sa:  brew install libomp"; 	 echo "Ako je projekat gradjen bez OpenMP-a, pokreni:  make -C bench PRS_BEZ_OMP=1"; exit 1
else
	@:
endif

$(BUILD)/bench_faze: bench_faze.cpp $(LIB)
	$(CXX) $(CXXFLAGS) $< $(LIB) $(OMP) -o $@

$(LIB):
	@echo "Nedostaje $(LIB) – prvo pokreni: cmake --build build-prof -j"; exit 1

ijk: $(BUILD)/ijk_vs_ikj

$(BUILD)/ijk_vs_ikj: ijk_vs_ikj.cpp
	$(CXX) $(CXXFLAGS) $< $(OMP) -o $@

run: $(BUILD)/bench_faze
	cd $(KORIJEN) && ./build-prof/bench_faze

clean:
	rm -f $(BUILD)/bench_faze

.PHONY: all run ijk clean provjeri-omp

# ---------------------------------------------------------------- mjerenje po nitima (zeleno/crveno)
# Poseban build jer se instrumentacija uključuje makroom PRS_PROFIL_NITI.
PROFIL_DIR := $(KORIJEN)/build-profil
profil:
	cmake -S $(KORIJEN) -B $(PROFIL_DIR) -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPRS_BUILD_TESTS=OFF \
	  -DCMAKE_CXX_FLAGS="$(FIX) -DPRS_PROFIL_NITI=1" \
	  -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O3 -g -DNDEBUG" >/dev/null
	cmake --build $(PROFIL_DIR) -j
	$(CXX) $(CXXFLAGS) -DPRS_PROFIL_NITI=1 profil_niti.cpp $(PROFIL_DIR)/libprs_erosion_core.a $(OMP) \
	  -o $(PROFIL_DIR)/profil_niti
	@echo "Pokreni: OMP_NUM_THREADS=6 ./build-profil/profil_niti"

.PHONY: profil
