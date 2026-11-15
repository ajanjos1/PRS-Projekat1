#!/usr/bin/env python3
"""Grafovi prije/poslije paralelizacije. Brojevi su izmjereni sa bench/bench_faze.cpp na Apple M1 Pro
(6 brzih + 2 spora jezgra), RelWithDebInfo -O3 -g, mreza 736x736, 50 koraka, najbolje od 3 ponavljanja.

Pokretanje:  python3 bench/grafovi.py
Izlaz:       bench/rezultati/*.png
"""
import os
import sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rezultati')
os.makedirs(OUT, exist_ok=True)
plt.rcParams.update({'font.family': 'Arial', 'font.size': 10})

FAZE = ['kiša', 'protoci\n(cijevi)', 'voda i\nbrzina', 'erozija i\ntaloženje', 'transport\nsedimenta']
PRIJE = [0.24, 14.87, 9.10, 9.20, 6.33]      # original, serijski
POSLIJE1 = [0.23, 12.36, 8.80, 9.39, 3.35]   # optimizovano + SIMD, 1 nit
POSLIJE6 = [0.24, 2.33, 1.79, 1.84, 1.05]    # optimizovano + SIMD, 6 niti (guided)
NITI = [1, 2, 4, 6, 8]
VRIJEME = [34.14, 17.36, 10.42, 7.31, 7.44]  # ns/ćeliji/koraku (SIMD + guided)

ZELENA, PLAVA, SIVA = '#2F855A', '#2B6CB0', '#A0AEC0'


def po_fazama():
    fig, ax = plt.subplots(figsize=(9, 4.6))
    x = np.arange(len(FAZE))
    w = 0.27
    for k, (vals, lab, boja) in enumerate([
            (PRIJE, f'prije: serijski ({sum(PRIJE):.1f} ns)', SIVA),
            (POSLIJE1, f'poslije, 1 nit ({sum(POSLIJE1):.1f} ns)', PLAVA),
            (POSLIJE6, f'poslije, 6 niti ({sum(POSLIJE6):.1f} ns)', ZELENA)]):
        b = ax.bar(x + (k - 1) * w, vals, w, label=lab, color=boja)
        ax.bar_label(b, fmt='%.1f', fontsize=7.5, padding=1)
    ax.set_xticks(x); ax.set_xticklabels(FAZE)
    ax.set_ylabel('ns po ćeliji po koraku')
    ax.set_title('Vrijeme po fazama jednog koraka simulacije (736 × 736, Apple M1 Pro)', fontsize=11)
    ax.legend(frameon=False)
    ax.spines[['top', 'right']].set_visible(False)
    ax.grid(axis='y', alpha=.25)
    fig.tight_layout(); fig.savefig(f'{OUT}/faze_prije_poslije.png', dpi=170); plt.close(fig)


def skaliranje():
    S = [VRIJEME[0] / v for v in VRIJEME]
    E = [s / p for s, p in zip(S, NITI)]
    fig, ax = plt.subplots(1, 2, figsize=(9.5, 4))
    ax[0].plot(NITI, NITI, '--', color=SIVA, label='idealno')
    ax[0].plot(NITI, S, 'o-', color=ZELENA, label='izmjereno')
    for p, s in zip(NITI, S):
        ax[0].annotate(f'{s:.2f}×', (p, s), textcoords='offset points', xytext=(6, -10), fontsize=8)
    ax[0].set_xlabel('broj niti p'); ax[0].set_ylabel('ubrzanje S(p)')
    ax[0].set_title('Ubrzanje', fontsize=11); ax[0].legend(frameon=False)

    ax[1].axhline(1.0, ls='--', color=SIVA)
    ax[1].plot(NITI, E, 'o-', color=PLAVA)
    for p, e in zip(NITI, E):
        ax[1].annotate(f'{e:.2f}', (p, e), textcoords='offset points', xytext=(6, -11), fontsize=8)
    ax[1].set_xlabel('broj niti p'); ax[1].set_ylabel('efikasnost E(p) = S(p)/p')
    ax[1].set_ylim(0, 1.15); ax[1].set_title('Efikasnost', fontsize=11)

    for a in ax:
        a.set_xticks(NITI); a.spines[['top', 'right']].set_visible(False); a.grid(alpha=.25)
    # 6 brzih jezgara: iznad toga rade spora jezgra, pa efikasnost naglo pada
    for a in ax:
        a.axvline(6, color='#C53030', lw=1, alpha=.5)
        a.annotate('6 brzih\njezgara', (6, a.get_ylim()[1] * 0.12), color='#C53030', fontsize=7.5,
                   ha='left', va='bottom')
    fig.suptitle('Skaliranje po broju niti (736 × 736, Apple M1 Pro: 6 brzih + 2 spora jezgra)', fontsize=11)
    fig.tight_layout(); fig.savefig(f'{OUT}/skaliranje.png', dpi=170); plt.close(fig)


