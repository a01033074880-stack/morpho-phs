#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""CPU classes for the factory bank.

Phase 6b2: the i9 measurement is authoritative.  `--i9 <qa.json>` is a `morpho_cli qa --cost` run on the target PC
(i9-13900HX, MSVC build); a preset whose i9 row has the same lattice / effective potential / thread count as the box
row takes its class from the measured i9 p99 / budget ("i9 measured").  Presets without such a row (new or redesigned
since that run) get the conservative estimate below ("estimated, re-run on the i9").

Conservative estimate (6b2): est_i9_p99_us = box_median_us * k_group * margin, where k_group is the WORST i9 p99 /
box median seen in the 6b i9 run for the same configuration group (lattice, potential, threads), margin 1.10, or 1.25
for spiral / Barkley-heavy content (spiral seed, morph >= 0.5 or life > 0).  32x32 + spiral uses the measured
Big Spiral 32 factor as a floor (its serial control path gave a p99 3.9x its median on the i9).
The Phase 6b model (box -> i9 median ratio x tail from one Phase 5 run, per configuration) was off in both directions:
it overestimated plain 32x32 HQ fast (Cathedral Gyro 32: 0.856 estimated, 0.411 measured: the Phase 5 tail 3.32 came
from a bad run; the 6b i9 tail is 1.78) and underestimated spiral / excitable and freezer-room content at 16x16
(i9 / box median 1.27-1.43 instead of 1.106, tails 2.7-3.4 instead of 1.85) and Big Spiral 32 (0.888 -> 1.226).

classes: eco < 0.20, standard < 0.50, high >= 0.50 of the budget (20.83 us/sample at 48 kHz).

