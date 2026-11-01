#!/usr/bin/env python3
"""Grafikoni performansi iz results/benchmarks.csv (i stream.csv).

Upotreba: python3 scripts/plot_results.py results results/figures
Boje: validirana kategorijalna paleta (plava, narandžasta, akva - prva tri slota prolaze
CVD provjeru za sve parove) i ordinalna plava rampa za veličine mreže.
"""
import csv
import os
import sys
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

SERIES = ["#2a78d6", "#eb6834", "#1baf7a"]           # E, F, G (kategorijalno)
MARKERS = ["o", "s", "^"]
GRID_RAMP = ["#86b6ef", "#3987e5", "#256abf", "#184f95"]  # 128 -> 1024 (ordinalno)
INK = "#0b0b0b"
INK2 = "#52514e"
GRIDC = "#e4e3df"
SURFACE = "#fcfcfb"

plt.rcParams.update({
    "font.family": "DejaVu Sans", "font.size": 9, "axes.edgecolor": INK2, "axes.labelcolor": INK,
    "xtick.color": INK2, "ytick.color": INK2, "axes.grid": True, "grid.color": GRIDC, "grid.linewidth": 0.6,
    "axes.spines.top": False, "axes.spines.right": False, "figure.facecolor": SURFACE, "axes.facecolor": SURFACE,
    "savefig.facecolor": SURFACE, "legend.frameon": False, "axes.titlesize": 10, "axes.titleweight": "bold",
})


