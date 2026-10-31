# SPDX-License-Identifier: AGPL-3.0-only
"""Phase 2 ENPU A/B/C: run build/enpu_ab for 3 scenarios x variants {A reproject, B keep-state, C selective} at
block 64 and per sample (block 1, parameters updated every sample; spiral: linearly interpolated).  The common reference
is the per-sample variant-C render; each variant is also compared with its own per-sample render.
Metrics (per scenario, stereo output, first 0.25 s skipped):
  zipper_dB      : mean level of the tick-rate lines k*750 Hz (k=1..20) above the local spectral median (+-40 Hz), dB
  hf_dB_vs_ref   : energy above 12 kHz relative to the reference render (click / discontinuity folding), dB
  mrstft_to_ref  : multi-resolution STFT distance (SC + log-mag, FFT 256..4096) to the reference
  energy_removed : total energy removed by ENPU (J-equivalent, model units), from the C++ JSON
  sav_incons     : mean / max of sum|psi^2-2U-c0| / sum 2U at the ticks (0 = consistent SAV state)"""
import json, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
import numpy as np
from scipy import signal
from scipy.io import wavfile
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
RES = os.path.join(ROOT, "experiments/results/phase2/enpu"); AUD = os.path.join(ROOT, "experiments/audio/phase2")
os.makedirs(RES, exist_ok=True); os.makedirs(AUD, exist_ok=True)
FS = 48000; SECS = float(os.environ.get("SECS", "8")); SCEN = ["glide_g_alpha", "glide_mass", "spiral"]; VN = {0: "A_reproject", 1: "B_keepstate", 2: "C_selective"}
jobs = [(s, v, b) for s in range(3) for v in range(3) for b in (64, 1)]


def run(j):
    s, v, b = j
    r = subprocess.run([os.path.join(ROOT, "build/enpu_ab"), str(s), str(v), str(b), str(SECS)], cwd=ROOT, capture_output=True, text=True)
    return j, json.loads(r.stdout.strip().splitlines()[-1])


if os.environ.get("SKIP_RUN") != "1":
    with ThreadPoolExecutor(int(os.environ.get("PAR", "6"))) as ex:
        runs = dict(ex.map(run, jobs))
    json.dump({f"{SCEN[s]}_v{v}_b{b}": r for (s, v, b), r in runs.items()}, open(os.path.join(RES, "enpu_runs.json"), "w"), indent=1)
runs = json.load(open(os.path.join(RES, "enpu_runs.json")))


def load(s, v, b): return np.fromfile(os.path.join(RES, f"{SCEN[s]}_v{v}_b{b}.f32"), np.float32).astype(float).reshape(-1, 2)


def mrstft(x, y):
    tot = 0
    for n in (256, 512, 1024, 2048, 4096):
        X = np.abs(signal.stft(x, FS, nperseg=n, noverlap=3 * n // 4)[2]); Y = np.abs(signal.stft(y, FS, nperseg=n, noverlap=3 * n // 4)[2])
        tot += np.linalg.norm(Y - X) / np.linalg.norm(Y) + np.mean(np.abs(np.log(X + 1e-7) - np.log(Y + 1e-7)))
    return tot / 5


def zipper(x):
    f, P = signal.welch(x, FS, nperseg=1 << 15); lv = []
    for k in range(1, 21):
        fk = 750 * k; i = np.argmin(np.abs(f - fk)); nb = (np.abs(f - fk) < 40) & (np.abs(f - fk) > 3)
        lv.append(10 * np.log10(P[i - 1:i + 2].max() / np.median(P[nb])))
    return float(np.mean(lv)), float(np.max(lv))


def hf(x): f, P = signal.welch(x, FS, nperseg=4096); return float(np.sum(P[f > 12000]))


out = {}
sk = int(0.25 * FS)
for s, sn in enumerate(SCEN):
    ref = load(s, 2, 1)[sk:]; out[sn] = {"reference": {"zipper_mean_dB": np.mean([zipper(ref[:, c])[0] for c in range(2)]), **runs[f"{sn}_v2_b1"]}}
    peak = max(np.abs(load(s, v, 64)).max() for v in range(3)); peak = max(peak, np.abs(ref).max())
    wavfile.write(os.path.join(AUD, f"enpu_{sn}_REF_persample.wav"), FS, (load(s, 2, 1) / peak * 0.89 * 32767).astype(np.int16))
    for v in range(3):
        x = load(s, v, 64); xs = x[sk:]
        csv = np.genfromtxt(os.path.join(RES, f"{sn}_v{v}_b64.csv"), delimiter=",", names=True)
        zm = [zipper(xs[:, c]) for c in range(2)]
        out[sn][VN[v]] = {"finite": bool(np.all(np.isfinite(x))),
                          "zipper_mean_dB": float(np.mean([z[0] for z in zm])), "zipper_max_dB": float(np.max([z[1] for z in zm])),
                          "hf_dB_vs_ref": float(10 * np.log10(sum(hf(xs[:, c]) for c in range(2)) / sum(hf(ref[:, c]) for c in range(2)))),
                          "mrstft_to_ref": float(np.mean([mrstft(xs[:, c], ref[:, c]) for c in range(2)])),
                          "mrstft_to_own_persample": float(np.mean([mrstft(xs[:, c], load(s, v, 1)[sk:, c]) for c in range(2)])),
                          "hf_dB_vs_own_persample": float(10 * np.log10(sum(hf(xs[:, c]) for c in range(2)) / sum(hf(load(s, v, 1)[sk:, c]) for c in range(2)))),
                          "zipper_own_persample_dB": float(np.mean([zipper(load(s, v, 1)[sk:, c])[0] for c in range(2)])),
                          "rms_dB_vs_ref": float(20 * np.log10(np.sqrt(np.mean(xs ** 2)) / np.sqrt(np.mean(ref ** 2)))),
                          "sav_incons_mean": float(np.mean(csv["sav_inconsistency"])), "sav_incons_max": float(np.max(csv["sav_inconsistency"])),
                          "ticks_scaled_frac": float(np.mean(csv["s"] < 1)), "min_s": float(np.min(csv["s"])),
                          **{k: runs[f"{sn}_v{v}_b64"][k] for k in ("enpu_calls", "enpu_scaled", "fallback", "energy_removed_by_enpu", "Emax")}}
        wavfile.write(os.path.join(AUD, f"enpu_{sn}_{VN[v]}.wav"), FS, (np.nan_to_num(x) / peak * 0.89 * 32767).astype(np.int16))
json.dump(out, open(os.path.join(RES, "enpu_ab.json"), "w"), indent=1, default=float)
for sn in SCEN:
    for k, r in out[sn].items():
        if k == "reference": continue
        print(f"{sn:14s} {k:12s} mrOwn {r['mrstft_to_own_persample']:.4f} hfOwn {r['hf_dB_vs_own_persample']:6.2f} zip {r['zipper_mean_dB']:6.2f} hf {r['hf_dB_vs_ref']:6.2f} mr {r['mrstft_to_ref']:.4f} rms {r['rms_dB_vs_ref']:6.2f} "
              f"rem {r['energy_removed_by_enpu']:.3e} incons {r['sav_incons_mean']:.2e}/{r['sav_incons_max']:.2e} scaled {r['ticks_scaled_frac']:.2f}")
