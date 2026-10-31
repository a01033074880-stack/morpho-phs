# SPDX-License-Identifier: AGPL-3.0-only
"""Audit (a): does the proposal's uniform sin(Phi) gyroscopic coupling on a scalar square
lattice open a topological gap?  Compare with honeycomb (Haldane-type NNN) and 2-DOF
gyroscopic honeycomb (Nash 2015 / Wang-Lu-Bertoldi 2015)."""
import sys, os, json
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import numpy as np
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import scipy.sparse as sp
import scipy.sparse.linalg as spla
from diffphs.topology import (square_scalar_model, honeycomb_scalar_model, honeycomb_gyro2dof_model,
                              bands, chern_fhs, ribbon_bands, first_order_L)
from diffphs.model import square_lattice, honeycomb_scalar

FIG = os.path.join(os.path.dirname(__file__), "../../reports/figures")
OUT = os.path.join(os.path.dirname(__file__), "../../experiments/results")
os.makedirs(FIG, exist_ok=True); os.makedirs(OUT, exist_ok=True)
k0, kc = 0.2, 1.0
res = {}

def path(points, n=80):
    ks, xs, x0 = [], [], 0.0
    for a, b in zip(points[:-1], points[1:]):
        for t in np.linspace(0, 1, n, endpoint=False):
            ks.append(a + t * (b - a))
        x0 += 1
    ks.append(points[-1]); return np.array(ks)

# ---------- 1. proposal: scalar square lattice ----------
G0 = np.zeros(2); X = np.array([np.pi, 0]); M = np.array([np.pi, np.pi])
kp = path([-X, G0, X, M, G0])
fig, ax = plt.subplots(1, 3, figsize=(14, 4))
for Phi, c in ((0, "k"), (np.pi / 2, "r")):
    mdl = square_scalar_model(k0, kc, 0.3, Phi)
    w = bands(mdl, kp)
    ax[0].plot(w, c, label=f"Phi={Phi:.2f}")
    C = chern_fhs(mdl, [[0]], Nk=32)[0]
    res[f"square_scalar_Phi={Phi:.3f}_C"] = round(C, 6)
ax[0].set_xticks([0, 80, 160, 240, 320]); ax[0].set_xticklabels(["-X", "Γ", "X", "M", "Γ"])
ax[0].set_title("Proposal: scalar square lattice\n1 band per k (C=0 always); Φ only tilts ω(k)≠ω(-k)")
ax[0].set_ylabel("ω (units √(kc/m))"); ax[0].legend()
# open boundary 16x16 flake
for col, Phi in ((1, 0.0), (2, np.pi / 2)):
    lat = square_lattice(16, k0, kc, 0.3, Phi)
    L = first_order_L(lat.K.toarray().astype(complex), lat.G.toarray().astype(complex), np.ones(lat.N))
    w, V = np.linalg.eigh(L)
    sel = w > 0; w = w[sel]; V = V[:, sel]
    dens = np.abs(V[:lat.N]) ** 2 + np.abs(V[lat.N:]) ** 2
    ew = dens[lat.edge].sum(0)
    sc = ax[col].scatter(np.arange(len(w)), w, c=ew, cmap="viridis", s=6, vmin=0, vmax=1)
    ax[col].set_title(f"16×16 open boundary, Φ={Phi:.2f}: no bulk gap\n(colour = mode weight on boundary sites, {lat.edge.mean():.0%} of sites)")
    ax[col].set_xlabel("mode index"); plt.colorbar(sc, ax=ax[col])
    gaps = np.diff(w); res[f"square16_Phi={Phi:.3f}_max_spacing_rel"] = float(gaps.max() / (w.max() - w.min()))
fig.tight_layout(); fig.savefig(f"{FIG}/a1_square_scalar_no_gap.png", dpi=130); plt.close(fig)

