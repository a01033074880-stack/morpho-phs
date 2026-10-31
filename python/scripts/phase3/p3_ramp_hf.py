# SPDX-License-Identifier: AGPL-3.0-only
"""Phase 3: HF artefact of block-rate g/mu steps vs the in-kernel staircase ramp (interp_step 8 / 1).
Same metric as Phase 2 (p2_enpu_ab.py): Welch energy above 12 kHz relative to the per-sample-interpolated reference."""
import json, os, numpy as np
from scipy import signal
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
D = os.path.join(ROOT, "experiments/results/phase3/ramp"); FS = 48000
def load(n): return np.fromfile(os.path.join(D, n), np.float32).astype(float).reshape(-1, 2)[FS // 2:]
def hf(x): f, P = signal.welch(x, FS, nperseg=4096); return float(np.sum(P[f > 12000]))
ref = load("spiral_v2_b1.f32"); out = {"reference": "spiral_v2_b1 (ENPU-C every sample, linear g/mu interpolation)", "skip_s": 0.5}
for nm, f in (("block64_step", "spiral_blk64_i0.f32"), ("block64_ramp8", "spiral_blk64_i8.f32"), ("block64_ramp1", "spiral_blk64_i1.f32")):
    x = load(f)
    out[nm] = {"file": f, "hf_dB_vs_ref": 10 * np.log10(sum(hf(x[:, c]) for c in range(2)) / sum(hf(ref[:, c]) for c in range(2))),
               "rms_dB_vs_ref": 10 * np.log10(np.mean(x ** 2) / np.mean(ref ** 2))}
json.dump(out, open(os.path.join(ROOT, "experiments/results/phase3/ramp_hf.json"), "w"), indent=1); print(json.dumps(out, indent=1))
