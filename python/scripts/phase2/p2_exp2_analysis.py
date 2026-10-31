# SPDX-License-Identifier: AGPL-3.0-only
"""Phase 2 exp2 analysis: directionality D(f), group delay, distance sweep, drum-weighted broadband ratio,
in-gap filter fixes (from exp2_impulse.npz) + space-time / backscatter (exp2_burst.json, exp2_spacetime.npz).
-> experiments/results/phase2/exp2_analysis.json ; figures -> paper/figures + reports/figures/phase2"""
import json, os
import numpy as np
from scipy import signal
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
HERE = os.path.dirname(__file__); ROOT = os.path.join(HERE, "../../..")
RES = os.path.join(ROOT, "experiments/results/phase2"); FIGS = [os.path.join(ROOT, "paper/figures"), os.path.join(ROOT, "reports/figures/phase2")]
FS = 48000.0
top = json.load(open(os.path.join(RES, "../phase1/p1_b_topology.json")))
GAPS = [(g["lo"], g["hi"]) for g in top["gaps_Hz"] if g.get("chern_sum_below", 0) != 0]
ALLGAPS = [(g["lo"], g["hi"], g.get("chern_sum_below", 0)) for g in top["gaps_Hz"]]


def save(fig, name):
    for d in FIGS:
        os.makedirs(d, exist_ok=True); fig.savefig(os.path.join(d, name + ".pdf")); fig.savefig(os.path.join(d, name + ".png"), dpi=150)
    plt.close(fig)


def smooth_oct(f, P, frac=6):
    out = np.empty_like(P)
    for i, fi in enumerate(f):
        if fi <= 0: out[i] = P[i]; continue
        lo, hi = fi * 2 ** (-0.5 / frac), fi * 2 ** (0.5 / frac); m = (f >= lo) & (f <= hi); out[i] = P[m].mean()
    return out


def in_gaps(f, gaps=None):
    gaps = GAPS if gaps is None else gaps
    m = np.zeros_like(f, bool)
    for lo, hi in gaps: m |= (f >= lo) & (f <= hi)
    return m


