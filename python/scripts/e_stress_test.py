# SPDX-License-Identifier: AGPL-3.0-only
"""Audit (c,d,e): stress tests of the literal SSDG vs baselines vs corrected ESAV-LF.
Scenarios on the proposal's scalar square lattice (Phi = pi/2)."""
import sys, os, json, time
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import numpy as np
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
from diffphs.model import square_lattice
from diffphs.integrators import Params, RK4, Verlet, Literal, IMDG, ESAV
from diffphs.audio_params import *
np.seterr(all="ignore")
FIG = os.path.join(os.path.dirname(__file__), "../../reports/figures")
OUT = os.path.join(os.path.dirname(__file__), "../../experiments/results"); os.makedirs(OUT, exist_ok=True)
L = int(os.environ.get("L", 8)); BIG = int(os.environ.get("BIG", 2000)); BN = int(os.environ.get("BN", 100))
T = int(os.environ.get("T", 2048)); BLK = 64
lat = square_lattice(L, K0, KC, SIGMA, np.pi / 2); N = lat.N


def shocks(B, seed):
    rng = np.random.default_rng(seed)
    q0 = 0.01 * rng.standard_normal((N, B)); p0 = 0.01 * rng.standard_normal((N, B))
    node = rng.integers(0, N, B); kind = rng.random(B) < 0.5
    vmag = 10 ** rng.uniform(1, 6, B) * rng.choice([-1, 1], B)
    qmag = 10 ** rng.uniform(-1, 2, B) * rng.choice([-1, 1], B)
    p0[node[kind], np.where(kind)[0]] += vmag[kind] * M0
    q0[node[~kind], np.where(~kind)[0]] += qmag[~kind]
    imp = []
    for k in range(4):  # 4 further random force impulses per trial (dv = a*h up to ~2e4)
        t = rng.integers(64, T - 64, B); nd = rng.integers(0, N, B)
        a = 10 ** rng.uniform(4, 9, B) * rng.choice([-1, 1], B)
        imp.append((t, nd, a))
    return q0, p0, imp


