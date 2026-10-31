# SPDX-License-Identifier: AGPL-3.0-only
"""Phase 2 exp3: control-layer coupling metrics.  Runs build/exp3_coupling (C++) for
  modes none / oneway / closed x seeds 0..2 (30 s each), a closed-loop kappa sweep and a tau sweep (one-way),
then computes on the left pickup (first 2 s discarded): spectral flatness, centroid trajectory, DFA exponent and
PSD slope of the centroid and log-RMS series (frame rate 93.75 Hz), multiscale sample entropy of the centroid
series, and the Rosenstein LLE of the waveform (with sine / noise / logistic-map calibration).
Results -> experiments/results/phase2/exp3_coupling.json"""
import json, os, subprocess, sys, time
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../.."))
from diffphs.complexity import frame_features, dfa, psd_slope, mse, lle_rosenstein
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
D = os.path.join(ROOT, "experiments/results/phase2/exp3"); os.makedirs(D, exist_ok=True)
FS = 48000; SECS = float(os.environ.get("SECS", 30))
runs = [(m, s, None, None) for m in (0, 1, 2) for s in (0, 1, 2)]
runs += [(2, 0, k, None) for k in (0.1, 1.0)]
runs += [(1, 0, 0.3, t) for t in (0.021, 0.0833, 0.1667)]
names = {0: "none", 1: "oneway", 2: "closed"}
meta = {}
for m, s, k, t in runs:
    args = [os.path.join(ROOT, "build/exp3_coupling"), str(m), str(s), str(SECS)]
    if k is not None: args += [str(k)]
    if t is not None: args += [str(t)]
    o = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, check=True).stdout.strip()
    j = json.loads(o); key = f"{names[m]}_s{s}" + (f"_k{k:.2f}" if k is not None and t is None else "") + (f"_tau{t:.4f}" if t is not None else "")
    j["file"] = (f"{names[m]}_s{s}" + (("_k" + f"{k:.6f}"[:4]) if k is not None else "") + (("_tau" + f"{t:.6f}"[:6]) if t is not None else "")) + ".f32"
    meta[key] = j; print(key, o, flush=True)


def analyze(fn):
    x = np.fromfile(os.path.join(D, fn), np.float32).reshape(-1, 2)[:, 0].astype(float)
    x = x[2 * FS:]
    ff = frame_features(x, FS); fr = ff["frame_rate"]
    c = ff["centroid"]; lr = np.log10(ff["rms"] + 1e-30)
    out = dict(flatness_mean=float(np.mean(ff["flatness"])), flatness_std=float(np.std(ff["flatness"])),
               centroid_mean_Hz=float(np.mean(c)), centroid_std_Hz=float(np.std(c)), rms_cv=float(np.std(ff["rms"]) / np.mean(ff["rms"])))
    for nm, ser in (("centroid", c), ("logrms", lr)):
        if np.std(ser) < 1e-12 * (abs(np.mean(ser)) + 1):
            out[f"dfa_{nm}"] = None; out[f"psd_beta_{nm}"] = None; continue
        d = dfa(ser); out[f"dfa_{nm}"] = {k: d[k] for k in ("alpha", "r2", "alpha_short", "alpha_long")}
        out[f"psd_beta_{nm}"] = psd_slope(ser, fr, 0.1, 10.0)[0]
    out["mse_centroid"] = mse(c) if np.std(c) > 0 else None
    seg = x[10 * FS: 10 * FS + 12000]
    out["lle_waveform"] = {k: v for k, v in lle_rosenstein(seg, FS).items() if k != "curve"}
    out["centroid_series_head"] = c[:: 4][:700].tolist()
    return out


res = dict(meta=meta, analysis={})
t0 = time.time()
for key, j in meta.items():
    res["analysis"][key] = analyze(j["file"]); a = res["analysis"][key]
    print(key, "flat", round(a["flatness_mean"], 4), "dfaC", a["dfa_centroid"] and round(a["dfa_centroid"]["alpha"], 3),
          "dfaR", a["dfa_logrms"] and round(a["dfa_logrms"]["alpha"], 3), "lle", round(a["lle_waveform"]["lle_per_s"], 1), flush=True)
# calibration of the LLE estimator on known signals (same parameters)
rng = np.random.default_rng(0); n = 12000; tt = np.arange(n) / FS
cal = {"sine_440Hz": np.sin(2 * np.pi * 440 * tt), "white_noise": rng.standard_normal(n)}
xl = np.empty(n); xl[0] = 0.3
for i in range(1, n): xl[i] = 4 * xl[i - 1] * (1 - xl[i - 1])
cal["logistic_r4 (true lambda = ln2 per sample = 33271/s)"] = xl
res["lle_calibration"] = {k: lle_rosenstein(v, FS)["lle_per_s"] for k, v in cal.items()}
res["lle_calibration_logistic_dim1"] = lle_rosenstein(xl, FS, dim=2, tau=1, theiler=2, fit=(0, 4))["lle_per_s"]
# DFA calibration: white (alpha 0.5), 1/f (alpha 1), brown (alpha 1.5)
def colored(beta, n=2625):
    X = np.fft.rfft(rng.standard_normal(n)); f = np.fft.rfftfreq(n); f[0] = f[1]; return np.fft.irfft(X / f ** (beta / 2), n)
res["dfa_calibration"] = {f"beta{b}": dfa(colored(b))["alpha"] for b in (0, 1, 2)}
print(res["lle_calibration"], res["lle_calibration_logistic_dim1"], res["dfa_calibration"])
json.dump(res, open(os.path.join(ROOT, "experiments/results/phase2/exp3_coupling.json"), "w"), indent=1)
print("saved", time.time() - t0)
