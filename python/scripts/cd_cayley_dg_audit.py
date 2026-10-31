# SPDX-License-Identifier: AGPL-3.0-only
"""Audit (c) Cayley step and (d) 'explicit' discrete gradient.
c1  Q = (I - h/2 A)^-1 (I + h/2 A), A = J*Hlin: preserves z^T Hlin z, NOT the Euclidean norm.
c2  Q is dense: fill fraction; sparse-LU fill vs L; per-sample flop counts (dense vs LU vs ESAV stencil).
c3  refactorisation cost when m_i changes (750 Hz control rate).
d1  Kick/Verlet ('q frozen' reading of the Gonzalez step) has an amplitude-dependent CFL:
    h * omega_loc(q) < 2  with omega_loc^2 = (k0 + 4 kc + 3 alpha q^2 + beta sech^2 q)/m  -> q_crit; numerical check.
d2  Gonzalez dG inside implicit midpoint: scalar transcendental equation per node, coupled through K -> Newton."""
import sys, os, json, time
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import numpy as np, scipy.sparse as sp, scipy.sparse.linalg as spla, scipy.optimize as so
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
plt.rcParams["font.family"] = ["Noto Sans CJK SC", "DejaVu Sans"]
from diffphs.model import square_lattice, dU, d2U
from diffphs.integrators import Params, Verlet, IMDG
from diffphs.audio_params import *
np.seterr(all="ignore")
FIG = os.path.join(os.path.dirname(__file__), "../../reports/figures")
OUT = os.path.join(os.path.dirname(__file__), "../../experiments/results")
res = {}
# ---- c1
L = 8; lat = square_lattice(L, K0, KC, SIGMA, np.pi / 2); N = lat.N
Minv = sp.diags(np.full(N, 1 / M0))
A = sp.bmat([[None, Minv], [-lat.K, lat.G @ Minv]]).toarray()
Hl = sp.block_diag([lat.K, Minv]).toarray()
I = np.eye(2 * N); Q = np.linalg.solve(I - H / 2 * A, I + H / 2 * A)
rng = np.random.default_rng(0); Z = rng.standard_normal((2 * N, 2000))
r_euc = np.linalg.norm(Q @ Z, axis=0) / np.linalg.norm(Z, axis=0)
eH = lambda z: np.einsum("ij,ij->j", z, Hl @ z)
r_H = eH(Q @ Z) / eH(Z)
sv = np.linalg.svd(Q, compute_uv=False)
res["c1"] = {"L": L, "euclid_norm_ratio_min": float(r_euc.min()), "euclid_norm_ratio_max": float(r_euc.max()),
             "Q_singular_min": float(sv.min()), "Q_singular_max": float(sv.max()),
             "H_ratio_max_dev": float(np.abs(r_H - 1).max()),
             "QtQ_minus_I_max": float(np.abs(Q.T @ Q - I).max()),
             "QtHQ_minus_H_rel": float(np.abs(Q.T @ Hl @ Q - Hl).max() / np.abs(Hl).max()),
             "Q_fill_fraction(|q|>1e-12*max)": float((np.abs(Q) > 1e-12 * np.abs(Q).max()).mean())}
# ---- c2 / c3
c2 = {}
for L2 in (8, 16, 32):
    lt = square_lattice(L2, K0, KC, SIGMA, np.pi / 2); n = lt.N
    Mi = sp.diags(np.full(n, 1 / M0)); A2 = sp.bmat([[None, Mi], [-lt.K, lt.G @ Mi]], format="csc")
    I2 = sp.identity(2 * n, format="csc")
    t0 = time.perf_counter(); lu = spla.splu((I2 - H / 2 * A2).tocsc()); tf = time.perf_counter() - t0
    nnzLU = lu.L.nnz + lu.U.nnz
    flops_lu_step = 2 * (2 * nnzLU + 2 * (I2 + H / 2 * A2).nnz)       # 2 Cayley half-steps: 2x (rhs matvec + 2 triangular solves)
    flops_dense_step = 2 * 2 * (2 * n) ** 2                           # 2 dense matvecs, 2 flops/MAC
    flops_esav = n * (2 * 6 + 2 * 15 * 5 + 40)                       # K stencil + 15 Jacobi G sweeps + ~40 per-node (excl. transcendentals)
    c2[L2] = {"N": n, "splu_nnz_L+U": int(nnzLU), "splu_factor_time_ms(python/SuperLU)": tf * 1e3,
              "factorisations_per_s_at_750Hz_if_m_changes": 750, "factor_cpu_fraction_at_750Hz": tf * 750,
              "flops_per_sample_dense_Q": flops_dense_step, "flops_per_sample_sparseLU": flops_lu_step,
              "flops_per_sample_ESAV_stencil": flops_esav,
              "GFLOP_s_dense_Q": flops_dense_step * FS / 1e9, "GFLOP_s_sparseLU": flops_lu_step * FS / 1e9}