def run(scheme, B, scenario, seed=0, record=False):
    q0, p0, imp = shocks(B, seed)
    rng = np.random.default_rng(seed + 99)
    mu = {"free_passive": 0.0, "passive": 0.0, "active": MU0 * 10 ** rng.uniform(-1, 1.5, B), "pump": 0.0, "pump_lossless": 0.0}[scenario]
    eta = 0.0 if scenario == "pump_lossless" else ETA0
    if scenario == "active" and issubclass(scheme, Literal):
        mu = MU0 * 3.0  # (any value: Bernoulli step is per-node; uniform keeps code simple)
    P = Params(N, B, m=M0, alpha=ALPHA0, beta=BETA0, mu=mu, eta=eta)
    if scenario == "free_passive":
        imp = []
    if scenario.startswith("pump"):  # small physical initial state; energy may only change via modulation
        q0 = 0.05 * np.random.default_rng(seed).standard_normal((N, B)); p0 = np.zeros((N, B)); imp = []
    s = scheme(lat, H, P, q0, p0)
    E0 = s.energy().copy(); Emax = E0.copy(); dead = np.zeros(B, bool)
    Eprev = E0.copy(); incr = np.zeros(B, bool); maxinc = np.zeros(B)
    trace = []; t0 = time.time()
    for n in range(T):
        if scenario.startswith("pump") and n % BLK == 0:
            u = 0.5 * (1 + (-1) ** (n // BLK))  # 750 Hz control rate, 375 Hz square modulation
            Pn = s.P.copy(); Pn.m[:] = M0 * (1 + 0.8 * u); Pn.alpha[:] = ALPHA0 * np.exp(1.0 * u)
            s.set_params(Pn)
        f = 0.0
        if imp:
            f = np.zeros((N, B))
            for (tt, nd, a) in imp:
                hit = tt == n
                if hit.any(): f[nd[hit], np.where(hit)[0]] += a[hit]
        s.step(f)
        q, p = s.state()
        newdead = (~np.isfinite(q).all(0) | (np.abs(q).max(0) > 1e4)) & ~dead
        dead |= newdead
        if newdead.any():  # zero diverged columns so inf/NaN/denormal arithmetic does not slow the batch
            for a in ("q", "p", "qm", "psi", "psim"):
                if hasattr(s, a) and np.ndim(getattr(s, a)) == 2: getattr(s, a)[:, dead] = 0.0
        E = s.energy(); Emax = np.where(np.isfinite(E), np.maximum(Emax, E), Emax)
        if scenario == "free_passive":
            rel = (E - Eprev) / np.abs(Eprev)
            incr |= (rel > 1e-9) & np.isfinite(rel); maxinc = np.maximum(maxinc, np.where(np.isfinite(rel), rel, 0))
            Eprev = E
        if record: trace.append(E.copy())
    wall = time.time() - t0
    Ef = s.energy(); ok = ~dead
    out = dict(divergence_rate=float(dead.mean()), n=B,
               max_energy_ratio=float(np.max(Emax[ok] / E0[ok])) if ok.any() else None,
               final_energy_ratio_median=float(np.median(Ef[ok] / E0[ok])) if ok.any() else None,
               wall_s=round(wall, 2), us_per_step_per_trial=1e6 * wall / T / B)
    if scenario == "free_passive":
        out.update(passivity_violation_rate=float(incr[ok].mean()) if ok.any() else None,
                   max_single_step_rel_increase=float(maxinc[ok].max()) if ok.any() else None)
    out["_E0"] = E0; out["_dead"] = dead
    if hasattr(s, "fail"): out.update(newton_fail_rate=float(s.fail.mean()), newton_iters_mean=float(np.mean(s.iters)), newton_iters_max=int(np.max(s.iters)))
    if getattr(s, "jac_sweeps", None): out.update(jacobi_sweeps_mean=float(np.mean(s.jac_sweeps)), jacobi_sweeps_max=int(np.max(s.jac_sweeps)))
    if hasattr(s, "nfactor"): out["lu_refactorizations"] = s.nfactor
    return out, (np.array(trace) if record else None)


class LitPrinted(Literal):
    name = "SSDG literal (printed Bernoulli formula)"
    def __init__(self, *a, **k): super().__init__(*a, literal_bernoulli=True, **k)


if __name__ == "__main__" and os.environ.get("SCHEME"):
    # parallel mode: one scheme per process; raw arrays saved for merge_stress.py
    cls = {c.__name__: c for c in (ESAV, Literal, RK4, Verlet, IMDG, LitPrinted)}[os.environ["SCHEME"]]
    B = BN if cls is IMDG else BIG; results = {}
    for scen in ("free_passive", "passive", "active", "pump", "pump_lossless"):
        if cls is LitPrinted and scen != "passive": continue
        Bx = B if not scen.startswith("pump") else min(B, 64)
        r, _ = run(cls, Bx, scen, seed=1)
        E0, dead = r.pop("_E0"), r.pop("_dead")
        if scen == "passive": r["_E0"] = E0.tolist(); r["_dead"] = dead.tolist()
        results[f"{scen}|{cls.name}"] = r
        print(scen, "|", cls.name, json.dumps({k: v for k, v in r.items() if not k.startswith("_")}), flush=True)
    json.dump(results, open(f"{OUT}/e_stress_L{L}_{cls.__name__}.json", "w"))
    sys.exit(0)

if __name__ == "__main__":
    results = {}
    schemes = [(ESAV, BIG), (Literal, BIG), (RK4, BIG), (Verlet, BIG), (IMDG, BN)]
    perE = {}
    for scen in ("free_passive", "passive", "active", "pump", "pump_lossless"):
        for cls, B in schemes:
            Bx = B if not scen.startswith("pump") else min(B, 64)
            r, _ = run(cls, Bx, scen, seed=1)
            E0, dead = r.pop("_E0"), r.pop("_dead")
            if scen == "passive": perE[cls.name] = (E0, dead)
            results[f"{scen}|{cls.name}"] = r
            print(scen, "|", cls.name, json.dumps(r), flush=True)
    r, _ = run(LitPrinted, min(BIG, 200), "passive", seed=1); r.pop("_E0"); r.pop("_dead")
    results["passive|" + LitPrinted.name] = r; print("passive |", LitPrinted.name, r, flush=True)
    json.dump(results, open(f"{OUT}/e_stress_L{L}.json", "w"), indent=1)
    fig, ax = plt.subplots(figsize=(7, 4))
    bins = np.linspace(0, 16, 17)
    for name, (E0, dead) in perE.items():
        lg = np.log10(E0); idx = np.digitize(lg, bins)
        rate = [dead[idx == k].mean() if (idx == k).sum() > 3 else np.nan for k in range(1, len(bins))]
        ax.plot(0.5 * (bins[1:] + bins[:-1]), rate, "o-", ms=3, label=name)
    ax.set_xlabel("log10 initial shock energy (limit-cycle energy ~ 3e4 per node)"); ax.set_ylabel("divergence rate")
    ax.set_title(f"{L}×{L} lattice, shocks + 4 impulses, μ=0, η>0, Φ=π/2, 48 kHz, {T} samples", fontsize=9); ax.legend(fontsize=7)
    fig.tight_layout(); fig.savefig(f"{FIG}/e1_divergence_vs_shock_L{L}.png", dpi=130); plt.close(fig)
    fig, ax = plt.subplots(1, 2, figsize=(12, 4))
    for cls, c in ((Literal, "r"), (RK4, "g"), (IMDG, "m"), (ESAV, "b")):
        for j, scen in enumerate(("pump_lossless", "pump")):
            _, tr = run(cls, 4 if cls is not IMDG else 2, scen, seed=3, record=True)
            tr = tr[:, 0]
            ax[j].semilogy(np.arange(T) / FS * 1e3, tr / tr[0], c, lw=.8, label=cls.name)
    for j, tt in enumerate(("μ=0, η=0 (proposal: lossless)", "μ=0, η>0 (proposal: passive)")):
        ax[j].set_title(f"m,α switched at 750 Hz (square wave), {tt}", fontsize=9); ax[j].set_xlabel("ms"); ax[j].set_ylabel("E/E0")
        hh, ll = ax[j].get_legend_handles_labels(); d = dict(zip(ll, hh)); ax[j].legend(d.values(), d.keys(), fontsize=7)
    fig.tight_layout(); fig.savefig(f"{FIG}/e2_parametric_pumping_energy_L{L}.png", dpi=130)