usage: cpu6b.py <box qa.json> [--i9 experiments/results/phase6b2/i9/qa.json] [--override DIR ...] [--out DIR]
"""
import argparse, json, pathlib, csv
from collections import Counter, defaultdict

BUDGET = 128 / 48000 * 1e6 / 128   # 20.83 us per sample
ROOT = pathlib.Path(__file__).resolve().parents[2]
# 6b i9 run vs 6b box bank run (the reference pair the group factors come from)
REF_BOX = ROOT / "experiments/results/phase6b2/box6b_qa.json"   # frozen copy of the 6b box bank run
REF_I9 = ROOT / "experiments/results/phase6b2/i9/qa.json"
MARGIN, MARGIN_HEAVY = 1.10, 1.25

def group(e): return (e["lattice"], 0 if e["potential_effective"] == 0 else 1, e["cost_threads"])
def gname(g): return "L%d_%s_%dT" % (g[0], "q" if g[1] == 0 else "hq", g[2])

def heavy(preset):
    p = preset["params"]; ls = preset.get("lattice_state", {})
    return bool(ls.get("seed_spiral")) or float(p.get("morph", 0)) >= 0.5 or float(p.get("life", 0)) > 0

def load_presets():
    out = {}
    for f in (ROOT / "presets").glob("*.morpho"):
        p = json.loads(f.read_text()); out[p["name"]] = p
    return out

def group_factors():
    box = {e["name"]: e for e in json.load(open(REF_BOX))}; i9 = {e["name"]: e for e in json.load(open(REF_I9))}
    k = defaultdict(list); spiral32 = []
    for n, e in i9.items():
        b = box.get(n)
        if not b or group(b) != group(e): continue
        kk = e["cost_p99_over_budget"] * BUDGET / b["cost_us_per_sample"]; k[group(e)].append(kk)
        if e["lattice"] >= 32 and "Spiral" in n: spiral32.append(kk)
    return {g: max(v) for g, v in k.items()}, (max(spiral32) if spiral32 else None)

def nearest(g, K):
    if g in K: return K[g], gname(g)
    cand = [h for h in K if h[0] == g[0] and h[1] == g[1]] or [h for h in K if h[0] == g[0]] or list(K)
    h = max(cand, key=lambda h: K[h]); return K[h], gname(h) + " (nearest)"

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("qa"); ap.add_argument("--override", nargs="*", default=[])
    ap.add_argument("--i9", default=str(REF_I9)); ap.add_argument("--out", default="experiments/results/phase6b/cpu")
    ap.add_argument("--measured", default="scripts/presets/bank6b_measured.json"); a = ap.parse_args()
    rows = {e["name"]: dict(e, source="box bank run %.0f s" % e.get("cost_seconds", 0)) for e in json.load(open(a.qa))}
    for d in a.override:
        for q in pathlib.Path(d).rglob("qa.json"):
            for e in json.load(open(q)):
                if e["name"] in rows and e.get("cost_seconds", 0) >= rows[e["name"]].get("cost_seconds", 0):
                    rows[e["name"]] = dict(e, source=f"box re-run {e['cost_seconds']:.0f} s")
    i9 = {e["name"]: e for e in json.load(open(a.i9))} if a.i9 and pathlib.Path(a.i9).exists() else {}
    K, k_sp32 = group_factors(); P = load_presets()
    out = []
    for n, e in rows.items():
        g = group(e); kg, gsrc = nearest(g, K)
        hv = heavy(P[n]) if n in P else False
        k = kg * (MARGIN_HEAVY if hv else MARGIN)
        if e["lattice"] >= 32 and P.get(n, {}).get("lattice_state", {}).get("seed_spiral") and k_sp32: k = max(k, k_sp32 * MARGIN_HEAVY)
        est = e["cost_us_per_sample"] * k / BUDGET
        m = i9.get(n)
        if m and group(m) == g and m.get("cost_seconds", 0) > 0:
            load, src = m["cost_p99_over_budget"], "i9 measured"
            i9_med, i9_over, i9_blocks = m["cost_us_per_sample"], m["cost_overruns"], m["cost_blocks"]
        else:
            load, src = est, "estimated (re-run on the i9)"; i9_med = i9_over = i9_blocks = None
        cls = "eco" if load < 0.2 else ("standard" if load < 0.5 else "high")
        out.append(dict(name=n, file=e["file"], mode=e["mode"], L=e["lattice"], threads=e["cost_threads"], potential=e["potential_effective"],
                        group=gname(g), heavy=hv, box_us=round(e["cost_us_per_sample"], 3), box_p99_over_budget=round(e["cost_p99_over_budget"], 3),
                        box_overruns=e["cost_overruns"], box_blocks=e["cost_blocks"], k_group=round(kg, 2), k_group_src=gsrc, k_used=round(k, 2),
                        est_i9_p99_over_budget=round(est, 3), i9_median_us=(round(i9_med, 2) if i9_med else None),
                        i9_overruns=i9_over, i9_blocks=i9_blocks, i9_p99_over_budget=round(load, 3), cpu_source=src, cpu_class=cls, box_source=e["source"]))
    out.sort(key=lambda x: x["file"])
    od = pathlib.Path(a.out); od.mkdir(parents=True, exist_ok=True)
    (od / "cpu_table.json").write_text(json.dumps(out, indent=1, ensure_ascii=False))
    with open(od / "cpu_table.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(out[0].keys())); w.writeheader(); w.writerows(out)
    (od / "group_factors.json").write_text(json.dumps({gname(g): round(v, 3) for g, v in sorted(K.items())} | {"L32_spiral_floor": k_sp32, "margin": MARGIN, "margin_heavy": MARGIN_HEAVY}, indent=1))
    mp = pathlib.Path(a.measured); m = json.loads(mp.read_text())
    for x in out: m.setdefault(x["name"], {})["cpu_class"] = x["cpu_class"]
    mp.write_text(json.dumps(m, indent=1, ensure_ascii=False) + "\n")
    print(Counter(x["cpu_class"] for x in out), {gname(g): round(v, 2) for g, v in sorted(K.items())}, "spiral32", k_sp32)
    for x in out: print(f"{x['name']:<22} {x['group']:<10} box {x['box_us']:6.3f}  est {x['est_i9_p99_over_budget']:.3f}  i9 {x['i9_p99_over_budget']:.3f} [{x['cpu_source']}]  {x['cpu_class']}")

if __name__ == "__main__": main()