def ukupno():
    fig, ax = plt.subplots(figsize=(7.2, 3.4))
    imena = ['prije\n(serijski)', 'poslije\n1 nit', 'poslije\n2 niti', 'poslije\n4 niti', 'poslije\n6 niti']
    vals = [sum(PRIJE), VRIJEME[0], VRIJEME[1], VRIJEME[2], VRIJEME[3]]
    boje = [SIVA, PLAVA, PLAVA, PLAVA, ZELENA]
    b = ax.barh(imena[::-1], vals[::-1], color=boje[::-1])
    for r, v in zip(b, vals[::-1]):
        ax.text(v + 0.5, r.get_y() + r.get_height() / 2, f'{v:.1f} ns   ({vals[0] / v:.2f}× brže)',
                va='center', fontsize=9)
    ax.set_xlabel('ns po ćeliji po koraku (manje je bolje)')
    ax.set_xlim(0, max(vals) * 1.42)
    ax.spines[['top', 'right']].set_visible(False); ax.grid(axis='x', alpha=.25)
    ax.set_title('Ukupno vrijeme jednog koraka: prije i poslije', fontsize=11)
    fig.tight_layout(); fig.savefig(f'{OUT}/ukupno_prije_poslije.png', dpi=170); plt.close(fig)


def sve():
    po_fazama(); skaliranje(); ukupno(); traka_niti(); ijk_ikj(); instrumenti(); iteracije()
    print(f'Grafovi: {OUT}')


# ---------------------------------------------------------------- traka zeleno/crveno po nitima
# Podaci iz bench/profil_niti.cpp (faza protoka, 50 koraka, 6 niti, mreza 736x736).
# zeleno = nit racuna, crveno = nit ceka na barijeri dok posljednja ne zavrsi.
STATIC_RAD = [58.34, 58.60, 57.87, 57.83, 57.72, 60.12]
STATIC_CEK = [5.20, 3.36, 4.81, 3.72, 4.25, 0.52]
# guided: mjerenje arhivirano u parametri/izlazi/03 profil_niti.txt (ponovljivo: isti brojevi i danas)
GUIDED_RAD = [59.54, 58.57, 58.75, 58.01, 58.48, 57.06]
GUIDED_CEK = [0.15, 0.21, 0.19, 0.21, 0.20, 0.25]
CRVENA = '#C53030'
KORAKA_STATIC = 50      # kroz koliko koraka su izmjereni gornji static podaci


def ucitaj_csv():
    """Ako postoji bench/rezultati/profil_niti.csv (iz zadnjeg pokretanja profil_niti),
    koristi te brojeve umjesto zapisanih. Tako slika uvijek odgovara onome sto je upravo izmjereno."""
    put = f'{OUT}/profil_niti.csv'
    if not os.path.exists(put):
        return None
    rad, cek = [], []
    with open(put, encoding='utf-8') as f:
        zaglavlje = next(f, '')
        koraka = next((d.split('=')[1] for d in zaglavlje.strip().split(',') if d.startswith('koraka=')), None)
        if koraka is not None and int(koraka) != KORAKA_STATIC:
            print(f'  (profil_niti.csv je mjeren kroz {koraka} koraka, a zapisani static podaci kroz '
                  f'{KORAKA_STATIC} – koriste se zapisani, da se ne porede razlicite stvari)')
            return None
        for red in f:
            dio = red.strip().split(',')
            if len(dio) == 3:
                rad.append(float(dio[1]))
                cek.append(float(dio[2]))
    if not rad:
        return None
    # Zapisani static podaci su za 6 niti; ako je mjereno s drugim brojem niti, poredjenje ne bi valjalo.
    if len(rad) != len(STATIC_RAD):
        print(f'  (profil_niti.csv ima {len(rad)} niti, a zapisani static podaci {len(STATIC_RAD)} – '
              f'koriste se zapisani, da se ne porede razlicite stvari)')
        return None
    return rad, cek


