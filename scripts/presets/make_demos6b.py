#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Phase 6b demo audio: trim the QA renders (morpho_cli qa, 8 s demo phrase + 8 s tail, float) to the audible part
(play + measured tail + 0.5 s, 8.5-16 s), 40 ms fade-out, TPDF-dithered 16-bit / 48 kHz WAV + Ogg Vorbis q5 + MP3 192k.
Loudness is not touched: the presets themselves are gain-fitted (instrument -18 LUFS-I, FX -20 LUFS-I = the dry input).
usage: make_demos6b.py <qa_dir with analysis.json> <out_dir>
"""
import json, pathlib, subprocess, sys
import numpy as np, soundfile as sf

qa, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
rng = np.random.default_rng(6)
rows = []
for a in json.load(open(qa / "analysis.json")):
    x, fs = sf.read(qa / a["wav"], dtype="float64", always_2d=True)
    dur = min(16.0, max(8.5, a["play_s"] + a["tail_s"] + 0.5))
    x = x[: int(dur * fs)].copy()
    nf = int(0.04 * fs); x[-nf:] *= np.linspace(1, 0, nf)[:, None] ** 2
    d = (rng.random(x.shape) - rng.random(x.shape)) / 32768.0
    y = np.clip(np.round((x + d) * 32767.0), -32768, 32767).astype(np.int16)
    stem = pathlib.Path(a["wav"]).stem
    w = out / f"{stem}.wav"; sf.write(w, y, fs, subtype="PCM_16")
    for ext, args in (("ogg", ["-c:a", "libvorbis", "-q:a", "5"]), ("mp3", ["-c:a", "libmp3lame", "-b:a", "192k"])):
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", str(w), *args, "-metadata", f"title={a['name']}",
                        "-metadata", "artist=MORPHO-PHS factory (Phase 6b2)", "-metadata", f"album={a['category']}", str(out / f"{stem}.{ext}")], check=True)
    rows.append({"name": a["name"], "category": a["category"], "file": stem, "seconds": round(len(y) / fs, 2), "lufs_i": a["lufs_i"],
                 "true_peak_dbtp": a["true_peak_dbtp"], "peak_limited": a["peak_limited"]})
(out / "demos.json").write_text(json.dumps(rows, indent=1, ensure_ascii=False))
print(len(rows), "demos ->", out)
