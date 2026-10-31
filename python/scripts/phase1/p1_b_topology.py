# SPDX-License-Identifier: AGPL-3.0-only
"""Phase 1 (B): topology of the 2-DOF gyroscopic honeycomb at audio-rate parameters.
 B1 bulk bands (Hz) along G-K-M-G, FHS Chern number per band, gaps in Hz
 B2 zigzag-ribbon spectrum (Hz) with edge polarisation
 B3 time-domain (ESAV-LF, 48 kHz) one-way edge transport and defect bypass on a finite flake:
    directionality and backscattering ratio in dB, for Omega>0, Omega<0, Omega=0 and for
    drive frequencies in gap 1, gap 2 and inside a bulk band."""
import json, os, sys, time
import numpy as np
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../.."))
from diffphs.topology import honeycomb_gyro2dof_model, bands, chern_fhs, ribbon_bands
from diffphs.gyro import GyroHoneycomb, GyroESAV, Params, audio_params
OUT = os.path.join(os.path.dirname(__file__), "../../../experiments/results/phase1")
FIG = os.path.join(os.path.dirname(__file__), "../../../reports/figures/phase1")
os.makedirs(OUT, exist_ok=True); os.makedirs(FIG, exist_ok=True)
AP = audio_params()
wu, fu = AP["wu"], AP["wu"] / (2 * np.pi)
k0r, Omr = AP["k0"] / AP["kc"], AP["g"] / (AP["m"] * wu)
res = dict(params=dict(f_unit_Hz=fu, k0_over_kc=k0r, Omega_over_wu=Omr, m=AP["m"], fs=AP["fs"]))

# ---------- B1 ----------
mdl = honeycomb_gyro2dof_model(k0r, 1.0, Omr)
b1, b2 = mdl.B
grid = bands(mdl, [(i / 60) * b1 + (j / 60) * b2 for i in range(60) for j in range(60)]) * fu
G0 = np.zeros(2); Kp = (2 * b1 + b2) / 3; Mp = b1 / 2
def seg(a, b, n=90): return [a + (b - a) * t for t in np.linspace(0, 1, n, endpoint=False)]
pathk = seg(G0, Kp) + seg(Kp, Mp) + seg(Mp, G0) + [G0]
wb = bands(mdl, pathk) * fu
Cs = chern_fhs(mdl, [[0], [1], [2], [3]], Nk=48)
gaps = [(float(grid[:, n].max()), float(grid[:, n + 1].min())) for n in range(3)]
res["bands_Hz"] = [[float(grid[:, n].min()), float(grid[:, n].max())] for n in range(4)]
res["chern"] = [round(float(c), 4) for c in Cs]
res["gaps_Hz"] = [dict(lo=a, hi=b, width=b - a, chern_sum_below=round(float(sum(Cs[:n + 1])), 4)) for n, (a, b) in enumerate(gaps)]
print(json.dumps(res, indent=1), flush=True)

# ---------- B2 ----------
kxs = np.linspace(-np.pi, np.pi, 181)
wr, er = ribbon_bands(mdl, 24, kxs)
wr = wr * fu
fig, ax = plt.subplots(1, 2, figsize=(12, 5))
for j in range(wb.shape[1]):
    ax[0].plot(np.arange(len(pathk)), wb[:, j], "k")
for n, (a, b) in enumerate(gaps):
    ax[0].axhspan(a, b, color="C1", alpha=0.2)
    ax[0].text(5, (a + b) / 2, f"gap {n+1}: {a:.0f}–{b:.0f} Hz, ΣC={sum(Cs[:n+1]):.0f}", fontsize=8)
ax[0].set_xticks([0, 90, 180, 270]); ax[0].set_xticklabels(["Γ", "K", "M", "Γ"])
ax[0].set_ylabel("frequency (Hz)"); ax[0].set_title(f"bulk bands, Chern per band = {np.round(Cs, 2).tolist()}")
for j in range(wr.shape[1]):
    ax[1].scatter(kxs, wr[:, j], c=er[:, 1, j], cmap="coolwarm", vmin=-1, vmax=1, s=1)
for (a, b) in gaps:
    ax[1].axhspan(a, b, color="C1", alpha=0.12)
ax[1].set_xlabel("k·a1"); ax[1].set_title("zigzag ribbon (W=24), colour = top(+)/bottom(−) edge weight")
fig.tight_layout(); fig.savefig(f"{FIG}/p1_b1_bands_ribbon_Hz.png", dpi=130); plt.close(fig)

# ---------- B3 time domain ----------
Lx, Ly = 40, 14
def build(defect):
    base = GyroHoneycomb(Lx, Ly, AP["k0"], AP["kc"])
    rm = []
    if defect:
        xy = base.coords
        xd = Lx / 2 + 4.0
        rm = list(np.where((np.abs(xy[:, 0] - xd) < 1.1) & (xy[:, 1] < 1.5))[0])   # remove a notch on the bottom edge
    return GyroHoneycomb(Lx, Ly, AP["k0"], AP["kc"], remove_sites=rm), len(rm)