def traka_niti():
    global GUIDED_RAD, GUIDED_CEK
    # Zadano se crtaju ZAPISANI brojevi (gore u datoteci), da slika ostane ista od pokretanja do pokretanja.
    # Sa `python3 bench/grafovi.py --svjeze` crtaju se brojevi iz zadnjeg pokretanja profil_niti.
    if '--svjeze' in sys.argv:
        izmjereno = ucitaj_csv()
        if izmjereno:
            GUIDED_RAD, GUIDED_CEK = izmjereno
            print('  (traka_niti: koriste se svjezi brojevi iz profil_niti.csv)')
    fig, ax = plt.subplots(2, 1, figsize=(9, 5), sharex=True)
    # Granica ose se racuna iz podataka (profil_niti prima broj koraka, pa trake mogu biti duze),
    # uz prostor za natpis „ceka X ms“ s desne strane.
    gornja = 1.30 * max(r + c for r, c in zip(STATIC_RAD + GUIDED_RAD, STATIC_CEK + GUIDED_CEK))
    for a, (rad, cek, ime) in zip(ax, [(STATIC_RAD, STATIC_CEK, 'schedule(static)'),
                                       (GUIDED_RAD, GUIDED_CEK, 'schedule(guided)')]):
        y = np.arange(len(rad))
        a.barh(y, rad, color=ZELENA, label='rad (zeleno)')
        a.barh(y, cek, left=rad, color=CRVENA, label='čekanje na barijeri (crveno)')
        for i, (r, c) in enumerate(zip(rad, cek)):
            a.text(r + c + 0.6, i, f'čeka {c:.2f} ms', va='center', fontsize=8, color=CRVENA)
        a.set_yticks(y); a.set_yticklabels([f'nit {i}' for i in y]); a.invert_yaxis()
        isk = 100 * sum(rad) / (sum(rad) + sum(cek))
        a.set_title(f'{ime} – iskorištenost niti {isk:.1f} %  (čekanje ukupno {sum(cek):.1f} ms)', fontsize=10.5)
        a.spines[['top', 'right']].set_visible(False); a.grid(axis='x', alpha=.25)
        a.set_xlim(0, gornja)
    ax[1].set_xlabel('vrijeme [ms]')
    h, l = ax[0].get_legend_handles_labels()
    fig.legend(h, l, frameon=False, fontsize=9, ncol=2, loc='lower center', bbox_to_anchor=(0.5, -0.02))
    fig.suptitle('Faza protoka: koliko svaka nit radi, a koliko čeka (736 × 736, 6 niti)', fontsize=11)
    fig.tight_layout(rect=[0, 0.05, 1, 1]); fig.savefig(f'{OUT}/traka_niti.png', dpi=170, bbox_inches='tight'); plt.close(fig)





# ---------------------------------------------------------------- IJK vs IKJ (uticaj redoslijeda petlji)
# Podaci iz bench/ijk_vs_ikj.cpp: matrica 1024x1024, double, najbolje od 3, 1 nit.
IJK_IMENA = ['jki', 'kji', 'jik', 'ijk', 'kij', 'ikj']
IJK_VRIJEME = [7.116, 7.064, 1.385, 1.344, 0.225, 0.168]
IJK_UNUTRA = ['i', 'i', 'k', 'k', 'j', 'j']
BOJE_UNUTRA = {'i': '#C53030', 'k': '#D69E2E', 'j': '#2F855A'}


