#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 MORPHO-PHS contributors
"""Phase 6b preset QA: loudness / DC / tail / spectral descriptors from `morpho_cli qa` renders.

usage: qa_analyze.py <qa_dir> [--spectrograms] [--target -18] [--fx-target -20] [--tol 2]
Instrument target: -18 LUFS-I (demo phrase); FX target: -20 LUFS-I = the dry demo input (loudness-neutral against bypass).
Reads <qa_dir>/qa.json (written by morpho_cli qa) and the WAVs beside it; writes <qa_dir>/analysis.json.
"""
import argparse, json, math, os, pathlib, sys
import numpy as np, soundfile as sf, pyloudnorm as pyln
from scipy.signal import resample_poly

def db(x): return 20.0 * math.log10(max(float(x), 1e-12))

# Phase 6b2 headroom rule: the output safety limiter starts at 0.9.  The canonical demo render must keep its sample peak
# <= 0.80 (1 dB below the knee: covers the libm/ULP drift of chaotic presets between GCC and MSVC, worst seen +0.8 dB),
# and FX presets additionally <= 0.89 / seed_peak_ratio, so no seed of the portable demo noise (seed_sweep6b2.py)
# reaches the knee.
PEAK_CEIL, KNEE_SAFE = 0.80, 0.89
def allowed_peak(name, measured):
    return min(PEAK_CEIL, KNEE_SAFE / max(1.0, float(measured.get(name, {}).get("seed_peak_ratio", 1.0))))
def load_measured():
    p = pathlib.Path(__file__).resolve().parent / "bank6b_measured.json"
    return json.loads(p.read_text()) if p.exists() else {}

