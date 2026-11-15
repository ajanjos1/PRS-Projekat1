#!/bin/bash
# Provjera rezultata optimizacije i paralelizacije na CPU-u, u jednom prolazu.
# Pokreće se jednom komandom iz korijena projekta:
#
#     ./bench/demo.sh            puna verzija (~90 s)
#     ./bench/demo.sh brzo       skraćena (~45 s)
#
# Redom radi sljedeće:
#   1. tačnost  – svi testovi moraju proći prije bilo kakvog mjerenja
#   2. ISTA simulacija serijski i na 6 niti, uz provjeru da su rezultati isti
#   3. rad i čekanje po nitima (zeleno/crveno) direktno u terminalu
#   4. koliko znači redoslijed petlji (IJK vs IKJ)
#   5. grafovi
set -u
cd "$(dirname "$0")/.." || exit 1

greska() { printf '\n\033[31m GRESKA: %s\033[0m\n' "$1"; exit 1; }
[ -x ./build-prof/prs_erosion ] || greska "nedostaje build-prof/prs_erosion -> cmake --build build-prof -j"
[ -x ./build-prof/prs_erosion_tests ] || greska "nedostaje build-prof/prs_erosion_tests"
[ -x ./build-prof/ijk_vs_ikj ] || greska "nedostaje build-prof/ijk_vs_ikj -> make -C bench ijk"

BRZO=${1:-}
MAPA=assets/heightmaps/primjer_3/mali-otocic-height-map-16bit-texturemap-app-2026-11-08.png
SATI=1;  MATRICA=1024
[ "$BRZO" = "brzo" ] && { SATI=0.4; MATRICA=512; }
NITI=$(sysctl -n hw.perflevel0.physicalcpu 2>/dev/null || echo 6)   # broj BRZIH jezgara

Z=$'\e[32m'; C=$'\e[31m'; B=$'\e[1m'; K=$'\e[0m'
naslov() { printf '\n%s══ %s ══%s\n\n' "$B" "$1" "$K"; }
sek() { python3 -c "import sys;print(f'{float(sys.argv[1]):.2f}')" "$1"; }

printf '%s\n' "$B"
cat <<'EOF'
 PRS-Projekat1 — simulacija erozije terena
 Optimizacija i paralelizacija na procesoru
EOF
printf '%s Mreza 736 x 736 (541 696 celija), kisa 100 mm, %s h\n' "$K" "$SATI"
printf ' Procesor: %s, brzih jezgara: %s\n' "$(sysctl -n machdep.cpu.brand_string)" "$NITI"

# ---------------------------------------------------------------- 1
naslov "1. TACNOST — bez ovoga nema smisla mjeriti brzinu"
# Izlaz se cuva da se moze ispisati zadnji red, ali se gleda STVARNI status testova,
# a ne status komande `tail` (zato ne ide direktno u cijev).
TESTLOG=$(mktemp)
if ./build-prof/prs_erosion_tests >"$TESTLOG" 2>&1; then
  tail -1 "$TESTLOG"
else
  tail -5 "$TESTLOG"
  printf '\n %sTESTOVI NISU PROSLI — mjerenje brzine nema smisla, demo se prekida.%s\n' "$C" "$K"
  rm -f "$TESTLOG"; exit 1
fi
rm -f "$TESTLOG"

# ---------------------------------------------------------------- 2
naslov "2. ISTA SIMULACIJA: serijski naspram $NITI niti"
rm -rf /tmp/prs_demo_1 /tmp/prs_demo_n

# Napredak se namjerno NE sakriva: vidi se da program radi i koliko je koraka odradio.
printf ' %s--- 1 NIT ---%s\n' "$C" "$K"
T1=$( { /usr/bin/time -p env OMP_NUM_THREADS=1 ./build-prof/prs_erosion --input "$MAPA" \
        --rain 100 --hours $SATI --out /tmp/prs_demo_1 | grep --line-buffered -E 'Napredak.*(0|5)\.0%' ; } 2>&1 \
      | tee /dev/stderr | awk '/^real/{print $2}')

printf '\n %s--- %s NITI ---%s\n' "$Z" "$NITI" "$K"
TN=$( { /usr/bin/time -p env OMP_NUM_THREADS=$NITI ./build-prof/prs_erosion --input "$MAPA" \
        --rain 100 --hours $SATI --out /tmp/prs_demo_n | grep --line-buffered -E 'Napredak.*(0|5)\.0%' ; } 2>&1 \
      | tee /dev/stderr | awk '/^real/{print $2}')