def ijk_ikj():
    fig, ax = plt.subplots(figsize=(9, 4.2))
    boje = [BOJE_UNUTRA[u] for u in IJK_UNUTRA]
    b = ax.barh(IJK_IMENA, IJK_VRIJEME, color=boje)
    for r, (t, u) in zip(b, zip(IJK_VRIJEME, IJK_UNUTRA)):
        gf = 2 * 1024 ** 3 / t / 1e9
        ax.text(t + 0.12, r.get_y() + r.get_height() / 2,
                f'{t:.3f} s   {gf:.1f} GFLOP/s   ({t / min(IJK_VRIJEME):.0f}× sporije)',
                va='center', fontsize=8.5)
    ax.set_xlabel('vrijeme [s] – manje je bolje (matrica 1024 × 1024, 1 nit)')
    ax.set_xlim(0, max(IJK_VRIJEME) * 1.5)
    ax.spines[['top', 'right']].set_visible(False)
    ax.grid(axis='x', alpha=.25)
    ax.set_title('Isti posao, šest redoslijeda petlji: C[i][j] += A[i][k] · B[k][j]', fontsize=11)
    from matplotlib.patches import Patch
    ax.legend(handles=[Patch(color=BOJE_UNUTRA['j'], label='unutrašnja petlja j – B i C susjedni (vektorizuje se)'),
                       Patch(color=BOJE_UNUTRA['k'], label='unutrašnja petlja k – B skače kroz memoriju'),
                       Patch(color=BOJE_UNUTRA['i'], label='unutrašnja petlja i – A i C oba skaču')],
              frameon=False, fontsize=8.5, loc='lower right')
    fig.tight_layout(); fig.savefig(f'{OUT}/ijk_vs_ikj.png', dpi=170); plt.close(fig)


# ------------------------------------------------- zeleno/crveno iz Xcode Instrumentsa (brojaci M1)
# Sablon "CPU Counters", nacin "CPU Bottlenecks". Mreza 736x736, --rain 100 --hours 0.3 (609 koraka).
# Izvoz snimka daje cetiri broja po uzorku bez imena kolona; koja je koja utvrdjeno je kalibracijom,
# snimanjem programa poznatog ponasanja (lanac dijeljenja, slucajno grananje, nezavisni FMA-i).
INSTR_IMENA = ['Korisno – instrukcije\nkoje nisu čekale', 'Obrada instrukcija\n(izvršne jedinice)',
               'Promašena\nspekulacija', 'Dostava instrukcija\n(front-end)']
INSTR_BOJE = ['#2F855A', '#C53030', '#9B2C2C', '#E53E3E']
INSTR_1 = [45.9, 38.3, 1.8, 14.1]
INSTR_6 = [42.5, 41.1, 2.8, 13.6]


def instrumenti():
    fig, ax = plt.subplots(figsize=(9, 2.9))
    redovi = [('6 niti\n2,59 s', INSTR_6), ('1 nit\n10,08 s', INSTR_1)]
    for y, (ime, vals) in enumerate(redovi):
        lijevo = 0.0
        for v, boja in zip(vals, INSTR_BOJE):
            ax.barh(y, v, left=lijevo, color=boja, height=0.5,
                    edgecolor='white', linewidth=0.8)
            if v >= 4:
                ax.text(lijevo + v / 2, y, f'{v:.1f}%', ha='center', va='center',
                        color='white', fontsize=9, fontweight='bold')
            lijevo += v
    ax.set_yticks(range(len(redovi))); ax.set_yticklabels([r[0] for r in redovi], fontsize=9)
    ax.set_xlim(0, 100); ax.set_xlabel('udio ciklusa procesora [%]')
    ax.spines[['top', 'right', 'left']].set_visible(False)
    ax.set_title('Na šta procesor troši cikluse (Xcode Instruments, stvarni brojači M1 Pro)', fontsize=11)
    from matplotlib.patches import Patch
    fig.legend(handles=[Patch(color=b, label=n.replace('\n', ' ')) for n, b in zip(INSTR_IMENA, INSTR_BOJE)],
               frameon=False, fontsize=8, ncol=2, loc='lower center', bbox_to_anchor=(0.5, -0.04))
    fig.tight_layout(rect=(0, 0.17, 1, 1))
    fig.savefig(f'{OUT}/instruments_zeleno_crveno.png', dpi=170); plt.close(fig)


