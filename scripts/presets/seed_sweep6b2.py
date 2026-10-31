#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 MORPHO-PHS contributors
"""Phase 6b2 headroom sweep: render every FX preset with N noise seeds of the (portable) demo input and record, per
preset, how far the worst seed's sample peak lies above the canonical seed's (12345).  fit_gain6b.py uses that ratio so
the canonical render keeps enough headroom that no tested seed reaches the output limiter knee (0.9).

The FX demo input has Gaussian snare / hat noise, and its single-sample extremes set the peak of the Edge Disperser
presets (the peak lands on a snare hit).  Phase 6b used std::normal_distribution, which libstdc++ and the MSVC STL
implement differently, so the i9 saw another noise realisation than the box (Chiral Drift: 0.936 on the i9, 0.753 on
the box).  The `peak` in qa.json is measured after the output limiter, so a seed that drives the limiter reports a
compressed peak and the ratio comes out too small.  The ratio is therefore measured on probe copies of the presets with
out_gain lowered by --probe-db (default -12 dB; out_gain is a post gain before the limiter, so the ratio does not depend
on it), and the probe asserts that no seed reaches the limiter.  --verify renders the presets as they are (no probe) and
only reports the seeds that reach the limiter.

usage: seed_sweep6b2.py <cli> <presets_dir> <work_dir> [--seeds 24] [--jobs 6] [--only "101_,..."] [--probe-db -12]
                        [--verify] [--no-write]
writes <work_dir>/seed_sweep.json and the "seed_peak_ratio" field of scripts/presets/bank6b_measured.json
"""
import argparse, json, pathlib, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = pathlib.Path(__file__).resolve().parents[2]

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("cli"); ap.add_argument("presets"); ap.add_argument("work")
    ap.add_argument("--seeds", type=int, default=24); ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--only", default=",".join("%d_" % n for n in range(101, 200)))
    ap.add_argument("--probe-db", type=float, default=-12.0); ap.add_argument("--verify", action="store_true")
    ap.add_argument("--no-write", action="store_true"); a = ap.parse_args()
    work = pathlib.Path(a.work); work.mkdir(parents=True, exist_ok=True)
    if a.verify: a.no_write = True
    else:   # probe copies with lower out_gain: limiter-free peaks
        probe = work / "presets_probe"; probe.mkdir(exist_ok=True)
        keys = [k.strip().lower() for k in a.only.split(",") if k.strip()]
        for f in sorted(pathlib.Path(a.presets).glob("*.morpho")):
            pr = json.loads(f.read_text())
            if not any(k in f.name.lower() or k in pr.get("name", "").lower() for k in keys): continue
            pr["params"]["out_gain"] = pr["params"].get("out_gain", 0.0) + a.probe_db
            (probe / f.name).write_text(json.dumps(pr, indent=1, ensure_ascii=False))
        a.presets = str(probe)
    seeds = [12345] + list(range(1, a.seeds + 1))
    def one(s):
        d = work / ("s%d" % s)
        subprocess.run([a.cli, "qa", a.presets, str(d), "--play", "8", "--tail", "8", "--seed", str(s), "--no-wav", "1", "--only", a.only],
                       check=False, stdout=subprocess.DEVNULL)
        return s, json.loads((d / "qa.json").read_text())
    with ThreadPoolExecutor(a.jobs) as ex: res = dict(ex.map(one, seeds))
    out = {}
    for r in res[12345]:
        n = r["name"]; pk = {s: next(x["peak"] for x in res[s] if x["name"] == n) for s in seeds}
        lim = [s for s in seeds if next(x["limiter_samples"] for x in res[s] if x["name"] == n) > 0]
        worst = max(pk.values())
        out[n] = {"canonical_peak": pk[12345], "max_peak": worst, "worst_seed": max(pk, key=pk.get), "ratio": worst / max(pk[12345], 1e-9),
                  "seeds": len(seeds), "seeds_with_limiter": lim}
        print("%-22s canonical %.3f  max %.3f (seed %d)  ratio %.3f  limiter seeds %s" % (n, pk[12345], worst, out[n]["worst_seed"], out[n]["ratio"], lim or "-"))
    if not a.verify:
        bad = [n for n, v in out.items() if v["seeds_with_limiter"]]
        if bad: sys.exit("probe gain too high, the limiter engaged for: %s (use a lower --probe-db)" % ", ".join(bad))
    (work / "seed_sweep.json").write_text(json.dumps(out, indent=1) + "\n")
    if not a.no_write:
        mp = ROOT / "scripts/presets/bank6b_measured.json"; meas = json.loads(mp.read_text())
        for n, v in out.items(): meas.setdefault(n, {})["seed_peak_ratio"] = round(v["ratio"], 4)
        mp.write_text(json.dumps(meas, indent=1, ensure_ascii=False) + "\n")

if __name__ == "__main__":
    main()