[ -n "$T1" ] && [ -n "$TN" ] || greska "simulacija nije zavrsila; provjeri da postoji $MAPA"
printf '\n serijski  : %s%s s%s\n' "$C" "$(sek "$T1")" "$K"
printf ' paralelno : %s%s s%s\n' "$Z" "$(sek "$TN")" "$K"

python3 - "$T1" "$TN" <<'PY'
import sys
t1, tn = float(sys.argv[1]), float(sys.argv[2])
print(f"\n \033[1mUbrzanje: {t1/tn:.2f}x\033[0m   (ustedjeno {t1-tn:.1f} s na jednom pokretanju)")
PY

cat <<'KRAJ'

 Obratiti paznju na redove iznad: "real" je stvarno proteklo vrijeme, a "user"
 je zbir vremena SVIH niti. Kod paralelnog pokretanja je "user" veci od
 "real" - to i jeste paralelizam: vise jezgara radi istovremeno.
KRAJ

printf '\n Jesu li rezultati isti? '
python3 - <<'PY'
import glob, numpy as np
import sys
f1 = glob.glob('/tmp/prs_demo_1/*_eroded_m.npy'); f2 = glob.glob('/tmp/prs_demo_n/*_eroded_m.npy')
if not f1 or not f2:
    print('nije moguce uporediti: izlazni fajlovi nisu napravljeni'); sys.exit(0)
a = np.load(f1[0])
b = np.load(f2[0])
d = float(np.abs(a - b).max())
raspon = float(a.max() - a.min())
ocjena = "\033[32mISTO\033[0m" if d < 1e-6 else "RAZLIKA"
print("najveca razlika visine {:.3e} m na rasponu {:.1f} m  ({})".format(d, raspon, ocjena))
PY

# ---------------------------------------------------------------- 3
naslov "3. STA RADE NITI — zeleno je racun, crveno je cekanje na barijeri"
if [ ! -x ./build-profil/profil_niti ]; then
  # Greske se NE gutaju: ako prevodjenje ne uspije, mora se vidjeti zasto.
  make -s -C bench profil || printf ' %sNije uspjelo prevodjenje profil_niti (vidi poruku iznad).%s\n' "$C" "$K"
fi
if [ -x ./build-profil/profil_niti ]; then
  OMP_NUM_THREADS=$NITI ./build-profil/profil_niti | tail -n +2
else
  printf ' (preskoceno: ./build-profil/profil_niti ne postoji)\n'
fi

# ---------------------------------------------------------------- 4
naslov "4. KOLIKO ZNACI REDOSLIJED PETLJI (mnozenje matrica $MATRICA x $MATRICA)"
OMP_NUM_THREADS=$NITI ./build-prof/ijk_vs_ikj $MATRICA 3

# ---------------------------------------------------------------- 5
naslov "5. GRAFOVI"
# --svjeze: traka po nitima se crta iz mjerenja koje je upravo napravljeno u koraku 3,
# a ne iz brojeva zapisanih u grafovi.py (koji su s mog racunara).
python3 bench/grafovi.py --svjeze
if [ "$BRZO" != "brzo" ]; then
    open bench/rezultati/faze_prije_poslije.png bench/rezultati/skaliranje.png \
         bench/rezultati/traka_niti.png bench/rezultati/ijk_vs_ikj.png 2>/dev/null \
      && echo " (slike otvorene u pregledniku)"
fi

naslov "GOTOVO"
# Zavrsni red se racuna iz onoga sto je upravo izmjereno na OVOM racunaru.
NS1=$(OMP_NUM_THREADS=1 ./build-prof/bench_faze 2>/dev/null | awk '/UKUPNO/{print $4}')
NSN=$(OMP_NUM_THREADS=$NITI ./build-prof/bench_faze 2>/dev/null | awk '/UKUPNO/{print $4}')
printf '\n Izmjereno sada (%s): %s ns po celiji na 1 niti  ->  %s ns na %s niti\n' \
       "$(uname -m)" "${NS1:-?}" "${NSN:-?}" "$NITI"
printf ' Simulacija iz koraka 2: %s s  ->  %s s\n' "$(sek "$T1")" "$(sek "$TN")"
printf ' Grafovi: bench/rezultati/   .  opis izmjena: README.md\n'