# ------------------------------------------- put kroz iteracije (serijska optimizacija faze protoka)
# Svaka izmjena je mjerena posebno sa bench_faze (736x736, 1 nit); dvije ideje su mjerene pa odbacene.
ITER_IMENA = ['polazno\nstanje', 'formula trenja\n(1 dijeljenje)', 'rub izdvojen\niz petlje',
              'bez grananja\n+ omp simd']
ITER_NS = [14.87, 14.01, 14.01, 12.36]
ITER_NAPOMENA = ['', '-5,8 %', '0 %', '-11,8 %']
ODBACENO = [('recipročni kubni korijen\nbez dijeljenja', '13,9 → 16,4 ns  (sporije)'),
            ('obrada po blokovima\nzbog keša', 'vrijeme po ćeliji isto\nod 128² do 2048²')]


def iteracije():
    fig, (ax, bx) = plt.subplots(1, 2, figsize=(10.5, 4.4), gridspec_kw={'width_ratios': [2.15, 1]})

    x = np.arange(len(ITER_NS))
    boje = [SIVA] + [PLAVA] * 2 + [ZELENA]
    b = ax.bar(x, ITER_NS, 0.62, color=boje)
    for r, v, nap in zip(b, ITER_NS, ITER_NAPOMENA):
        ax.text(r.get_x() + r.get_width() / 2, v + 0.22, f'{v:.2f}'.replace('.', ',') + ' ns',
                ha='center', fontsize=9.5, fontweight='bold')
        if nap:
            ax.text(r.get_x() + r.get_width() / 2, v - 0.9, nap, ha='center', fontsize=8.5,
                    color='white', fontweight='bold')
    # stepenasta linija koja spaja vrhove, da se vidi putanja
    for i in range(len(ITER_NS) - 1):
        ax.plot([x[i], x[i] + 0.5, x[i] + 0.5, x[i + 1]],
                [ITER_NS[i]] * 2 + [ITER_NS[i + 1]] * 2, color='#4A5568', lw=1.1, ls='--', zorder=0)
    ax.set_xticks(x); ax.set_xticklabels(ITER_IMENA, fontsize=9)
    ax.set_ylabel('faza protoka [ns po ćeliji po koraku]')
    ax.set_ylim(0, max(ITER_NS) * 1.18)
    ax.spines[['top', 'right']].set_visible(False); ax.grid(axis='y', alpha=.25)
    ax.set_title('Serijska optimizacija korak po korak (736 × 736, jedna nit)', fontsize=11)

    bx.axis('off')
    bx.set_title('Izmjereno pa odbačeno', fontsize=11, color=CRVENA)
    for k, (ime, mjera) in enumerate(ODBACENO):
        y = 0.86 - k * 0.40
        bx.add_patch(plt.Rectangle((0.02, y - 0.26), 0.96, 0.30, transform=bx.transAxes,
                                   facecolor='#FFF5F5', edgecolor=CRVENA, lw=1.1))
        bx.text(0.07, y - 0.03, ime, transform=bx.transAxes, fontsize=9.5, fontweight='bold',
                va='center', color=CRVENA)
        bx.text(0.07, y - 0.185, mjera, transform=bx.transAxes, fontsize=8.8, va='center', color='#2D3748')
    bx.text(0.02, 0.03, 'Odluku je u oba slučaja donijelo mjerenje,\nne pretpostavka.',
            transform=bx.transAxes, fontsize=8.8, style='italic', color='#4A5568', va='bottom')

    fig.tight_layout(); fig.savefig(f'{OUT}/iteracije_serijski.png', dpi=170); plt.close(fig)


if __name__ == '__main__':
    sve()