def gap_filter_H2(f, order=8):
    """|H|^2 of the parallel steep Butterworth band-pass bank used for the in-gap fix (same as the C++ demo)."""
    H = np.zeros_like(f, complex)
    for lo, hi in GAPS:
        sos = signal.butter(order // 2, [lo * 1.03, hi * 0.97], "bandpass", fs=FS, output="sos")
        _, h = signal.sosfreqz(sos, worN=f, fs=FS); H += h
    return np.abs(H) ** 2


out = {"gaps_with_edge_modes_Hz": GAPS, "all_gaps": ALLGAPS}
Z = np.load(os.path.join(RES, "exp2_impulse.npz"))
rec, xb, xs, cols = Z["rec"].astype(float), Z["xb"], float(Z["xs"]), [str(c) for c in Z["cols"]]
T = rec.shape[0]; win = np.ones(T); nt = T // 8; win[-nt:] = 0.5 * (1 + np.cos(np.pi * np.arange(nt) / nt))
V = np.fft.rfft(rec * win[:, None, None, None], axis=0); f = np.fft.rfftfreq(T, 1 / FS)
P = (np.abs(V) ** 2).sum(2)                                # (F, nb, B) power, both components
drum = np.fromfile(os.path.join(ROOT, "experiments/audio/drum_loop_48k.f32"), np.float32).astype(float)
fd, Pd = signal.welch(drum, FS, nperseg=4096); Wd = np.interp(f, fd, Pd)
H2 = gap_filter_H2(f)


def site_at(x): return int(np.argmin(np.abs(xb - x)))


def ratio_dB(PR, PL, w): return 10 * np.log10(np.sum(w * PR) / np.sum(w * PL))


band = (f > 80) & (f < 6000)
out["D_at_8"] = {}; out["broadband"] = {}
fig, axs = plt.subplots(3, 1, figsize=(8, 8), sharex=True)
for b, c in enumerate(cols):
    o, a = c.split("|")
    R, L = P[:, site_at(xs + 8), b], P[:, site_at(xs - 8), b]
    D = 10 * np.log10(smooth_oct(f, R) / smooth_oct(f, L))
    ax = axs[["none", "sponge", "frame"].index(a)]
    ax.semilogx(f[band], D[band], label=f"Omega {o}")
    ax.set_title(f"absorber: {a}"); ax.set_ylabel("D(f) = R/L dB")
    gm = in_gaps(f) & band; om = (~in_gaps(f)) & band
    out["D_at_8"][c] = {"in_gap_mean_dB": float(np.mean(D[gm])), "out_gap_mean_dB": float(np.mean(D[om])),
                         **{f"gap{k+1}_mean_dB": float(np.mean(D[(f >= lo) & (f <= hi)])) for k, (lo, hi) in enumerate(GAPS)}}
    out["broadband"][c] = {"flat_dB": ratio_dB(R[band], L[band], np.ones(band.sum())),
                           "drum_weighted_dB": ratio_dB(R[band], L[band], Wd[band]),
                           "drum_weighted_ingap_ideal_dB": ratio_dB(R[band], L[band], (Wd * in_gaps(f))[band]),
                           "drum_weighted_butter8_bank_dB": ratio_dB(R[band], L[band], (Wd * H2)[band]),
                           "drum_energy_fraction_in_gaps": float(np.sum(Wd[band] * in_gaps(f)[band]) / np.sum(Wd[band]))}
for ax in axs:
    for lo, hi in GAPS: ax.axvspan(lo, hi, color="0.85", zorder=0)
    ax.axhline(0, color="k", lw=0.5); ax.set_ylim(-45, 45); ax.legend(fontsize=7, loc="lower left")
axs[-1].set_xlabel("frequency (Hz)  [grey: bulk gaps with chiral edge modes]")
fig.tight_layout(); save(fig, "p2_exp2_directionality_Df")

# distance sweep (frame absorber; Omega+ and Omega0)
ds = np.arange(2, 19, 2); out["distance_sweep"] = {}
fig, ax = plt.subplots(figsize=(7, 4))
for c in ("+1|frame", "+0|frame", "-1|frame"):
    b = cols.index(c); rows = []
    for d in ds:
        R, L = P[:, site_at(xs + d), b], P[:, site_at(xs - d), b]
        rows.append([ratio_dB(R[band], L[band], (Wd * in_gaps(f))[band]), ratio_dB(R[band], L[band], (Wd * ~in_gaps(f))[band])])
    rows = np.array(rows); out["distance_sweep"][c] = {"d": ds.tolist(), "ingap_dB": rows[:, 0].tolist(), "outgap_dB": rows[:, 1].tolist()}
    ax.plot(ds, rows[:, 0], "o-", label=f"{c} in-gap"); ax.plot(ds, rows[:, 1], "x--", label=f"{c} out-of-gap")
ax.set_xlabel("pickup distance from source (lattice units)"); ax.set_ylabel("R/L energy ratio, drum-weighted (dB)"); ax.legend(fontsize=7); ax.grid(alpha=0.3)
fig.tight_layout(); save(fig, "p2_exp2_distance_sweep")

# group delay at the forward pickup (d = 8) and group velocity from band-limited envelope arrival vs distance
out["group"] = {}
fig, axs = plt.subplots(1, 2, figsize=(11, 4))
for c in ("+1|frame", "+0|frame"):
    b = cols.index(c); Hr = V[:, site_at(xs + 8), 0, b]
    ph = np.unwrap(np.angle(Hr)); tg = -np.gradient(ph, 2 * np.pi * f)
    tgs = signal.medfilt(tg, 31)
    axs[0].plot(f[band], 1e3 * tgs[band], label=c, lw=0.8)
for lo, hi in GAPS: axs[0].axvspan(lo, hi, color="0.85", zorder=0)
axs[0].set_xscale("log"); axs[0].set_ylim(-2, 20); axs[0].set_xlabel("Hz"); axs[0].set_ylabel("group delay at d=8 (ms)"); axs[0].legend()
for k, (lo, hi) in enumerate(GAPS):
    fc, bw = 0.5 * (lo + hi), 0.35 * (hi - lo)
    sos = signal.butter(3, [fc - bw, fc + bw], "bandpass", fs=FS, output="sos")
    for c in ("+1|frame", "-1|frame"):
        b = cols.index(c); sgn = 1 if c.startswith("+") else -1
        dd, tt = [], []
        for d in range(3, 17):
            x = rec[:, site_at(xs + sgn * d), 0, b]; e = np.abs(signal.hilbert(signal.sosfiltfilt(sos, x)))
            dd.append(d); tt.append(np.argmax(e) / FS)
        dd, tt = np.array(dd), np.array(tt); A = np.polyfit(dd, tt, 1)
        out["group"][f"gap{k+1}|{c}"] = {"fc_Hz": fc, "delay_per_unit_ms": 1e3 * A[0], "group_velocity_units_per_ms": 1 / (1e3 * A[0]) if A[0] > 0 else None,
                                         "fit_rms_ms": float(1e3 * np.sqrt(np.mean((np.polyval(A, dd) - tt) ** 2)))}
        axs[1].plot(dd, 1e3 * tt, "o", ms=3, label=f"gap{k+1} {c}")
axs[1].set_xlabel("distance along edge (forward direction)"); axs[1].set_ylabel("envelope arrival (ms)"); axs[1].legend(fontsize=6)
fig.tight_layout(); save(fig, "p2_exp2_group_delay")

# ---------- bursts ----------
if os.path.exists(os.path.join(RES, "exp2_burst.json")):
    Bj = json.load(open(os.path.join(RES, "exp2_burst.json"))); out["burst"] = Bj["runs"]
    S = np.load(os.path.join(RES, "exp2_spacetime.npz")); scols = [str(c) for c in S["cols"]]; dt = float(S["dt_ms"])
    fig, axs = plt.subplots(3, 4, figsize=(14, 9))
    for i, o in enumerate(("+1", "+0", "-1")):
        for j, dn in enumerate(("none", "vacancy_notch", "heavy_mass_x25", "Omega0_patch")):
            st = S[f"{dn}_st"]; xbb = S[f"{dn}_xb"]; b = scols.index(f"{o}|gap1")
            img = 10 * np.log10(st[:, :, b] + st[:, :, b].max() * 1e-6)
            axs[i, j].imshow(img, aspect="auto", origin="lower", extent=[xbb[0], xbb[-1], 0, st.shape[0] * dt], vmin=img.max() - 50, vmax=img.max(), cmap="magma")
            axs[i, j].axvline(16, color="c", lw=0.6); axs[i, j].axvline(24, color="w", lw=0.6, ls="--")
            r = Bj["runs"][f"{dn}|Omega{o}|gap1"]
            axs[i, j].set_title(f"Omega{o} {dn}\nbackscatter {r['backscatter_dB']:.1f} dB", fontsize=8)
            if j == 0: axs[i, j].set_ylabel("time (ms)")
            if i == 2: axs[i, j].set_xlabel("x along bottom edge")
    fig.tight_layout(); save(fig, "p2_exp2_spacetime_gap1")
json.dump(out, open(os.path.join(RES, "exp2_analysis.json"), "w"), indent=1)
print(json.dumps({k: out[k] for k in ("broadband", "group")}, indent=1))

# ---------- filter-bank options for the in-gap fix (frame absorber, d = 8, drum-weighted) ----------
def bank_H2(kind, order):
    H = np.zeros_like(f, complex)
    for lo, hi in GAPS:
        wn = [lo * 1.03, hi * 0.97]
        if kind == "butter": sos = signal.butter(order // 2, wn, "bandpass", fs=FS, output="sos")
        elif kind == "cheby2": sos = signal.cheby2(order // 2, 80, [lo, hi], "bandpass", fs=FS, output="sos")
        else: sos = signal.ellip(order // 2, 0.5, 80, wn, "bandpass", fs=FS, output="sos")
        _, h = signal.sosfreqz(sos, worN=f, fs=FS); H += h
    return np.abs(H) ** 2
out["filter_options"] = {}
for kind, order in (("butter", 8), ("butter", 16), ("butter", 24), ("ellip", 8), ("ellip", 12), ("cheby2", 12)):
    H2o = bank_H2(kind, order); row = {}
    for c in ("+1|frame", "-1|frame", "+0|frame", "+1|sponge"):
        b = cols.index(c); R, L = P[:, site_at(xs + 8), b], P[:, site_at(xs - 8), b]
        row[c] = ratio_dB(R[band], L[band], (Wd * H2o)[band])
    row["biquads_per_gap"] = order // 2; row["kept_ingap_drum_energy_dB"] = float(10 * np.log10(np.sum((Wd * H2o * in_gaps(f))[band]) / np.sum((Wd * in_gaps(f))[band])))
    out["filter_options"][f"{kind}{order}"] = row
    print(kind, order, {k: round(v, 2) for k, v in row.items()})
json.dump(out, open(os.path.join(RES, "exp2_analysis.json"), "w"), indent=1)
