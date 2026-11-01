#!/usr/bin/env python3
"""Vizualizacija rezultata simulacije (izlaz načina rada "prs_swe simulate").

Upotreba: python3 scripts/visualize_simulation.py results/sim512 results/figures
Ulaz: terrain_initial.npy, terrain_final.npy, water_final.npy, sediment_final.npy, speed_final.npy,
      water_XXXXXX.npy (snimci), diagnostics.csv, meta.txt
"""
import csv
import glob
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from matplotlib.colors import LightSource, LinearSegmentedColormap, LogNorm  # noqa: E402

INK2 = "#52514e"
SURFACE = "#fcfcfb"
plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 9, "figure.facecolor": SURFACE,
                     "savefig.facecolor": SURFACE, "axes.titlesize": 10, "axes.titleweight": "bold"})

# sekvencijalno: jedna nijansa (plava za vodu, narandžasta za brzinu); divergentno: plava - siva - crvena
BLUE_SEQ = LinearSegmentedColormap.from_list("blue_seq", ["#f7f9fc", "#cde2fb", "#86b6ef", "#3987e5", "#1c5cab", "#0d366b"])
ORANGE_SEQ = LinearSegmentedColormap.from_list("orange_seq", ["#fdf6f1", "#f9d2bd", "#f3a079", "#eb6834", "#b8461b", "#7a2c0f"])
DIVERGING = LinearSegmentedColormap.from_list("div", ["#7a1f1f", "#d03b3b", "#ec9a8f", "#f0efec", "#86b6ef", "#2a78d6", "#0d366b"])


def meta(path):
    out = {}
    with open(os.path.join(path, "meta.txt")) as f:
        for line in f:
            if "=" in line:
                k, v = line.strip().split("=", 1)
                out[k] = v
    return out


def extent(m):
    w, h, L = int(m["W"]), int(m["H"]), float(m["cell_size"])
    return [0, w * L, h * L, 0]


def hillshade(ax, b, ext, title, vmin, vmax):
    ls = LightSource(azdeg=315, altdeg=40)
    rgb = ls.shade(b, cmap=plt.cm.gist_earth, vert_exag=3.0, blend_mode="soft", vmin=vmin, vmax=vmax)
    ax.imshow(rgb, extent=ext)
    ax.set_title(title, loc="left")
    ax.set_xlabel("x [m]")
    ax.set_ylabel("y [m]")