def load(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        for k in ("W", "H", "threads", "steps"):
            r[k] = int(r[k])
        for k in ("median_ms", "min_ms", "max_ms", "stddev_ms", "mcells_per_s", "ns_per_cell_step", "est_GBps",
                  "speedup_vs_scalar", "speedup_vs_1thread_same_kernels", "parallel_efficiency"):
            r[k] = float(r[k])
    return rows


def grids_of(rows):
    return sorted({r["grid"] for r in rows}, key=lambda g: int(g.split("x")[0]))


def pick(rows, grid, variant, threads=None, measure="kernel"):
    for r in rows:
        if r["grid"] == grid and r["variant"] == variant and r["measure"] == measure and \
                (threads is None or r["threads"] == threads):
            return r
    return None


def save(fig, out_dir, name):
    path = os.path.join(out_dir, name)
    fig.savefig(path, dpi=200, bbox_inches="tight")
    plt.close(fig)
    print("  ", path)


def fig_speedup_variants(rows, out_dir, p_best):
    grids = grids_of(rows)
    variants = [("A_scalar", 1, "A\nskalar"), ("A2_scalar_branchless", 1, "A2\nbez grananja"),
                ("B_auto", 1, "B\nauto-vek."), ("C_omp_simd", 1, "C\nomp simd"), ("D_avx2", 1, "D\nAVX2"),
                ("E_omp", p_best, f"E\nomp\np={p_best}"), ("F_omp_simd", p_best, f"F\nomp+simd\np={p_best}"),
                ("G_omp_avx2", p_best, f"G\nomp+AVX2\np={p_best}")]
    variants = [v for v in variants if pick(rows, grids[0], v[0], v[1])]
    fig, ax = plt.subplots(figsize=(7.2, 3.4))
    n = len(grids)
    width = 0.8 / n
    for gi, g in enumerate(grids):
        xs, ys = [], []
        for vi, (vid, p, _) in enumerate(variants):
            r = pick(rows, g, vid, p)
            xs.append(vi + (gi - (n - 1) / 2) * width)
            ys.append(r["speedup_vs_scalar"] if r else 0)
        ax.bar(xs, ys, width=width * 0.92, color=GRID_RAMP[gi % 4], label=g, edgecolor=SURFACE, linewidth=0.8)
        if g == grids[-1]:
            for x, y in zip(xs, ys):
                ax.text(x, y + 0.25, f"{y:.1f}", ha="center", va="bottom", fontsize=6.5, color=INK2)
    ax.set_xticks(range(len(variants)))
    ax.set_xticklabels([v[2] for v in variants], fontsize=7.5)
    ax.set_ylabel("Ubrzanje u odnosu na A (×)")
    ax.axhline(1, color=INK2, linewidth=0.8)
    ax.legend(title="Mreža", ncol=4, loc="upper left", fontsize=8, title_fontsize=8)
    ax.set_title("Ubrzanje numeričkog jezgra u odnosu na A (medijan od 10 mjerenja)", loc="left")
    save(fig, out_dir, "perf_speedup_variants.png")


def fig_throughput(rows, out_dir, p_best):
    grids = grids_of(rows)
    variants = [("A_scalar", 1, "A"), ("A2_scalar_branchless", 1, "A2"), ("B_auto", 1, "B"), ("C_omp_simd", 1, "C"),
                ("D_avx2", 1, "D"), ("E_omp", p_best, "E"), ("F_omp_simd", p_best, "F"), ("G_omp_avx2", p_best, "G")]
    variants = [v for v in variants if pick(rows, grids[0], v[0], v[1])]
    fig, ax = plt.subplots(figsize=(7.2, 3.0))
    n = len(grids)
    width = 0.8 / n
    for gi, g in enumerate(grids):
        xs = [vi + (gi - (n - 1) / 2) * width for vi in range(len(variants))]
        ys = [pick(rows, g, v[0], v[1])["mcells_per_s"] for v in variants]
        ax.bar(xs, ys, width=width * 0.92, color=GRID_RAMP[gi % 4], label=g, edgecolor=SURFACE, linewidth=0.8)
    ax.set_xticks(range(len(variants)))
    ax.set_xticklabels([v[2] + (f" (p={v[1]})" if v[1] > 1 else "") for v in variants], fontsize=8)
    ax.set_ylabel("Mćelija·korak / s")
    ax.legend(title="Mreža", ncol=4, loc="upper left", fontsize=8, title_fontsize=8)
    ax.set_title("Propusnost: obrađene ćelije po sekundi (jedan vremenski korak = 3 faze)", loc="left")
    save(fig, out_dir, "perf_throughput.png")


def fig_thread_scaling(rows, out_dir, physical_cores):
    grids = grids_of(rows)
    show = [g for g in (grids[1] if len(grids) > 1 else grids[0], grids[-1])]
    fams = [("E_omp", "E: skalarno"), ("F_omp_simd", "F: omp simd"), ("G_omp_avx2", "G: AVX2")]
    fig, axes = plt.subplots(2, len(show), figsize=(7.2, 5.0), sharex=True)
    for ci, g in enumerate(show):
        ax_s, ax_e = axes[0][ci], axes[1][ci]
        threads = sorted({r["threads"] for r in rows if r["grid"] == g and r["variant"] == "E_omp" and r["measure"] == "kernel"})
        for k, (vid, lab) in enumerate(fams):
            pts = [pick(rows, g, vid, t) for t in threads]
            pts = [p for p in pts if p]
            ts = [p["threads"] for p in pts]
            sp = [p["speedup_vs_1thread_same_kernels"] for p in pts]
            ef = [p["parallel_efficiency"] for p in pts]
            ax_s.plot(ts, sp, color=SERIES[k], marker=MARKERS[k], markersize=5, linewidth=2, label=lab)
            ax_e.plot(ts, ef, color=SERIES[k], marker=MARKERS[k], markersize=5, linewidth=2, label=lab)
        ideal = [min(t, physical_cores) for t in threads]
        ax_s.plot(threads, ideal, color=INK2, linestyle="--", linewidth=1, label="idealno (ograničeno brojem jezgri)")
        for ax in (ax_s, ax_e):
            ax.axvspan(physical_cores * 2 ** 0.5, max(threads) * 1.2, color="#f0efec", zorder=0)
            ax.set_xscale("log", base=2)
            ax.set_xticks(threads)
            ax.set_xticklabels([str(t) for t in threads])
        ax_s.text(physical_cores * 1.5, 0.15, "više dretvi\nnego jezgri", fontsize=7, color=INK2, va="bottom")
        ax_s.set_title(f"Mreža {g}", loc="left")
        ax_s.set_ylim(0, max(physical_cores + 0.5, 2.5))
        ax_e.set_ylim(0, 1.2)
        ax_e.axhline(1.0, color=INK2, linewidth=0.8, linestyle=":")
        ax_e.set_xlabel("Broj OpenMP dretvi p")
        if ci == 0:
            ax_s.set_ylabel("S(p) = T(1) / T(p)")
            ax_e.set_ylabel("E(p) = S(p) / p")
    handles, labels = axes[0][0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=4, fontsize=7.5, bbox_to_anchor=(0.5, -0.02))
    fig.suptitle("Skaliranje sa brojem dretvi (T(1) = serijska varijanta sa istim kernelima: A, C, D)",
                 x=0.01, ha="left", fontsize=10, fontweight="bold")
    fig.tight_layout(rect=(0, 0.05, 1, 1))
    save(fig, out_dir, "perf_thread_scaling.png")


def fig_bandwidth(rows, stream_path, out_dir, p_best):
    grids = grids_of(rows)
    variants = [("A_scalar", 1, "A"), ("B_auto", 1, "B"), ("D_avx2", 1, "D"), ("E_omp", p_best, f"E p={p_best}"),
                ("G_omp_avx2", p_best, f"G p={p_best}")]
    fig, ax = plt.subplots(figsize=(7.2, 3.0))
    n = len(grids)
    width = 0.8 / n
    for gi, g in enumerate(grids):
        xs = [vi + (gi - (n - 1) / 2) * width for vi in range(len(variants))]
        ys = [pick(rows, g, v[0], v[1])["est_GBps"] for v in variants]
        ax.bar(xs, ys, width=width * 0.92, color=GRID_RAMP[gi % 4], label=g, edgecolor=SURFACE, linewidth=0.8)
    refs = []
    for pth, kind, style in ((stream_path, "RAM (384 MB)", "--"), (stream_path.replace("stream.csv", "stream_l3.csv"), "keš (36 MB)", ":")):
        if os.path.exists(pth):
            with open(pth) as f:
                st = {int(r["threads"]): float(r["GBps_median"]) for r in csv.DictReader(f)}
            for t in (1, p_best):
                if t in st:
                    refs.append((st[t], f"triad {kind}, p={t}: {st[t]:.0f} GB/s", style))
    for val, lab, style in refs:
        ax.axhline(val, color=INK2, linestyle=style, linewidth=0.9)
        ax.text(-0.45, val + 0.6, lab, fontsize=6.5, color=INK2, va="bottom", ha="left")
    ax.set_xticks(range(len(variants)))
    ax.set_xticklabels([v[2] for v in variants])
    ax.set_ylabel("GB/s (model: 112 B po ćeliji i koraku)")
    ax.legend(title="Mreža", ncol=4, loc="upper center", bbox_to_anchor=(0.5, -0.12), fontsize=8, title_fontsize=8)
    ax.set_title("Efektivna propusnost jezgra naspram STREAM triad mjerenja", loc="left")
    save(fig, out_dir, "perf_bandwidth.png")


def fig_e2e(rows, out_dir, p_best):
    grids = grids_of(rows)
    fig, ax = plt.subplots(figsize=(7.2, 2.8))
    variants = [("B_auto", 1, "B"), ("D_avx2", 1, "D"), ("E_omp", p_best, "E"), ("G_omp_avx2", p_best, "G")]
    labels, kern, e2e = [], [], []
    for g in grids:
        for vid, p, lab in variants:
            k = pick(rows, g, vid, p, "kernel")
            e = pick(rows, g, vid, p, "e2e")
            if k and e:
                labels.append(f"{lab}\n{g.split('x')[0]}")
                kern.append(k["speedup_vs_scalar"])
                e2e.append(e["speedup_vs_scalar"])
    xs = range(len(labels))
    ax.bar([x - 0.2 for x in xs], kern, width=0.38, color=SERIES[0], label="samo numeričko jezgro")
    ax.bar([x + 0.2 for x in xs], e2e, width=0.38, color=SERIES[1], label="kompletna simulacija (alokacija + teren + koraci)")
    ax.set_xticks(list(xs))
    ax.set_xticklabels(labels, fontsize=6.5)
    ax.set_ylabel("Ubrzanje u odnosu na A (×)")
    ax.legend(loc="upper left", fontsize=7.5)
    ax.set_title("Jezgro i kompletna simulacija (10× više koraka + serijsko generisanje terena)", loc="left")
    save(fig, out_dir, "perf_kernel_vs_e2e.png")


def fig_branch_ablation(rows, out_dir):
    grids = grids_of(rows)
    fig, ax = plt.subplots(figsize=(7.2, 2.8))
    variants = [("A_scalar", "A: skalarno, sa grananjem"), ("A2_scalar_branchless", "A2: skalarno, bez grananja"),
                ("B_auto", "B: isti izvor kao A2, vektorizovano")]
    n = len(variants)
    width = 0.8 / n
    for vi, (vid, lab) in enumerate(variants):
        xs = [gi + (vi - (n - 1) / 2) * width for gi in range(len(grids))]
        ys = [pick(rows, g, vid, 1)["ns_per_cell_step"] for g in grids]
        ax.bar(xs, ys, width=width * 0.92, color=SERIES[vi], label=lab, edgecolor=SURFACE, linewidth=0.8)
        for x, y in zip(xs, ys):
            ax.text(x, y + 0.4, f"{y:.1f}", ha="center", fontsize=6.5, color=INK2)
    ax.set_xticks(range(len(grids)))
    ax.set_xticklabels(grids)
    ax.set_ylabel("ns po ćeliji i koraku")
    ax.legend(loc="upper right", fontsize=7.5)
    ax.set_title("Razdvajanje efekata: uklanjanje grananja (A→A2) i SIMD (A2→B)", loc="left")
    save(fig, out_dir, "perf_branch_simd_decomposition.png")


def main():
    res = sys.argv[1] if len(sys.argv) > 1 else "results"
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(res, "figures")
    os.makedirs(out, exist_ok=True)
    rows = load(os.path.join(res, "benchmarks.csv"))
    physical = 2
    try:
        with open(os.path.join(res, "system_info.txt")) as f:
            for line in f:
                if line.startswith("Core(s) per socket"):
                    physical = int(line.split(":")[1])
    except OSError:
        pass
    print("Grafikoni:")
    fig_speedup_variants(rows, out, physical)
    fig_throughput(rows, out, physical)
    fig_thread_scaling(rows, out, physical)
    fig_bandwidth(rows, os.path.join(res, "stream.csv"), out, physical)
    fig_e2e(rows, out, physical)
    fig_branch_ablation(rows, out)


if __name__ == "__main__":
    main()
