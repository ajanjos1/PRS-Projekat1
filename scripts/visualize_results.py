#!/usr/bin/env python3
"""Uporedni prikaz "Original vs Eroded" s mapom razlike u metrima (opciono; glavni program je u C++-u).

Čita sirove podatke koje prs_erosion zapisuje (float64 .npy, bez gubitka preciznosti):
    <ime>_original_m.npy, <ime>_eroded_m.npy, <ime>_difference_m.npy, <ime>_water_max_depth_m.npy

Upotreba:
    python scripts/visualize_results.py output/<ime_mape>
    python scripts/visualize_results.py output/<ime_mape> --save slika.png

Napomena o prikazu: original i erodirana mapa se crtaju s ISTOM skalom boja (isti vmin/vmax), a razlika
s vlastitom, simetričnom skalom u metrima (negativno = erozija, pozitivno = depozicija). Tako promjena
kontrasta pri prikazu ne može izgledati kao promjena terena.
Potrebno: numpy, matplotlib.
"""
import argparse
import sys
from pathlib import Path

import numpy as np


def find(folder, suffix):
    hits = sorted(folder.glob(f"*{suffix}"))
    if not hits:
        sys.exit(f"Nema datoteke *{suffix} u {folder}")
    return hits[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder", help="izlazni folder prs_erosion (npr. output/mountain)")
    ap.add_argument("--save", help="sačuvaj sliku umjesto prikaza")
    args = ap.parse_args()

    import matplotlib
    if args.save:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    folder = Path(args.folder)
    original = np.load(find(folder, "_original_m.npy"))
    eroded = np.load(find(folder, "_eroded_m.npy"))
    diff = np.load(find(folder, "_difference_m.npy"))
    water = np.load(find(folder, "_water_max_depth_m.npy"))

    lo, hi = float(original.min()), float(original.max())
    lim = float(np.percentile(np.abs(diff[diff != 0]), 99)) if np.any(diff != 0) else 1e-3

    fig, ax = plt.subplots(2, 2, figsize=(13, 10), constrained_layout=True)
    im0 = ax[0, 0].imshow(original, cmap="terrain", vmin=lo, vmax=hi)
    ax[0, 0].set_title("Original heightmap [m]")
    fig.colorbar(im0, ax=ax[0, 0], shrink=0.8)
    im1 = ax[0, 1].imshow(eroded, cmap="terrain", vmin=lo, vmax=hi)
    ax[0, 1].set_title("Eroded heightmap [m] (ista skala)")
    fig.colorbar(im1, ax=ax[0, 1], shrink=0.8)
    im2 = ax[1, 0].imshow(diff, cmap="RdBu", vmin=-lim, vmax=lim)
    ax[1, 0].set_title(f"Δh = eroded − original [m]\nmin {diff.min():.3f}, max {diff.max():.3f} (skala ±{lim:.3f}, 99. percentil)")
    fig.colorbar(im2, ax=ax[1, 0], shrink=0.8)
    im3 = ax[1, 1].imshow(np.log10(np.maximum(water, 1e-4)), cmap="Blues")
    ax[1, 1].set_title("log10(max dubina vode [m])")
    fig.colorbar(im3, ax=ax[1, 1], shrink=0.8)
    for a in ax.ravel():
        a.set_xticks([])
        a.set_yticks([])
    fig.suptitle(folder.name)
    if args.save:
        fig.savefig(args.save, dpi=110)
        print(f"Sačuvano: {args.save}")
    else:
        plt.show()


if __name__ == "__main__":
    main()
