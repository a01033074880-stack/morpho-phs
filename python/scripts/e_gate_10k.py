# SPDX-License-Identifier: AGPL-3.0-only
"""Proposal gate: 10,000 random extreme shocks with zero NaN (8x8, 48 kHz, 2048 samples each)."""
import sys, os, json
sys.path.insert(0, os.path.dirname(__file__)); sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import numpy as np
from e_stress_test import run, ESAV, Literal, OUT, L, T
res = {}
SEL = os.environ.get("GATE", "ESAV:passive,ESAV:active,Literal:passive,Literal:active").split(",")
for cls in (ESAV, Literal):
    for scen in ("passive", "active"):
        if f"{cls.__name__}:{scen}" not in SEL: continue
        tot = 0; dead = 0; nan_any = 0
        for chunk in range(4):
            r, _ = run(cls, 2500, scen, seed=1000 + chunk)
            tot += r["n"]; dead += int(round(r["divergence_rate"] * r["n"]))
        res[f"{scen}|{cls.name}"] = dict(trials=tot, diverged=dead)
        print(scen, cls.name, res[f"{scen}|{cls.name}"], flush=True)
json.dump(res, open(f"{OUT}/e_gate10k_L{L}_{os.environ.get('GTAG','all')}.json", "w"), indent=1)
