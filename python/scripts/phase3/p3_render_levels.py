# SPDX-License-Identifier: AGPL-3.0-only
"""Per-channel level / clipping audit of all Phase-3 renders -> experiments/results/phase3/render_levels.{json,txt}."""
import glob, json, os
import numpy as np
from scipy.io import wavfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
rows = []
for f in sorted(glob.glob(os.path.join(ROOT, "experiments/audio/phase3/**/*.wav"), recursive=True)):
    fs, x = wavfile.read(f)
    x = x.astype(np.float64) / (2.0**31 if x.dtype == np.int32 else (32768.0 if x.dtype == np.int16 else 1.0))
    if x.ndim == 1: x = x[:, None]
    ch = []
    for c in range(x.shape[1]):
        v = x[:, c]; a = np.abs(v)
        ch.append({"peak_dbfs": float(20 * np.log10(max(a.max(), 1e-12))), "rms_dbfs": float(20 * np.log10(max(np.sqrt((v**2).mean()), 1e-12))),
                   "frac_above_0p9": float((a > 0.9).mean()), "finite": bool(np.isfinite(v).all())})
    rows.append({"file": os.path.relpath(f, ROOT), "fs": fs, "seconds": len(x) / fs, "channels": ch,
                 "limiter_engaged": any(c["frac_above_0p9"] > 1e-3 for c in ch)})
out = os.path.join(ROOT, "experiments/results/phase3/render_levels")
json.dump(rows, open(out + ".json", "w"), indent=1)
with open(out + ".txt", "w") as t:
    t.write("file  | L peak/rms dBFS  >0.9 | R peak/rms dBFS  >0.9 | flag\n")
    for r in rows:
        c = r["channels"]; cr = c[1] if len(c) > 1 else c[0]
        t.write("%-70s %6.1f/%6.1f %7.4f | %6.1f/%6.1f %7.4f | %s\n" % (r["file"], c[0]["peak_dbfs"], c[0]["rms_dbfs"], c[0]["frac_above_0p9"],
                cr["peak_dbfs"], cr["rms_dbfs"], cr["frac_above_0p9"], "LIMITER>0.1%" if r["limiter_engaged"] else ""))
print(open(out + ".txt").read())
print("files", len(rows), "flagged", sum(r["limiter_engaged"] for r in rows))
