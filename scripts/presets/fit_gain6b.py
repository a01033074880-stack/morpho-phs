#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 MORPHO-PHS contributors
"""Phase 6b loudness normalisation: iterate bank6b.py -> morpho_cli qa -> qa_analyze.py and set each preset's
out_gain so the standard demo hits the target (instrument -18 LUFS-I, FX -20 LUFS-I = the dry demo input) while the
true peak stays <= -1 dBTP (the output safety limiter starts at 0.9 = -0.9 dBFS and must never engage).

usage: fit_gain6b.py <cli> <work_dir> [--iters 3]
"""
import argparse, json, pathlib, subprocess, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent)); import qa_analyze   # 6b2 headroom rule

ROOT = pathlib.Path(__file__).resolve().parents[2]
PY = sys.executable

def run(cli, work, it):
    bank = work / ("bank_it%d" % it); qa = work / ("qa_it%d" % it)
    subprocess.run([PY, str(ROOT / "scripts/presets/bank6b.py"), "--out", str(bank)], check=True)
    subprocess.run([cli, "qa", str(bank), str(qa), "--play", "8", "--tail", "8"], check=False, stdout=subprocess.DEVNULL)
    subprocess.run([PY, str(ROOT / "scripts/presets/qa_analyze.py"), str(qa)], check=False, stdout=subprocess.DEVNULL)
    return json.loads((qa / "analysis.json").read_text())

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("cli"); ap.add_argument("work"); ap.add_argument("--iters", type=int, default=3)
    a = ap.parse_args(); work = pathlib.Path(a.work); work.mkdir(parents=True, exist_ok=True)
    mp = ROOT / "scripts/presets/bank6b_measured.json"
    meas = json.loads(mp.read_text()) if mp.exists() else {}
    for it in range(a.iters):
        rows = run(a.cli, work, it); changed = 0
        for r in rows:
            m = meas.setdefault(r["name"], {}); g = m.get("out_gain", 0.0)
            tgt = r["lufs_target"]
            d = tgt - r["lufs_i"]
            head = -1.3 - r["true_peak_dbtp"]          # dB we may still add before -1.3 dBTP
            # Phase 6b2: and before the headroom ceiling (qa_analyze.allowed_peak, aim 0.1 dB under it)
            head = min(head, qa_analyze.db(qa_analyze.allowed_peak(r["name"], meas)) - 0.1 - r["peak_dbfs"])
            if r["limiter_samples"] > 0: head = min(head, -1.5)
            step = min(d, head) if d > 0 else min(d, head)
            if d < 0 and head < d: step = head
            if abs(step) >= 0.2 or (step < 0 and head < 0):
                m["out_gain"] = round(max(-24.0, min(24.0, g + step)), 2); changed += 1
        mp.write_text(json.dumps(meas, indent=1, ensure_ascii=False) + "\n")
        bad = [r for r in rows if r["fails"]]
        print("iter %d: %d gain change(s); %d preset(s) with failures" % (it, changed, len(bad)))
        for r in bad: print("   %-24s %s" % (r["name"], ", ".join(r["fails"])))
        if changed == 0: break

if __name__ == "__main__":
    main()