def analyse(path, play_s, row):
    x, fs = sf.read(path, always_2d=True); x = x.astype(np.float64)
    m = pyln.Meter(fs)
    lufs = m.integrated_loudness(x)
    play_n = int(play_s * fs)
    lufs_play = m.integrated_loudness(x[:play_n]) if play_n > fs else lufs
    peak = np.abs(x).max()
    tp = np.abs(resample_poly(x, 4, 1, axis=0)).max()
    dc = np.abs(x.mean(axis=0)).max()
    dc_tail = np.abs(x[-fs:].mean(axis=0)).max()
    # short-term level (100 ms RMS, 50 ms hop, max over channels)
    hop, win = fs // 20, fs // 10
    nfr = max(1, (len(x) - win) // hop)
    lev = np.array([db(np.sqrt((x[i*hop:i*hop+win]**2).mean(axis=0)).max()) for i in range(nfr)])
    t = (np.arange(nfr) * hop + win / 2) / fs
    tail = None
    after = np.where(t >= play_s)[0]
    if len(after):
        loud = after[lev[after] > -60.0]
        if len(loud) == 0: tail = 0.0
        elif loud[-1] < after[-1] - 10: tail = float(t[loud[-1] + 1] - play_s)   # >= 0.5 s below -60 dBFS before the end
    end_level = float(lev[-3:].max())
    # spectral descriptors over the played part (mono), frames above -50 dB of the loudest frame
    mono = x[:play_n].mean(axis=1) if play_n > fs else x.mean(axis=1)
    N, H = 2048, 512
    w = np.hanning(N)
    fr = np.lib.stride_tricks.sliding_window_view(mono, N)[::H] * w
    S = np.abs(np.fft.rfft(fr, axis=1)); f = np.fft.rfftfreq(N, 1 / fs)
    P = S ** 2; e = P.sum(axis=1); keep = e > e.max() * 1e-5
    Pk, Sk = P[keep], S[keep]
    cent = (Pk @ f) / Pk.sum(axis=1)
    cum = np.cumsum(Pk, axis=1); roll = f[np.argmax(cum >= 0.85 * cum[:, -1:], axis=1)]
    Sn = Sk / (np.linalg.norm(Sk, axis=1, keepdims=True) + 1e-12)
    flux = np.sqrt(((np.diff(Sn, axis=0)) ** 2).sum(axis=1)) if len(Sn) > 1 else np.zeros(1)
    flat = np.exp(np.log(Pk + 1e-20).mean(axis=1)) / (Pk.mean(axis=1) + 1e-20)
    lo = (Pk[:, f < 200].sum(axis=1) / Pk.sum(axis=1)).mean()
    tr = row.get("energy_trace", []) or [0]
    tr = np.array(tr, dtype=float)
    k2 = max(1, int(2.0 / 0.0987))
    runaway = bool(tr[-1] > 1.10 or (len(tr) > k2 and tr[-1] > 0.5 and tr[-1] > tr[-k2] * 1.02))
    sustains = bool(tail is None)
    return dict(lufs_i=lufs, lufs_play=lufs_play, peak_dbfs=db(peak), true_peak_dbtp=db(tp), dc_dbfs=db(dc), dc_tail_dbfs=db(dc_tail),
                tail_s=tail, end_level_dbfs=end_level, centroid_hz=float(np.median(cent)), centroid_mean_hz=float(cent.mean()),
                rolloff85_hz=float(np.median(roll)), flux=float(flux.mean()), flatness=float(np.median(flat)), low_ratio=float(lo),
                runaway=runaway, sustains=sustains, energy_end=float(tr[-1]), energy_max=float(row.get("energy_max_over_cap", 0))), (S, f, fs, H)

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("dir"); ap.add_argument("--spectrograms", action="store_true")
    ap.add_argument("--target", type=float, default=-18.0); ap.add_argument("--fx-target", type=float, default=-20.0); ap.add_argument("--tol", type=float, default=2.0)
    a = ap.parse_args()
    rows = json.load(open(os.path.join(a.dir, "qa.json")))
    measured = load_measured()
    out = []
    if a.spectrograms:
        import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
    for r in rows:
        res, (S, f, fs, H) = analyse(os.path.join(a.dir, r["wav"]), r["play_s"], r)
        res.update({("render_" + k if k in res else k): r[k] for k in r if k != "energy_trace"})
        fails = []
        if not r["finite"] or r["nan_resets"]: fails.append("non-finite")
        tgt = a.target if r["mode"] == 2 else a.fx_target
        res["lufs_target"] = tgt
        # peak-limited: a percussive preset whose crest factor cannot reach the target under -1 dBTP is normalised by
        # peak instead (EBU R128 practice for one-shots); reported, not failed.
        allow = allowed_peak(r["name"], measured); res["peak_allowed"] = allow
        res["peak_limited"] = bool(res["lufs_i"] < tgt - a.tol and (res["true_peak_dbtp"] >= -1.6 or res["peak_dbfs"] >= db(allow) - 0.6))
        res["plr_db"] = res["true_peak_dbtp"] - res["lufs_i"]
        if abs(res["lufs_i"] - tgt) > a.tol and not res["peak_limited"]: fails.append("loudness %.1f" % res["lufs_i"])
        if res["dc_dbfs"] > -60: fails.append("DC %.1f dBFS" % res["dc_dbfs"])
        if res["runaway"]: fails.append("energy runaway")
        if res["sustains"]: fails.append("tail does not end")
        elif res["tail_s"] > 12: fails.append("tail %.1f s" % res["tail_s"])
        if r["limiter_samples"] > 0: fails.append("limiter %d" % r["limiter_samples"])
        if res["peak_dbfs"] > db(allow) + 0.1: fails.append("headroom: peak %.2f dBFS > %.2f" % (res["peak_dbfs"], db(allow)))
        if res["true_peak_dbtp"] > -1.0: fails.append("true peak %.1f dBTP" % res["true_peak_dbtp"])
        res["fails"] = fails
        out.append(res)
        print("%-24s %6.1f LUFS tp %5.1f dc %6.1f tail %5s cen %6.0f roll %6.0f flux %.3f %s" % (
            r["name"][:24], res["lufs_i"], res["true_peak_dbtp"], res["dc_dbfs"],
            "inf" if res["tail_s"] is None else "%.1f" % res["tail_s"], res["centroid_hz"], res["rolloff85_hz"], res["flux"],
            ("FAIL: " + ", ".join(fails)) if fails else ("ok (peak-limited, PLR %.1f dB)" % res["plr_db"] if res["peak_limited"] else "ok")))
        if a.spectrograms:
            fig = plt.figure(figsize=(8, 3), dpi=90)
            Z = 20 * np.log10(np.abs(S).T + 1e-9)
            plt.imshow(Z, origin="lower", aspect="auto", extent=[0, S.shape[0] * H / fs, 0, f[-1] / 1000], vmin=Z.max() - 80, vmax=Z.max(), cmap="magma")
            plt.ylim(0, 12); plt.title(r["name"]); plt.xlabel("s"); plt.ylabel("kHz"); plt.tight_layout()
            fig.savefig(os.path.join(a.dir, os.path.splitext(r["wav"])[0] + "_spec.png")); plt.close(fig)
    json.dump(out, open(os.path.join(a.dir, "analysis.json"), "w"), indent=1)
    print("%d preset(s), %d with failures" % (len(out), sum(1 for o in out if o["fails"])))

if __name__ == "__main__":
    main()
