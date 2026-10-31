# SPDX-License-Identifier: AGPL-3.0-only
"""F2b: robustness of Gray-Scott patterns vs grid size with random multi-patch seeds.
For each (regime, L): 8 random initial conditions (2-6 random squares of (u,v)=(0.5,0.25), side 2..max(3,L/6),
+1% noise), 15000 slow steps (= 20 s at 750 Hz). Periodic BC. Reports fraction alive, wavelength, settle time."""
import os, json, time
import numpy as np
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
plt.rcParams["font.family"] = ["Noto Sans CJK SC", "DejaVu Sans"]
FIG = os.path.join(os.path.dirname(__file__), "../../reports/figures")
OUT = os.path.join(os.path.dirname(__file__), "../../experiments/results")
DU, DV = [0.25 * x for x in (2e-5 * (256 / 2.5) ** 2, 1e-5 * (256 / 2.5) ** 2)]
SLOW = 750.0
REG = {"mitosis (spots)": (0.0367, 0.0649), "coral (labyrinth)": (0.0545, 0.062),
       "solitons": (0.030, 0.062), "waves/chaos": (0.014, 0.045), "worms": (0.078, 0.061)}
lap = lambda a: np.roll(a, 1, -1) + np.roll(a, -1, -1) + np.roll(a, 1, -2) + np.roll(a, -1, -2) - 4 * a
def dom_wavelength(v):
    L = v.shape[-1]; P = np.abs(np.fft.fft2(v - v.mean())) ** 2
    f = np.fft.fftfreq(L); KX, KY = np.meshgrid(f, f); kr = np.hypot(KX, KY); P[0, 0] = 0
    i = np.unravel_index(P.argmax(), P.shape); return float(1 / kr[i])
NS, STEPS, REC = 8, 15000, 50
names = list(REG); res = {}; finals = {}
for L in (16,):
    B = len(names) * NS; rng = np.random.default_rng(L)
    F = np.repeat([REG[n][0] for n in names], NS)[:, None, None]; k = np.repeat([REG[n][1] for n in names], NS)[:, None, None]
    u = np.ones((B, L, L)); v = np.zeros((B, L, L))
    for b in range(B):
        for _ in range(rng.integers(2, 7)):
            s = rng.integers(2, max(3, L // 6) + 1); x, y = rng.integers(0, L, 2)
            ix = (np.arange(x, x + s) % L)[:, None]; iy = (np.arange(y, y + s) % L)[None, :]
            u[b, ix, iy] = 0.5; v[b, ix, iy] = 0.25
    u += 0.01 * rng.standard_normal(u.shape); v = np.clip(v + 0.01 * rng.standard_normal(v.shape), 0, None)
    hist = []; t0 = time.time()
    for n in range(STEPS):
        uvv = u * v * v
        u, v = u + DU * lap(u) - uvv + F * (1 - u), v + DV * lap(v) + uvv - (F + k) * v
        if n % REC == 0: hist.append(v.reshape(B, -1).std(1))
    hist = np.array(hist)
    for i, n in enumerate(names):
        al, lam, tset, chg = [], [], [], []
        for j in range(NS):
            b = i * NS + j; h = hist[:, b]; fin = h[-1]; alive = v[b].max() > 0.05; al.append(bool(alive))
            if alive:
                lam.append(dom_wavelength(v[b]))
                bad = np.where(np.abs(h - fin) > 0.1 * max(fin, 1e-9))[0]
                tset.append(((bad[-1] + 1) if len(bad) else 0) * REC)
                chg.append(float(np.abs(np.diff(h[-40:])).max() / max(fin, 1e-9)))  # late-time variability
            finals.setdefault((n, L), v[b].copy())
            if alive and not any(al[:-1]): finals[(n, L)] = v[b].copy()
        res.setdefault(n, {})[L] = {"alive_fraction": float(np.mean(al)),
            "wavelength_cells_median": float(np.median(lam)) if lam else None,
            "settle_steps_median": float(np.median(tset)) if tset else None,
            "settle_seconds_median_at_750Hz": float(np.median(tset)) / SLOW if tset else None,
            "settle_seconds_max_at_750Hz": float(np.max(tset)) / SLOW if tset else None,
            "late_relative_variability_median": float(np.median(chg)) if chg else None}
    print("L", L, round(time.time() - t0, 1), "s", flush=True)
json.dump(res, open(os.path.join(OUT, "f2b_grayscott_D025_L16.json"), "w"), indent=1)
fig, ax = plt.subplots(len(names), 1, figsize=(3, 2.6 * len(names))); ax = ax[:, None]
for i, n in enumerate(names):
    for j, L in enumerate((16,)):
        a = ax[i, j]; a.imshow(finals[(n, L)], cmap="magma", vmin=0, vmax=0.5); a.set_xticks([]); a.set_yticks([])
        d = res[n][L]; lam = d["wavelength_cells_median"]
        a.set_title(f"{n} L={L}\nalive {d['alive_fraction']*100:.0f}%" + (f", λ≈{lam:.1f}, t≈{d['settle_seconds_median_at_750Hz']:.1f}s" if lam else ""), fontsize=8)
fig.suptitle(f"Gray-Scott, 8 random seeds per cell, {STEPS} slow steps (= {STEPS/SLOW:.0f} s at 750 Hz)", fontsize=10)
fig.tight_layout(); fig.savefig(os.path.join(FIG, "f1b_grayscott_D025_L16.png"), dpi=110); plt.close(fig)
for n in names: print(n, res[n][16])
