# SPDX-License-Identifier: AGPL-3.0-only
"""Audit (f): Gray-Scott morphogen field at the proposal's slow rate (f_s/64 = 750 Hz).
F1 linear/Turing analysis of homogeneous states (lattice dispersion), wavelength vs grid size
F2 free GS simulations on 16/32/64 periodic grids for commonly used (F,k) sets
F3 nucleation test: acoustic source on u only, starting from (u,v) = (1,0)
F4 coupled fast-slow run: 16x16 ESAV lattice (48 kHz) <-> GS (750 Hz) with the proposal's maps
Grid units: dx = 1, one GS step (dt = 1) per slow tick. Du, Dv = Pearson (1993) values
2e-5, 1e-5 rescaled to a 256-cell / 2.5-length grid (0.2097, 0.1049)."""
import sys, os, json, time
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import numpy as np
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
plt.rcParams["font.family"] = ["Noto Sans CJK SC", "DejaVu Sans"]
FIG = os.path.join(os.path.dirname(__file__), "../../reports/figures")
OUT = os.path.join(os.path.dirname(__file__), "../../experiments/results")
DU, DV = 2e-5 * (256 / 2.5) ** 2, 1e-5 * (256 / 2.5) ** 2
SLOW = 750.0
# commonly quoted parameter sets (labels follow the Pearson/Munafo naming; not re-verified here)
REG = {"mitosis (spots)": (0.0367, 0.0649), "coral (labyrinth)": (0.0545, 0.062),
       "solitons": (0.030, 0.062), "waves/chaos": (0.014, 0.045), "worms": (0.078, 0.061)}
res = {"Du": DU, "Dv": DV, "slow_rate_hz": SLOW}

# ---------------- F1 linear analysis ----------------
def hom_states(F, k):
    s = [(1.0, 0.0)]
    disc = F * F - 4 * F * (F + k) ** 2
    if disc >= 0:
        for sg in (+1, -1):
            v = (F + sg * np.sqrt(disc)) / (2 * (F + k)); u = (F + k) / v; s.append((u, v))
    return s

def growth(F, k, u, v, kap):
    J = np.array([[-v * v - F, -2 * u * v], [v * v, 2 * u * v - (F + k)]])
    out = []
    for K in kap:
        out.append(np.linalg.eigvals(J - K * np.diag([DU, DV])).real.max())
    return np.array(out)

kk = np.linspace(1e-3, np.pi, 400); kap = 4 * np.sin(kk / 2) ** 2  # lattice Laplacian symbol along an axis
F1 = {}
for name, (F, k) in REG.items():
    info = []
    for (u, v) in hom_states(F, k):
        g = growth(F, k, u, v, kap); g0 = growth(F, k, u, v, [0.0])[0]
        i = g.argmax()
        info.append({"u": u, "v": v, "growth_k0": g0, "max_growth": g[i],
                     "k_star": kk[i], "lambda_star_cells": 2 * np.pi / kk[i],
                     "turing_unstable": bool(g0 < 0 and g[i] > 0)})
    F1[name] = info
res["F1_homogeneous"] = F1

# ---------------- GS simulator (periodic, batched) ----------------
def lap(a):
    return np.roll(a, 1, -1) + np.roll(a, -1, -1) + np.roll(a, 1, -2) + np.roll(a, -1, -2) - 4 * a

def gs_run(L, Fk, steps, seed=0, init="pearson", src=None, rec_every=50):
    B = len(Fk); F = np.array([f for f, _ in Fk])[:, None, None]; k = np.array([q for _, q in Fk])[:, None, None]
    rng = np.random.default_rng(seed)
    u = np.ones((B, L, L)); v = np.zeros((B, L, L))
    if init == "pearson":
        c = L // 2; r = max(1, L // 10)
        u[:, c - r:c + r, c - r:c + r] = 0.5; v[:, c - r:c + r, c - r:c + r] = 0.25
        u += 0.01 * rng.standard_normal(u.shape); v += 0.01 * rng.standard_normal(v.shape)
        v = np.clip(v, 0, None)
    hist = []
    for n in range(steps):
        uvv = u * v * v
        du = DU * lap(u) - uvv + F * (1 - u); dv = DV * lap(v) + uvv - (F + k) * v
        if src is not None: du = du + src
        u = u + du; v = v + dv
        if n % rec_every == 0: hist.append(v.reshape(B, -1).std(1))
    return u, v, np.array(hist)