res["c2"] = c2
# ---- d1 amplitude CFL of kick-type (Verlet) updates, Phi = 0 (no gyroscopic term)
wloc2 = lambda q: (K0 + 4 * KC + 3 * ALPHA0 * q * q + BETA0 / np.cosh(q) ** 2) / M0
qcrit_pred = so.brentq(lambda q: H * np.sqrt(wloc2(q)) - 2, 0, 1e3)
lat0 = square_lattice(8, K0, KC, SIGMA, 0.0); n0 = lat0.N
amps = np.logspace(-1, 2, 61); B = len(amps)
q0 = np.zeros((n0, B)); q0[n0 // 2 + 4] = amps
P = Params(n0, B, m=M0, alpha=ALPHA0, beta=BETA0)
vs = Verlet(lat0, H, P, q0.copy(), np.zeros((n0, B))); E0 = vs.energy()
for _ in range(2000): vs.step(0.0)
blow_v = ~(np.isfinite(vs.energy()) & (vs.energy() < 10 * E0))
im = IMDG(lat0, H, P.copy(), q0.copy(), np.zeros((n0, B)))
for _ in range(200): im.step(0.0)
blow_i = ~(np.isfinite(im.energy()) & (im.energy() < 10 * E0))
res["d1"] = {"q_crit_linearised_prediction": qcrit_pred,
             "verlet_first_blowup_amp": float(amps[blow_v.argmax()]) if blow_v.any() else None,
             "imdg_blowups": int(blow_i.sum()), "imdg_newton_iters_mean": float(np.mean(im.iters)) if hasattr(im, "iters") else None,
             "imdg_newton_iters_max": int(np.max(im.iters)) if hasattr(im, "iters") else None}
# ---- figure
fig, ax = plt.subplots(1, 3, figsize=(14, 4))
ax[0].hist(r_euc, 50, alpha=.7, label="‖Qz‖₂/‖z‖₂ (Euclid)"); ax[0].axvline(1, c="k")
ax[0].set_title(f"(c) Cayley Q 8×8: Euclid ratio ∈[{r_euc.min():.3f},{r_euc.max():.3f}]\nH-norm ratio dev ≤ {np.abs(r_H-1).max():.1e}"); ax[0].legend()
Ls = [8, 16, 32]
ax[1].semilogy(Ls, [c2[l]["GFLOP_s_dense_Q"] for l in Ls], "o-", label="dense Q (2 matvec)")
ax[1].semilogy(Ls, [c2[l]["GFLOP_s_sparseLU"] for l in Ls], "s-", label="sparse LU solve")
ax[1].semilogy(Ls, [c2[l]["flops_per_sample_ESAV_stencil"] * FS / 1e9 for l in Ls], "^-", label="ESAV-LF stencil")
ax[1].set_xlabel("L"); ax[1].set_ylabel("GFLOP/s @48 kHz"); ax[1].set_title("(c) arithmetic per second"); ax[1].legend()
ax[2].semilogx(amps, blow_v, "o", label="Verlet / kick (q frozen) blow-up")
ax[2].semilogx(amps, blow_i + 0.03, "x", label="implicit midpoint-dG")
ax[2].axvline(qcrit_pred, c="r", ls="--", label=f"linearised q_crit={qcrit_pred:.2f}")
ax[2].set_xlabel("initial |q| at one node"); ax[2].set_title("(d) amplitude-dependent CFL, Φ=0"); ax[2].legend(fontsize=8)
fig.tight_layout(); fig.savefig(os.path.join(FIG, "cd_cayley_and_kick_cfl.png"), dpi=110)
json.dump(res, open(os.path.join(OUT, "cd_cayley_dg.json"), "w"), indent=1, default=float)
print(json.dumps(res, indent=1, default=float))