# ---------- 2. honeycomb scalar Haldane-type NNN gyroscopic ----------
a1 = np.array([1, 0.]); a2 = np.array([.5, np.sqrt(3) / 2])
mdl0 = honeycomb_scalar_model(k0, kc, 0.15)
b1, b2 = mdl0.B
Kpt = (2 * b1 + b2) / 3; Mpt = b1 / 2
kp = path([G0, Kpt, Mpt, G0])
fig, ax = plt.subplots(2, 3, figsize=(15, 8))
for col, (s2, hal, lab) in enumerate(((0.0, True, "σ2=0 (reciprocal)"), (0.15, True, "σ2=0.15 Haldane (sublattice-odd)"),
                                       (0.15, False, "σ2=0.15 same sign on A/B"))):
    mdl = honeycomb_scalar_model(k0, kc, s2, haldane=hal)
    w = bands(mdl, kp)
    ax[0, col].plot(w, "k"); ax[0, col].set_xticks([0, 80, 160, 240]); ax[0, col].set_xticklabels(["Γ", "K", "M", "Γ"])
    Cs = chern_fhs(mdl, [[0], [1]], Nk=36) if s2 != 0 else [0.0, 0.0]
    gap = bands(mdl, [Kpt, -Kpt]); 
    allw = bands(mdl, [(i / 42) * b1 + (j / 42) * b2 for i in range(42) for j in range(42)])
    g = allw[:, 1].min() - allw[:, 0].max()
    ax[0, col].set_title(f"{lab}\nC=({Cs[0]:+.2f},{Cs[1]:+.2f}), full gap={g:.3f}")
    res[f"honey_scalar_s2={s2}_haldane={hal}"] = dict(C=[round(c, 4) for c in Cs], gap=float(g))
    kxs = np.linspace(-np.pi, np.pi, 161)
    wr, er = ribbon_bands(mdl, 24, kxs)
    for j in range(wr.shape[1]):
        ax[1, col].scatter(kxs, wr[:, j], c=er[:, 1, j], cmap="coolwarm", vmin=-1, vmax=1, s=1.5)
    ax[1, col].set_xlabel("k·a1"); ax[1, col].set_ylim(allw.min() * 0.95, allw.max() * 1.02)
    ax[1, col].set_title("ribbon (W=24): red=top edge, blue=bottom edge")
ax[0, 0].set_ylabel("ω"); ax[1, 0].set_ylabel("ω")
fig.tight_layout(); fig.savefig(f"{FIG}/a2_honeycomb_scalar_haldane.png", dpi=130); plt.close(fig)

# gap and Chern vs Phi with sigma2 = sigma*sin(Phi)
Phis = np.linspace(0, 2 * np.pi, 25)
gaps, chs = [], []
for Phi in Phis:
    mdl = honeycomb_scalar_model(k0, kc, 0.15 * np.sin(Phi))
    allw = bands(mdl, [(i / 30) * b1 + (j / 30) * b2 for i in range(30) for j in range(30)])
    g = allw[:, 1].min() - allw[:, 0].max(); gaps.append(g)
    chs.append(chern_fhs(mdl, [[0]], Nk=24)[0] if g > 1e-3 else np.nan)
res["honey_vs_Phi"] = dict(Phi=Phis.tolist(), gap=[float(x) for x in gaps], C=[None if np.isnan(c) else round(float(c), 3) for c in chs])

# ---------- 3. 2-DOF gyroscopic honeycomb ----------
fig, ax = plt.subplots(1, 2, figsize=(11, 4))
for col, Om in enumerate((0.0, 0.6)):
    mdl = honeycomb_gyro2dof_model(k0, kc, Om)
    allw = bands(mdl, [(i / 36) * b1 + (j / 36) * b2 for i in range(36) for j in range(36)])
    gapinfo = [float(allw[:, n + 1].min() - allw[:, n].max()) for n in range(3)]
    Cs = chern_fhs(mdl, [[0], [1], [2], [3]], Nk=36)
    res[f"gyro2dof_Omega={Om}"] = dict(C=[round(c, 3) for c in Cs], gaps=gapinfo)
    kxs = np.linspace(-np.pi, np.pi, 141)
    wr, er = ribbon_bands(mdl, 20, kxs)
    for j in range(wr.shape[1]):
        ax[col].scatter(kxs, wr[:, j], c=er[:, 1, j], cmap="coolwarm", vmin=-1, vmax=1, s=1.2)
    ax[col].set_title(f"2-DOF gyroscopic honeycomb, Ω={Om}\nC={np.round(Cs,2).tolist()}, gaps={np.round(gapinfo,3).tolist()}")
    ax[col].set_xlabel("k·a1")
