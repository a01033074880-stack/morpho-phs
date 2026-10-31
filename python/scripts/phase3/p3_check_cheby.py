# SPDX-License-Identifier: AGPL-3.0-only
"""Compare the C++ Cheby-II in-gap bank (tools/check_cheby.cpp) with scipy.signal.cheby2(6, 80, [lo, hi], 'bandpass')."""
import json, os, subprocess, numpy as np
from scipy import signal
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
out = subprocess.run([os.path.join(ROOT, "build/p3/check_cheby")], capture_output=True, text=True, check=True).stdout
d = np.array([[float(v) for v in l.split(",")] for l in out.strip().splitlines()])
gaps = [(448, 1104), (1500, 2229), (2400, 2794)]; res = {}
for g, (lo, hi) in enumerate(gaps):
    sos = signal.cheby2(6, 80, [lo, hi], "bandpass", fs=48000, output="sos")
    sel = d[d[:, 0] == g]; f = sel[:, 1]
    _, H = signal.sosfreqz(sos, worN=f, fs=48000); ref = 20 * np.log10(np.abs(H) + 1e-300)
    m = ref > -100   # compare where both are above numerical floor
    res[f"gap{g+1}_{lo}_{hi}Hz"] = {"max_abs_dB_diff_where_ref>-100dB": float(np.max(np.abs(sel[m, 2] - ref[m]))),
                                    "passband_peak_dB": float(sel[:, 2].max()), "stopband_max_dB_outside_edges": float(sel[(f < lo * 0.98) | (f > hi * 1.02), 2].max())}
json.dump(res, open(os.path.join(ROOT, "experiments/results/phase3/cheby_bank_check.json"), "w"), indent=1); print(json.dumps(res, indent=1))
