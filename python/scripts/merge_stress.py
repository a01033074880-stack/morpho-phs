# SPDX-License-Identifier: AGPL-3.0-only
"""Merge per-scheme stress results (SCHEME=... runs) into e_stress_L{L}.json and draw e1/e2 figures."""
import sys, os, json, glob
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
from e_stress_test import run, ESAV, Literal, RK4, IMDG, Verlet, OUT, FIG, L, T, FS
res = {}; perE = {}
for f in sorted(glob.glob(f"{OUT}/e_stress_L{L}_*.json")):
    for k, r in json.load(open(f)).items():
        if "_E0" in r: perE[k.split("|")[1]] = (np.array(r.pop("_E0")), np.array(r.pop("_dead"), bool))
        res[k] = r
json.dump(res, open(f"{OUT}/e_stress_L{L}.json", "w"), indent=1)
fig, ax = plt.subplots(figsize=(7, 4)); bins = np.linspace(0, 16, 17)
for name, (E0, dead) in perE.items():
    idx = np.digitize(np.log10(E0), bins)
    rate = [dead[idx == k].mean() if (idx == k).sum() > 3 else np.nan for k in range(1, len(bins))]
    ax.plot(0.5 * (bins[1:] + bins[:-1]), rate, "o-", ms=3, label=name)
ax.set_xlabel("log10 initial shock energy"); ax.set_ylabel("divergence rate")
ax.set_title(f"{L}×{L} lattice, shocks + 4 impulses, μ=0, η>0, Φ=π/2, 48 kHz, {T} samples", fontsize=9); ax.legend(fontsize=7)
fig.tight_layout(); fig.savefig(f"{FIG}/e1_divergence_vs_shock_L{L}.png", dpi=130); plt.close(fig)
fig, ax = plt.subplots(1, 2, figsize=(12, 4))
for cls, c in ((Literal, "r"), (RK4, "g"), (IMDG, "m"), (ESAV, "b")):
    for j, scen in enumerate(("pump_lossless", "pump")):
        _, tr = run(cls, 2, scen, seed=3, record=True); tr = tr[:, 0]
        ax[j].semilogy(np.arange(T) / FS * 1e3, tr / tr[0], c, lw=.8, label=cls.name)
for j, tt in enumerate(("μ=0, η=0 (proposal: lossless)", "μ=0, η>0 (proposal: passive)")):
    ax[j].set_title(f"m,α switched at 750 Hz (square wave), {tt}", fontsize=9); ax[j].set_xlabel("ms"); ax[j].set_ylabel("E/E0"); ax[j].legend(fontsize=7)
fig.tight_layout(); fig.savefig(f"{FIG}/e2_parametric_pumping_energy_L{L}.png", dpi=130)
for k, r in res.items(): print(k, {a: b for a, b in r.items() if a in ("divergence_rate", "n", "max_energy_ratio", "passivity_violation_rate", "max_single_step_rel_increase", "us_per_step_per_trial", "lu_refactorizations", "newton_iters_mean", "newton_iters_max", "newton_fail_rate")})