freqs = {"gap1": 0.5 * sum(gaps[0]), "gap2": 0.5 * sum(gaps[1]), "bulk_band2": 0.5 * sum(res["bands_Hz"][1])}
Omegas = {"Omega+": +1, "Omega-": -1, "Omega0": 0}
cols = [(o, fk) for o in Omegas for fk in freqs]
B = len(cols)
T = 6000; h = AP["h"]
td = {}
snap_store = {}
for defect in (False, True):
    lat, nrm = build(defect)
    xy = lat.coords; N = lat.N
    xmid = Lx / 2
    ybot = xy[:, 1] < 1.5
    src = int(np.argmin((xy[:, 0] - (xmid - 4.0)) ** 2 + xy[:, 1] ** 2))
    # absorbing frame: graded damping away from the bottom-edge corridor (sides and top)
    dside = np.maximum(0, np.maximum(xy[:, 0] - xy[:, 1] / np.sqrt(3) - (Lx - 7), 6 - (xy[:, 0] - xy[:, 1] / np.sqrt(3))))
    dtop = np.maximum(0, xy[:, 1] - (Ly * np.sqrt(3) / 2 - 5))
    damp = 4000.0 * (np.clip(dside / 6, 0, 1) ** 2 + np.clip(dtop / 5, 0, 1) ** 2)
    P = Params(N, B, m=AP["m"], mu=-damp[:, None], g=np.array([AP["g"] * Omegas[o] for o, _ in cols])[None, :])
    I = GyroESAV(lat, h, P, np.zeros((2 * N, B)), np.zeros((2 * N, B)))
    tt = np.arange(T) * h; t0, sg = 0.012, 0.004
    env = np.exp(-0.5 * ((tt - t0) / sg) ** 2)
    fr = np.array([freqs[fk] for _, fk in cols])
    xs = xy[:, 0]
    right = ybot & (xs > xmid + 7) & (xs < Lx - 7); left = ybot & (xs < xmid - 9) & (xs > 6)
    Er = np.zeros(B); El = np.zeros(B); Etot_in = np.zeros(B)
    snaps = []
    for n in range(T):
        f = np.zeros((2 * N, B))
        f[2 * src] = 3e5 * env[n] * np.sin(2 * np.pi * fr * tt[n])
        vb = I.step(f, return_vbar=True)
        e = 0.5 * (vb[0::2] ** 2 + vb[1::2] ** 2)  # kinetic energy density per site (m=1)
        Er += e[right].sum(0); El += e[left].sum(0)
        if defect and n in (500, 900, 1500):
            snaps.append(e[:, 0].copy())
    for b, (o, fk) in enumerate(cols):
        td[f"{'defect' if defect else 'clean'}|{o}|{fk}"] = dict(f_Hz=float(fr[b]), E_right=float(Er[b]), E_left=float(El[b]),
                                                              right_over_left_dB=float(10 * np.log10((Er[b] + 1e-300) / (El[b] + 1e-300))))
    if defect:
        snap_store = dict(xy=xy, snaps=snaps, nrm=nrm, src=src, right=right, left=left)
for k_ in [k for k in td if k.startswith("defect")]:
    c = td[k_.replace("defect", "clean")]; d = td[k_]
    d["transmission_vs_clean_dB"] = float(10 * np.log10(d["E_right"] / c["E_right"]))
res["time_domain"] = td
res["time_domain_setup"] = dict(Lx=Lx, Ly=Ly, steps=T, fs=AP["fs"], burst_sigma_s=0.004, removed_sites_defect=snap_store["nrm"],
                                detectors="bottom-edge corridor (y<1.5), right: xmid+7<x<Lx-7, left: 6<x<xmid-9; source at xmid-4; defect notch at xmid+4; xmid=Lx/2",
                                absorbing="graded mu<0 on side and top frame")
for k_, v in td.items():
    print(k_, {a: round(b, 2) for a, b in v.items()})
xy = snap_store["xy"]
fig, ax = plt.subplots(3, 1, figsize=(10, 9))
for i, s in enumerate(snap_store["snaps"]):
    sc = ax[i].scatter(xy[:, 0], xy[:, 1], c=np.log10(s + 1e-12), s=9, cmap="magma", vmin=np.log10(s.max()) - 3)
    ax[i].plot(*xy[snap_store["src"]], "c*", ms=10)
    ax[i].set_aspect("equal"); ax[i].set_title(f"Ω>0, gap-2 drive, notch defect, t = {[500,900,1500][i]/48:.1f} ms (log10 kinetic energy, 3 decades)")
fig.tight_layout(); fig.savefig(f"{FIG}/p1_b2_edge_defect_snapshots.png", dpi=120); plt.close(fig)
fig, ax = plt.subplots(figsize=(9, 4))
labels = list(td.keys()); vals = [td[k]["right_over_left_dB"] for k in labels]
ax.barh(range(len(labels)), vals, color=["C3" if "defect" in k else "C0" for k in labels])
ax.set_yticks(range(len(labels))); ax.set_yticklabels(labels, fontsize=7); ax.set_xlabel("E_right / E_left (dB)")
ax.axvline(0, color="k"); fig.tight_layout(); fig.savefig(f"{FIG}/p1_b3_directionality_dB.png", dpi=130); plt.close(fig)
json.dump(res, open(f"{OUT}/p1_b_topology.json", "w"), indent=1)
