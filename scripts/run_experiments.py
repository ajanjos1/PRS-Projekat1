#!/usr/bin/env python3
"""Eksperimenti 4–6 na stvarnim height mapama: pokreće prs_erosion (komandna linija) i iz JSON izvještaja
pravi tabelu u Markdown-u. Sve brojke dolaze iz izvještaja programa (ništa se ne računa "napamet").

Upotreba (iz korijena projekta):
    python scripts/run_experiments.py --exe build/prs_erosion                 # Linux
    python scripts/run_experiments.py --exe build-vs\\Release\\prs_erosion.exe   # Windows
    python scripts/run_experiments.py --exe ... --quick                         # samo kratki scenariji

Izlaz: output/experiments/<scenarij>/ (rezultati simulacija) i output/experiments/experiments.md (tabele).
Potreban je samo Python 3 (standardna biblioteka).
"""
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

MAPS = {
    "primjer_1": "assets/heightmaps/primjer_1/height-map-input-try-1-height-map-16bit-texturemap-app-2026-11-08.png",
    "primjer_2": "assets/heightmaps/primjer_2/mocvarno-tlo-height-map-16bit-texturemap-app-2026-11-08.png",
    "primjer_3": "assets/heightmaps/primjer_3/mali-otocic-height-map-16bit-texturemap-app-2026-11-08.png",
}


def run(exe, out_root, name, input_path, rain, hours, extra=()):
    out_dir = out_root / name
    cmd = [str(exe), "--input", input_path, "--rain", str(rain), "--hours", str(hours), "--out", str(out_dir), *extra]
    print(f"--> {name}: {' '.join(cmd[1:])}", flush=True)
    t0 = time.perf_counter()
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    wall = time.perf_counter() - t0
    if proc.returncode != 0:
        print(proc.stdout)
        raise SystemExit(f"{name}: prs_erosion je vratio kod {proc.returncode}")
    report = json.loads((out_dir / "simulation_report.json").read_text(encoding="utf-8"))
    report["_name"] = name
    report["_process_seconds"] = wall
    print(f"    gotovo za {wall:.1f} s", flush=True)
    return report


def row(r):
    n, w, m, t, rain = r["numerics"], r["waterBalance"], r["materialBalance"], r["terrainChange"], r["rainfall"]
    inp, sc = r["input"], r["terrainScale"]
    return (f"| {r['_name']} | {inp['width']}x{inp['height']} | {sc['cellSizeMeters']:g} | {rain['totalLitersPerSquareMeter']:g} "
            f"| {rain['durationHours']:g} | {rain['intensityMillimetersPerHour']:g} | {n['steps']} | {n['wallSeconds']:.1f} "
            f"| {t['netLoweredVolume']:.0f} | {t['maxLowering']:.3f} | {t['maxRaise']:.3f} | {t['cellsChangedMoreThan1mm']} "
            f"| {w['peakDepth']:.2f} | {w['balanceRelativeError']:.1e} | {m['balanceRelativeError']:.1e} |")


HEADER = ("| scenarij | mreža | ćelija [m] | kiša [L/m²] | trajanje [h] | intenzitet [mm/h] | koraka | vrijeme [s] "
          "| sniženo [m³] | max sniženje [m] | max povišenje [m] | ćelija >1 mm | max dubina [m] | greška vode | greška materijala |\n"
          "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True, help="putanja do prs_erosion")
    ap.add_argument("--out", default="output/experiments")
    ap.add_argument("--quick", action="store_true", help="samo primjer_1 i kraći scenariji")
    args = ap.parse_args()
    exe = Path(args.exe)
    if not exe.exists():
        sys.exit(f"Ne postoji: {exe}")
    out_root = Path(args.out)
    out_root.mkdir(parents=True, exist_ok=True)

    sections = []
    # Test 4: stvarne mape, 100 L/m² za 1 h, podrazumijevana skala (ćelija 30 m, 0..100 m)
    maps = ["primjer_1"] if args.quick else list(MAPS)
    sections.append(("Test 4 – stvarne height mape (100 L/m² za 1 h)",
                     [run(exe, out_root, f"t4_{m}", MAPS[m], 100, 1) for m in maps]))
    # Test 5: ista mapa i trajanje, različita količina kiše
    sections.append(("Test 5 – količina kiše (primjer_1, 1 h)",
                     [run(exe, out_root, f"t5_{mm}mm", MAPS["primjer_1"], mm, 1) for mm in (10, 50, 100, 200)]))
    # Test 6: ista ukupna kiša, različito trajanje (intenzitet)
    durations = (1, 5) if args.quick else (1, 5, 10)
    sections.append(("Test 6 – isto ukupno (100 L/m²), različito trajanje (primjer_1)",
                     [run(exe, out_root, f"t6_{h}h", MAPS["primjer_1"], 100, h) for h in durations]))

    lines = ["# Rezultati eksperimenata (generisano: scripts/run_experiments.py)", ""]
    for title, reports in sections:
        lines += [f"## {title}", "", HEADER] + [row(r) for r in reports] + [""]
    lines.append("Vrijednosti su iz simulation_report.json svakog pokretanja; vrijeme je vrijeme izvršavanja simulacije.")
    md = out_root / "experiments.md"
    md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"\nTabela: {md}")


if __name__ == "__main__":
    main()