def dom_wavelength(v):
    L = v.shape[-1]; P = np.abs(np.fft.fft2(v - v.mean())) ** 2
    f = np.fft.fftfreq(L); KX, KY = np.meshgrid(f, f); kr = np.hypot(KX, KY)
    P[0, 0] = 0; i = np.unravel_index(P.argmax(), P.shape)
    return float(1 / kr[i]) if kr[i] > 0 else float("inf")

# ---------------- F2 free simulations ----------------
STEPS = int(os.environ.get("GS_STEPS", 20000)); REC = 50
names = list(REG); Fk = [REG[n] for n in names]
F2 = {}; finals = {}
for L in (16, 32, 64):
    t0 = time.time(); u, v, hist = gs_run(L, Fk, STEPS, rec_every=REC)
    for b, n in enumerate(names):
        h = hist[:, b]; fin = h[-1]
        alive = bool(v[b].max() > 0.05)
        t90 = None
        if alive and fin > 1e-3:
            idx = np.where(np.abs(h - fin) <= 0.1 * fin)[0]
            # first time after which it stays within 10% of the final std
            ok = np.ones(len(h), bool); ok[:] = False
            for j in range(len(h) - 1, -1, -1):
                if abs(h[j] - fin) > 0.1 * fin: break
                ok[j] = True
            j0 = int(np.argmax(ok)) if ok.any() else len(h) - 1
            t90 = j0 * REC
        F2.setdefault(n, {})[L] = {"pattern_alive": alive, "v_std_final": float(fin), "v_max": float(v[b].max()),
            "dominant_wavelength_cells": dom_wavelength(v[b]) if alive else None,
            "steps_to_settle_10pct": t90, "seconds_at_750Hz": (t90 / SLOW if t90 is not None else None)}
        finals[(n, L)] = v[b]
    print("F2 L", L, "done", round(time.time() - t0, 1), "s", flush=True)
res["F2_free"] = F2

fig, ax = plt.subplots(len(names), 3, figsize=(8, 2.6 * len(names)))
for i, n in enumerate(names):
    for j, L in enumerate((16, 32, 64)):
        a = ax[i, j]; a.imshow(finals[(n, L)], cmap="magma", vmin=0, vmax=0.5); a.set_xticks([]); a.set_yticks([])
        d = F2[n][L]
        a.set_title(f"{n} L={L}\n" + ("dead (v→0)" if not d["pattern_alive"] else f"λ≈{d['dominant_wavelength_cells']:.1f}, t≈{d['seconds_at_750Hz']:.1f}s"), fontsize=8)
fig.suptitle(f"Gray-Scott v after {STEPS} slow steps (= {STEPS/SLOW:.1f} s at 750 Hz), periodic", fontsize=10)
fig.tight_layout(); fig.savefig(os.path.join(FIG, "f1_grayscott_regimes_vs_size.png"), dpi=110); plt.close(fig)

# ---------------- F3 nucleation from (1,0) with u-only source ----------------
L = 16; S3 = 6000
rng = np.random.default_rng(3)
Hfield = rng.random((L, L)) ** 4                         # "acoustic energy" pattern, strongly non-uniform
src = 0.01 * np.tanh(5 * Hfield)                         # kappa_ac * tanh(lambda_E H)
u, v, _ = gs_run(L, [REG["mitosis (spots)"]], S3, init="uniform", src=src[None])
F3 = {"u_only_source_from_(1,0)": {"v_max_after": float(v.max()), "u_max_after": float(u.max()),
      "note": "v = 0 is an invariant set of the v-equation; a u-source can never create v"}}
# seed threshold: 3x3 v patch of amplitude a on top of (1,0), with the source on
thr = []
for a in np.logspace(-4, 0, 17):
    u0 = np.ones((1, L, L)); v0 = np.zeros((1, L, L)); v0[0, 7:10, 7:10] = a
    uu, vv = u0, v0
    F, k = REG["mitosis (spots)"]
    for n in range(S3):
        uvv = uu * vv * vv
        uu = uu + DU * lap(uu) - uvv + F * (1 - uu) + src[None]
        vv = vv + DV * lap(vv) + uvv - (F + k) * vv
    thr.append({"seed_amp": float(a), "v_max_final": float(vv.max())})
F3["v_seed_threshold_scan"] = thr
res["F3_nucleation"] = F3
print("F3 done", flush=True)

json.dump(res, open(os.path.join(OUT, "f_grayscott.json"), "w"), indent=1, default=float)
print(json.dumps(F1, indent=1, default=float)[:3000])