def save(fig, out, name):
    p = os.path.join(out, name)
    fig.savefig(p, dpi=200, bbox_inches="tight")
    plt.close(fig)
    print("  ", p)


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "results/sim512"
    out = sys.argv[2] if len(sys.argv) > 2 else "results/figures"
    os.makedirs(out, exist_ok=True)
    m = meta(src)
    ext = extent(m)
    t_end = int(m["steps"]) * float(m["dt"])
    b0 = np.load(os.path.join(src, "terrain_initial.npy"))
    b1 = np.load(os.path.join(src, "terrain_final.npy"))
    w = np.load(os.path.join(src, "water_final.npy"))
    v = np.load(os.path.join(src, "speed_final.npy"))
    print("Vizualizacija simulacije:")

    # 1) teren prije i poslije (hillshade)
    fig, ax = plt.subplots(1, 2, figsize=(7.2, 3.6))
    vmin, vmax = float(min(b0.min(), b1.min())), float(max(b0.max(), b1.max()))
    hillshade(ax[0], b0, ext, "Teren prije simulacije", vmin, vmax)
    hillshade(ax[1], b1, ext, f"Teren nakon {t_end:.0f} s erozije", vmin, vmax)
    ax[1].set_ylabel("")
    sm = plt.cm.ScalarMappable(cmap=plt.cm.gist_earth, norm=plt.Normalize(vmin, vmax))
    fig.colorbar(sm, ax=ax, shrink=0.75, label="visina terena b [m]")
    save(fig, out, "sim_terrain_before_after.png")

    # 2) razlika visine terena
    db = b1 - b0
    lim = float(np.percentile(np.abs(db), 99.5))
    fig, ax = plt.subplots(figsize=(4.6, 3.8))
    im = ax.imshow(db, cmap=DIVERGING, vmin=-lim, vmax=lim, extent=ext)
    fig.colorbar(im, ax=ax, label="Δb = b(T) − b(0) [m]\n(crveno: erozija, plavo: depozicija)")
    ax.set_title("Promjena visine terena", loc="left")
    ax.set_xlabel("x [m]")
    ax.set_ylabel("y [m]")
    save(fig, out, "sim_terrain_difference.png")

    # 3) dubina vode i brzina toka
    fig, ax = plt.subplots(1, 2, figsize=(7.2, 3.3))
    im0 = ax[0].imshow(w, cmap=BLUE_SEQ, vmin=0, vmax=float(np.percentile(w, 99.5)), extent=ext)
    fig.colorbar(im0, ax=ax[0], shrink=0.85, label="d [m]")
    ax[0].set_title("Dubina vode", loc="left")
    vv = np.where(w > 0.01, v, np.nan)  # brzina samo gdje ima vode (d > 1 cm)
    vmax = float(np.nanpercentile(vv, 99))
    im1 = ax[1].imshow(vv, cmap=ORANGE_SEQ, norm=LogNorm(vmin=max(vmax * 1e-3, 1e-3), vmax=vmax), extent=ext)
    ax[1].set_facecolor("#e4e3df")
    fig.colorbar(im1, ax=ax[1], shrink=0.85, label="|v| [m/s] (log)")
    ax[1].set_title("Brzina toka (d > 1 cm)", loc="left")
    for a in ax:
        a.set_xlabel("x [m]")
    ax[0].set_ylabel("y [m]")
    fig.tight_layout()
    save(fig, out, "sim_water_velocity.png")

    # 4) evolucija vode (snimci)
    snaps = sorted(glob.glob(os.path.join(src, "water_0*.npy")))
    if snaps:
        snaps = snaps[-4:]
        fig, ax = plt.subplots(1, len(snaps), figsize=(7.2, 2.2))
        ax = np.atleast_1d(ax)
        vmax = float(np.percentile(np.load(snaps[-1]), 99.5))
        for a, p in zip(ax, snaps):
            step = int(os.path.basename(p)[6:12])
            a.imshow(np.load(p), cmap=BLUE_SEQ, vmin=0, vmax=vmax, extent=ext)
            a.set_title(f"t = {step * float(m['dt']):.0f} s", fontsize=8, loc="left")
            a.set_xticks([])
            a.set_yticks([])
        sm = plt.cm.ScalarMappable(cmap=BLUE_SEQ, norm=plt.Normalize(0, vmax))
        fig.colorbar(sm, ax=list(ax), shrink=0.8, label="d [m]")
        save(fig, out, "sim_water_evolution.png")

    # 5) bilansi mase iz dijagnostike
    with open(os.path.join(src, "diagnostics.csv")) as f:
        rows = list(csv.DictReader(f))
    t = np.array([float(r["time_s"]) for r in rows])
    water = np.array([float(r["water_m3"]) for r in rows])
    solid = np.array([float(r["terrain_plus_sediment_m3"]) for r in rows])
    sed = np.array([float(r["sediment_m3"]) for r in rows])
    rain = np.array([float(r["rain_added_m3"]) for r in rows])
    fig, ax = plt.subplots(1, 2, figsize=(7.2, 2.8))
    ax[0].plot(t, rain, color="#86b6ef", linewidth=2, label="ukupno pala kiša")
    ax[0].plot(t, water, color="#2a78d6", linewidth=2, label="voda u domeni")
    ax[0].plot(t, sed, color="#eb6834", linewidth=2, label="suspendovani sediment")
    ax[0].fill_between(t, water, rain, color="#cde2fb", alpha=0.6, linewidth=0)
    ax[0].text(t[-1] * 0.55, (rain[-1] + water[-1]) / 2, "isparilo", fontsize=7.5, color="#52514e")
    ax[0].set_xlabel("t [s]")
    ax[0].set_ylabel("zapremina [m³]")
    ax[0].legend(fontsize=7, loc="upper left", frameon=False)
    ax[0].set_title("Voda i sediment", loc="left")
    rel = (solid - solid[0]) / solid[0]
    ax[1].plot(t, rel * 1e6, color="#2a78d6", linewidth=2)
    ax[1].set_xlabel("t [s]")
    ax[1].set_ylabel("relativna promjena [ppm]")
    ax[1].set_title("Očuvanje Σ(b + s)", loc="left")
    for a in ax:
        a.grid(True, color="#e4e3df", linewidth=0.6)
        a.spines[["top", "right"]].set_visible(False)
    fig.tight_layout()
    save(fig, out, "sim_mass_balance.png")

    # sažetak za dokument
    eroded = float(np.sum(np.clip(-db, 0, None))) * float(m["cell_size"]) ** 2
    deposited = float(np.sum(np.clip(db, 0, None))) * float(m["cell_size"]) ** 2
    with open(os.path.join(out, "sim_summary.txt"), "w") as f:
        f.write(f"grid={m['W']}x{m['H']} steps={m['steps']} dt={m['dt']} T={t_end:.1f}s variant={m['variant']} threads={m['threads']} wall={m['wall_time_s']}s\n")
        f.write(f"terrain range initial: {b0.min():.2f}..{b0.max():.2f} m\n")
        f.write(f"db: min={db.min():.3f} max={db.max():.3f} p1={np.percentile(db,1):.3f} p99={np.percentile(db,99):.3f} m\n")
        f.write(f"eroded volume={eroded:.1f} m3, deposited volume={deposited:.1f} m3, suspended={sed[-1]:.1f} m3\n")
        f.write(f"water: final volume={water[-1]:.1f} m3, rain total={rain[-1]:.1f} m3, max depth={w.max():.3f} m, wet(d>1cm) fraction={(w>0.01).mean():.3f}\n")
        f.write(f"solid (b+s) relative drift: max |rel| = {np.max(np.abs(rel)):.3e}\n")
        f.write(f"speed (d>1cm): median={np.nanmedian(vv):.3f} p99={np.nanpercentile(vv,99):.3f} m/s\n")
    print("  ", os.path.join(out, "sim_summary.txt"))


if __name__ == "__main__":
    main()
