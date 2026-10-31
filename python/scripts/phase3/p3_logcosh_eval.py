# SPDX-License-Identifier: AGPL-3.0-only
"""Evaluate the K=3 sum-of-sqrt log-cosh surrogate (constants from p3_fit_logcosh.py, KS=3) in f64 and f32,
using the cancellation-free form  sqrt(c^2+s^2) - c = s^2/(sqrt(c^2+s^2)+c).  Writes logcosh_surrogate_eval.json + figure."""
import json, os, numpy as np
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
W = np.array([8.579902105918194, 4.224340409541995, -11.804242515460189])
C = np.array([2.3359531810335286, 1.569904461145531, 2.200722466602941])
def surr(s, dt):
    s = s.astype(dt); w = W.astype(dt); c = C.astype(dt); s2 = s * s
    r = np.sqrt(c[None, :] * c[None, :] + s2[:, None])
    return (w * (s2[:, None] / (r + c))).sum(1, dtype=dt), (w * (s[:, None] / r)).sum(1, dtype=dt)
s = np.concatenate([np.linspace(0, 10, 100001), np.geomspace(10, 1e6, 20000)[1:]])
sl = s.astype(np.longdouble)
lc = np.abs(sl) + np.log1p(np.exp(-2 * np.abs(sl))) - np.log(np.longdouble(2)); th = np.tanh(sl)
r64 = np.sqrt(C[None, :] ** 2 + sl[:, None] ** 2); U2 = (W * C ** 2 / r64 ** 3).sum(1)
out = {"form": "U_hat(s)=sum_k w_k s^2/(sqrt(c_k^2+s^2)+c_k), U_hat'(s)=sum_k w_k s/sqrt(c_k^2+s^2)", "w": W.tolist(), "c": C.tolist(),
       "sum_w": float(W.sum()), "sum_w_over_c": float((W / C).sum()), "sum_w_c2": float((W * C * C).sum()),
       "asymptotic_offset_vs_logcosh": float(np.log(2) - (W * C).sum()),
       "grid": "s in [0,10] (1e5 pts) + geomspace(10,1e6,2e4); reference in long double",
       "min_second_derivative_on_grid": float(U2.min()), "convex_on_grid": bool(U2.min() > 0)}
for dt, nm in ((np.float64, "f64"), (np.float32, "f32")):
    U, dU = surr(s, dt)
    eU = np.abs(U.astype(np.longdouble) - lc); ed = np.abs(dU.astype(np.longdouble) - th)
    small = s < 1e-2
    core = s <= 10
    out[nm] = {"max_abs_err_U_vs_logcosh_|s|<=10": float(eU[core].max()), "max_rel_err_U_vs_logcosh_|s|>=10": float((eU[~core] / lc[~core]).max()),
               "max_abs_err_U_vs_logcosh_all": float(eU.max()), "max_abs_err_dU_vs_tanh": float(ed.max()),
               "min_U": float(U.min()), "U_nonnegative": bool(U.min() >= 0),
               "max_rel_err_U_small_s(<1e-2)_vs_s2/2": float(np.max(np.abs(U[small][1:].astype(np.longdouble) / (sl[small][1:] ** 2 / 2) - 1)))}
json.dump(out, open(os.path.join(ROOT, "experiments/results/phase3/logcosh_surrogate_eval.json"), "w"), indent=1)
print(json.dumps(out, indent=1))
x = np.linspace(0, 8, 4000); U, dU = surr(x, np.float64)
fig, ax = plt.subplots(1, 2, figsize=(11, 3.8))
ax[0].plot(x, U - (np.abs(x) + np.log1p(np.exp(-2 * x)) - np.log(2))); ax[0].set_title("U_hat(s) - log cosh(s)  (K=3)")
ax[1].plot(x, dU - np.tanh(x)); ax[1].set_title("U_hat'(s) - tanh(s)  (K=3)")
for a in ax: a.grid(alpha=.3); a.set_xlabel("s")
fig.tight_layout(); fig.savefig(os.path.join(ROOT, "reports/figures/phase3/logcosh_surrogate_error.png"), dpi=130)
