# SPDX-License-Identifier: AGPL-3.0-only
"""Audit (b): exact Bernoulli flow of the Rayleigh port  dp/dt = (mu - eta p^2/m^2) p/m."""
import sys, os, json
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import numpy as np, sympy as s, mpmath as mp
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
from diffphs.bernoulli import bernoulli_literal, bernoulli_stable
FIG = os.path.join(os.path.dirname(__file__), "../../reports/figures")
OUT = os.path.join(os.path.dirname(__file__), "../../experiments/results"); os.makedirs(OUT, exist_ok=True)
res = {}

# 1. symbolic check
t, p0, m, eta = s.symbols("t p0 m eta", positive=True); mu = s.symbols("mu", real=True)
P = p0 * s.exp(mu / m * t) / s.sqrt(1 + eta / (mu * m ** 2) * p0 ** 2 * (s.exp(2 * mu * t / m) - 1))
ode = s.simplify(s.diff(P, t) - (mu - eta * P ** 2 / m ** 2) * P / m)
res["sympy_residual"] = str(ode)
res["sympy_initial"] = str(s.simplify(P.subs(t, 0)))
res["sympy_limit_mu0"] = str(s.simplify(s.limit(P, mu, 0)))
print(res)

# 2. accuracy vs 60-digit reference
mp.mp.dps = 60
def ref(p, m, mu, eta, dt):
    p, m, mu, eta, dt = map(mp.mpf, (p, m, mu, eta, dt))
    a = mu / m; b = eta / m ** 3
    if mu == 0:
        w = 1 / p ** 2 + 2 * b * dt
    else:
        w = mp.e ** (-2 * a * dt) / p ** 2 + (b / a) * (1 - mp.e ** (-2 * a * dt))
    return float(mp.sign(p) / mp.sqrt(w))
dt = 1 / 48000; mq = 1.0; eta_ = 1.5e-5; p0v = 3000.0
mus = np.concatenate([-np.logspace(3, -14, 120), [0.0], np.logspace(-14, 3, 120)])
el, es = [], []
for mu_ in mus:
    r = ref(p0v, mq, mu_, eta_, dt)
    el.append(abs(bernoulli_literal(np.array(p0v), mq, mu_, eta_, dt) - r) / abs(r))
    es.append(abs(bernoulli_stable(np.array(p0v), mq, mu_, eta_, dt) - r) / abs(r))
el = np.array(el, float); es = np.array(es, float)
res["literal_nan_at_mu0"] = bool(np.isnan(bernoulli_literal(np.array(p0v), mq, 0.0, eta_, dt)))
res["literal_max_relerr_finite"] = float(np.nanmax(el)); res["stable_max_relerr"] = float(es.max())
# huge dt / huge p
pbig = np.array([1e-300, 1e-6, 1.0, 1e6, 1e150]); 
res["stable_huge_dt"] = [float(x) for x in bernoulli_stable(pbig, 1.0, 50.0, 1e-5, 1e6)]
res["literal_huge_dt"] = [float(x) for x in bernoulli_literal(pbig, 1.0, 50.0, 1e-5, 1e6)]
res["pstar"] = float(np.sqrt(50.0 / 1e-5))

# 3. randomized monotonicity / boundedness test (1e6 draws)
rng = np.random.default_rng(1)
n = 1_000_000
pp = rng.standard_normal(n) * 10 ** rng.uniform(-6, 8, n)
mm = 10 ** rng.uniform(-1, 1, n); mu_r = rng.uniform(-1, 1, n) * 10 ** rng.uniform(-8, 4, n)
et = 10 ** rng.uniform(-9, -2, n); dd = 10 ** rng.uniform(-7, 1, n)
pn = bernoulli_stable(pp, mm, mu_r, et, dd)
pstar = np.where(mu_r > 0, mm * np.sqrt(np.maximum(mu_r, 0) / et), 0.0)
lo = np.minimum(np.abs(pp), pstar); hi = np.maximum(np.abs(pp), pstar)
tol = 1e-12 * hi + 1e-300
ok = (np.abs(pn) >= lo - tol) & (np.abs(pn) <= hi + tol) & np.isfinite(pn) & (np.sign(pn) * np.sign(pp) >= 0)
res["random_monotone_bounded_pass"] = int(ok.sum()); res["random_total"] = n
pl = bernoulli_literal(pp, mm, mu_r, et, dd)
res["random_literal_nonfinite"] = int((~np.isfinite(pl)).sum())

# 4. fixed point of sub-flow vs true limit cycle of the coupled oscillator
w0 = 2 * np.pi * 440.0; mu0 = 60.0; et0 = 1.5e-5; mq = 1.0
def rhs(x):
    q, p = x; v = p / mq
    return np.array([v, -mq * w0 ** 2 * q + (mu0 - et0 * v * v) * v])
h = 1 / 192000; x = np.array([0.0, 1.0]); vs = []
for k in range(int(1.5 * 192000)):
    k1 = rhs(x); k2 = rhs(x + h / 2 * k1); k3 = rhs(x + h / 2 * k2); k4 = rhs(x + h * k3)
    x = x + h / 6 * (k1 + 2 * k2 + 2 * k3 + k4); vs.append(x[1] / mq)
vs = np.array(vs); vamp = np.abs(vs[-20000:]).max()
res["limit_cycle_v_amp_sim"] = float(vamp)
res["proposal_radius_sqrt(mu/eta)"] = float(np.sqrt(mu0 / et0))
res["rayleigh_averaging_sqrt(4mu/3eta)"] = float(np.sqrt(4 * mu0 / (3 * et0)))

fig, ax = plt.subplots(1, 2, figsize=(12, 4))
xs = np.sign(mus) * np.log10(np.abs(mus) + 1e-300); xs[mus == 0] = 0
ax[0].semilogy(np.arange(len(mus)), np.where(np.isfinite(el), el, 1.0) + 1e-17, "r.", ms=3, label="proposal formula (NaN plotted as 1)")
ax[0].semilogy(np.arange(len(mus)), es + 1e-17, "b.", ms=3, label="stable form 1/p²=e^{-z}/p0²+2bΔt·φ1(-z)")
ax[0].axvline(120, color="k", lw=.5); ax[0].set_xlabel("μ index (−1e3 … −1e-14, 0, 1e-14 … 1e3)")
ax[0].set_ylabel("relative error vs 60-digit reference"); ax[0].legend(fontsize=8); ax[0].set_title("Bernoulli one-step map at Δt=1/48000")
tt = np.arange(len(vs)) * h
ax[1].plot(tt, vs, lw=.3, color="gray")
ax[1].axhline(np.sqrt(mu0 / et0), color="r", ls="--", label="proposal 'limit-cycle radius' √(μ/η)")
ax[1].axhline(np.sqrt(4 * mu0 / (3 * et0)), color="b", ls="--", label="Rayleigh averaging √(4μ/3η)")
ax[1].set_xlabel("t [s]"); ax[1].set_ylabel("velocity p/m"); ax[1].legend(fontsize=8)
ax[1].set_title("Single Rayleigh oscillator 440 Hz, μ=60, η=1.5e-5 (RK4, 192 kHz)")
fig.tight_layout(); fig.savefig(f"{FIG}/b1_bernoulli.png", dpi=130)
json.dump(res, open(f"{OUT}/b_bernoulli.json", "w"), indent=1)
print(json.dumps(res, indent=1))
