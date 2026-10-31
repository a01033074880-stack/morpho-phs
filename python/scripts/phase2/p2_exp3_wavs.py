# SPDX-License-Identifier: AGPL-3.0-only
"""Listening excerpts for exp3 (10 s after the 2 s transient, stereo, peak -1 dBFS each)."""
import os, numpy as np
from scipy.io import wavfile
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
for k in ("none_s0", "oneway_s0", "closed_s0", "closed_s0_k1.00"):
    x = np.fromfile(os.path.join(ROOT, f"experiments/results/phase2/exp3/{k}.f32"), np.float32).reshape(-1, 2)[2 * 48000:12 * 48000].astype(float)
    x = x / np.abs(x).max() * 0.891
    wavfile.write(os.path.join(ROOT, f"experiments/audio/phase2/exp3_{k}.wav"), 48000, (x * 32767).astype(np.int16)); print(k)
