# SPDX-License-Identifier: AGPL-3.0-only
"""Phase 3: fit a vectorisable convex surrogate of log cosh (no exp/log, only sqrt + div):
    L(s) = sum_k w_k (sqrt(c_k^2 + s^2) - c_k),   c_k > 0.
Exact constraints:  sum w = 1      (L'(inf) = 1 = tanh(inf): same large-amplitude force saturation)
                    sum w / c = 1  (L''(0) = 1 = logcosh''(0): same small-signal stiffness)
NOTE: the third natural constraint sum w c = ln2 (same asymptote |s|-ln2) is INFEASIBLE together with these for
w >= 0 (Jensen: sum w/c >= 1/sum w c = 1/ln2 > 1), so the asymptotic offset is left free and reported.
Weights may be signed; convexity L'' = sum w c^2/(c^2+s^2)^{3/2} >= 0 is enforced on a dense grid and checked
in the tail (sum w c^2 > 0).  L is even, L(0)=0, and L' = sum w s/sqrt(c^2+s^2) is its EXACT derivative, so the
SAV coefficient (alpha s^3 + beta L')/sqrt(2(alpha s^4/4 + beta L) + c0) is consistent by construction and
Thms 1-4 (convex U, U(0)=0, U>=0) hold for the surrogate potential exactly."""
import json, os, numpy as np
from scipy.optimize import minimize
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
s = np.concatenate([np.linspace(0, 10, 5001), np.geomspace(10, 1e5, 3000)[1:]])
lc = np.abs(s) + np.log1p(np.exp(-2 * np.abs(s))) - np.log(2); th = np.tanh(s)

def model(w, c, x):
    r = np.sqrt(c[None, :] ** 2 + x[:, None] ** 2)
    return (w * (r - c)).sum(1), (w * x[:, None] / r).sum(1), (w * c ** 2 / r ** 3).sum(1)

def fit(K, seed, p_list=(8, 32, 128), lam=0.3):
    rng = np.random.default_rng(seed)
    z0 = np.concatenate([rng.uniform(0.1, 1, K), np.log(rng.uniform(0.2, 4, K))]); z0[:K] /= z0[:K].sum()
    sg = s[::20]
    cons = [{"type": "eq", "fun": lambda z: z[:K].sum() - 1},
            {"type": "eq", "fun": lambda z: (z[:K] / np.exp(z[K:])).sum() - 1},
            {"type": "ineq", "fun": lambda z: model(z[:K], np.exp(z[K:]), sg)[2] * sg.clip(1) ** 3}]
    for p in p_list:
        f = lambda z: (lambda L, d, _: np.mean(np.abs(d - th[::4]) ** p) ** (1 / p) + lam * np.mean(np.abs(L - lc[::4]) ** p) ** (1 / p))(*model(z[:K], np.exp(z[K:]), s[::4]))
        r = minimize(f, z0, method="SLSQP", constraints=cons, options={"maxiter": 3000, "ftol": 1e-15})
        z0 = r.x
    w, c = z0[:K], np.exp(z0[K:])
    L, d, d2 = model(w, c, s)
    return w, c, float(np.max(np.abs(d - th))), float(np.max(np.abs(L - lc))), float(d2.min())

res = {}
for K in tuple(int(k) for k in os.environ.get("KS", "2,3").split(",")):
    best = None
    for seed in range(24):
        try: w, c, e1, e0, cmin = fit(K, seed)
        except Exception as ex: continue
        if not np.all(np.isfinite(w)) or cmin < 0 or abs(w.sum() - 1) > 1e-9 or abs((w / c).sum() - 1) > 1e-9 or (w * c * c).sum() <= 0: continue
        sc = e1 + 0.3 * e0
        if best is None or sc < best[0]: best = (sc, w, c, e1, e0, cmin)
    _, w, c, e1, e0, cmin = best
    res[f"K{K}"] = {"w": w.tolist(), "c": c.tolist(), "max_abs_err_dU_vs_tanh": e1, "max_abs_err_U_vs_logcosh": e0,
                    "asymptotic_offset_U_minus_logcosh": float(np.log(2) - (w * c).sum()),
                    "sum_w": float(w.sum()), "sum_w_over_c": float((w / c).sum()), "sum_w_c2": float((w * c * c).sum()),
                    "min_U2_on_grid": cmin, "grid": "s in [0,10] step 0.002 + geomspace(10,1e5,3000)"}
    print(K, json.dumps(res[f"K{K}"]), flush=True)
out = os.path.join(ROOT, "experiments/results/phase3"); os.makedirs(out, exist_ok=True)
json.dump(res, open(os.path.join(out, "logcosh_fit.json"), "w"), indent=1)
fig, ax = plt.subplots(1, 2, figsize=(11, 4)); x = np.linspace(0, 8, 2000)
lcx = np.abs(x) + np.log1p(np.exp(-2 * x)) - np.log(2)
for k, v in res.items():
    L, d, _ = model(np.array(v["w"]), np.array(v["c"]), x)
    ax[0].plot(x, L - lcx, label=k); ax[1].plot(x, d - np.tanh(x), label=k)
ax[0].set_title("U_hat(s) - log cosh(s)"); ax[1].set_title("U_hat'(s) - tanh(s)")
for a in ax: a.grid(alpha=.3); a.legend(); a.set_xlabel("s")
fd = os.path.join(ROOT, "reports/figures/phase3"); os.makedirs(fd, exist_ok=True)
fig.tight_layout(); fig.savefig(os.path.join(fd, "logcosh_surrogate_error.png"), dpi=130)
