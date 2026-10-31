# SPDX-License-Identifier: AGPL-3.0-only
"""Phase 2: fixed chiral-edge demos.  Renders raw pickups with build/render_edge_v2 (C++, ESAV-LF, 48 kHz) and applies
the in-gap fix: a parallel bank of steep Chebyshev-II band-passes (order 12 = 6 biquads each, 80 dB stopband from the gap edges) over the bulk gaps that
carry chiral edge modes (Chern sum != 0); choice justified by exp2_analysis.json filter_options.  Causal sosfilt (= real-time biquads).
Outputs WAVs 08-11 in experiments/audio/phase2/ and experiments/results/phase2/edge_v2/edge_v2_metrics.json"""
import json, os, subprocess
import numpy as np
from scipy import signal
from scipy.io import wavfile
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
RES = os.path.join(ROOT, "experiments/results/phase2/edge_v2"); AUD = os.path.join(ROOT, "experiments/audio/phase2")
os.makedirs(RES, exist_ok=True); os.makedirs(AUD, exist_ok=True)
FS = 48000
top = json.load(open(os.path.join(ROOT, "experiments/results/phase1/p1_b_topology.json")))
GAPS = [(g["lo"], g["hi"]) for g in top["gaps_Hz"] if g.get("chern_sum_below", 0) != 0]
SOS = [signal.cheby2(6, 80, [lo, hi], "bandpass", fs=FS, output="sos") for lo, hi in GAPS]   # chosen in exp2 filter_options
cfgs = [(+1, 1, 8), (0, 1, 8), (-1, 1, 8), (+1, 0, 8), (0, 0, 8), (-1, 0, 8)]
meta = {}
for s, a, d in cfgs:
    o = subprocess.run([os.path.join(ROOT, "build/render_edge_v2"), str(s), str(a), str(d)], cwd=ROOT, capture_output=True, text=True, check=True).stdout
    meta[f"s{s:+d}_a{a}_d{d}"] = json.loads(o)


def load(s, a, d): return np.fromfile(os.path.join(RES, f"raw_s{s:+d}_a{a}_d{d}.f32"), np.float32).astype(float).reshape(-1, 2)


def gapbank(x): return sum(signal.sosfilt(sos, x, axis=0) for sos in SOS)


def rl(x): return float(10 * np.log10(np.sum(x[:, 1] ** 2) / np.sum(x[:, 0] ** 2)))


out = {"gaps_Hz": GAPS, "filter": "parallel Chebyshev-II band-pass, order 12 (6 biquads) per gap, 80 dB stopband starting at the gap edges", "renders": meta, "metrics": {}}
for s, a, d in cfgs:
    x = load(s, a, d); g = gapbank(x); r = x - g
    out["metrics"][f"s{s:+d}_a{a}_d{d}"] = {"absorber": ["sponge", "frame"][a], "raw_R_over_L_dB": rl(x), "ingap_R_over_L_dB": rl(g), "residual_outgap_R_over_L_dB": rl(r),
                                            "ingap_energy_fraction": float(np.sum(g ** 2) / np.sum(x ** 2))}


def wav(name, y):
    y = y / (np.max(np.abs(y)) + 1e-12) * 0.891; wavfile.write(os.path.join(AUD, name), FS, (y * 32767).astype(np.int16))


files = {}
for s, tag in ((+1, "OmegaPlus"), (-1, "OmegaMinus"), (0, "Omega0_reference")):
    g = gapbank(load(s, 1, 8)); fn = f"{ {+1: '08', -1: '09', 0: '10'}[s] }_edge_v2_frame_ingap_{tag}.wav".replace(" ", ""); wav(fn, g); files[fn] = f"s{s:+d}_a1_d8 in-gap bank"
x = load(+1, 1, 8); g = gapbank(x); r = x - g; mono = 0.5 * (r[:, 0] + r[:, 1])
y = g + mono[:, None]; wav("11_edge_v2_bandsplit_OmegaPlus.wav", y); files["11_edge_v2_bandsplit_OmegaPlus.wav"] = "in-gap part stereo (directional) + out-of-gap residual mono"
out["metrics"]["11_bandsplit_OmegaPlus"] = {"R_over_L_dB": rl(y)}
wav("12_edge_v2_frame_raw_OmegaPlus.wav", x); files["12_edge_v2_frame_raw_OmegaPlus.wav"] = "frame absorber only, no filter"
out["files"] = files
json.dump(out, open(os.path.join(RES, "edge_v2_metrics.json"), "w"), indent=1)
for k, v in out["metrics"].items(): print(k, v)