fig.tight_layout(); fig.savefig(f"{FIG}/a3_gyro2dof_honeycomb_ribbon.png", dpi=130); plt.close(fig)

# ---------- 4. time-domain one-way edge transport on scalar Haldane flake ----------
def run_flake(s2, Lc=22, T=260.0, dt=0.05, defect=True):
    lat = honeycomb_scalar(Lc, Lc, k0, kc, s2)
    N = lat.N
    K = lat.K.tolil()
    xy = lat.coords
    # stiff defect: 3 sites on bottom edge region near middle get huge grounding spring
    bot = np.where(lat.edge & (xy[:, 1] < 0.9))[0]
    if defect:
        cx = xy[:, 0].mean() + 3
        dsites = np.argsort((xy[:, 0] - cx) ** 2 + (xy[:, 1]) ** 2)[:4]
        for s in dsites: K[s, s] += 50.0
    K = K.tocsr(); G = lat.G
    mdl = honeycomb_scalar_model(k0, kc, abs(s2) if s2 != 0 else 0.15)
    allw = bands(mdl, [(i / 30) * b1 + (j / 30) * b2 for i in range(30) for j in range(30)])
    wc = 0.5 * (allw[:, 1].min() + allw[:, 0].max())
    # source: bottom-edge site left of centre, Gaussian-windowed tone at gap centre
    src = np.argmin((xy[:, 0] - (xy[:, 0].mean() - 6)) ** 2 + xy[:, 1] ** 2)
    I = sp.identity(N, format="csc")
    # implicit midpoint (exactly energy conserving for linear system) on (q,v)
    A = sp.bmat([[None, I], [-K, G]], format="csc")
    Ibig = sp.identity(2 * N, format="csc")
    lu = spla.splu((Ibig - 0.5 * dt * A).tocsc()); Bm = (Ibig + 0.5 * dt * A).tocsr()
    x = np.zeros(2 * N); nsteps = int(T / dt); snaps = {}
    for n in range(nsteps):
        t = (n + 0.5) * dt
        f = np.zeros(2 * N)
        f[N + src] = np.sin(wc * t) * np.exp(-((t - 40) / 15) ** 2)
        x = lu.solve(Bm @ x + dt * f)
        if n in (int(80 / dt), int(150 / dt), nsteps - 1):
            snaps[round(t)] = 0.5 * (x[N:] ** 2 + x[:N] * (K @ x[:N]))
    return lat, snaps, src, wc

fig, ax = plt.subplots(3, 3, figsize=(15, 12))
for r, s2 in enumerate((0.0, 0.15, -0.15)):
    lat, snaps, src, wc = run_flake(s2)
    for c, (t, e) in enumerate(sorted(snaps.items())):
        a = ax[r, c]
        a.scatter(lat.coords[:, 0], lat.coords[:, 1], c=np.log10(np.abs(e) + 1e-9), s=6, cmap="magma", vmin=-6, vmax=-1)
        a.plot(*lat.coords[src], "c*", ms=10); a.set_aspect("equal"); a.set_xticks([]); a.set_yticks([])
        a.set_title(f"σ2={s2:+.2f}, t={t} (ω_drive={wc:.2f}, gap centre)")
fig.suptitle("Scalar honeycomb with Haldane-type NNN gyroscopic coupling: local energy (log10).\n"
             "σ2≠0 → wave travels one way along the edge and passes the stiff defect; sign of σ2 flips direction; σ2=0 → bulk-gap-free spreading", fontsize=11)
fig.tight_layout(); fig.savefig(f"{FIG}/a4_edge_transport_timedomain.png", dpi=110); plt.close(fig)

json.dump(res, open(f"{OUT}/a_topology.json", "w"), indent=1)
print(json.dumps({k: v for k, v in res.items() if k != "honey_vs_Phi"}, indent=1))
print("gap vs Phi:", np.round(gaps, 4).tolist()); print("C vs Phi:", chs)
