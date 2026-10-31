# SPDX-License-Identifier: AGPL-3.0-only
"""F4: closed fast-slow loop exactly as written in the proposal (maps lines 66-75), on 16x16:
acoustic lattice (corrected ESAV-LF, 48 kHz, Phi=pi/2)  <->  Gray-Scott (750 Hz, D/4 so that patterns fit).
  u_t += kappa * tanh(lambda_E * <H_i>_block)        (source on u)
  m_i = m0 (1 + dm u_i),  alpha_i = alpha0 exp(da v_i),  mu_i = mu_max (v_i - v_crit)
Columns: A no GS seed + input noise; B seeded + input noise; C seeded, no input (synth); D = C with 5x kappa."""
import sys, os, json, time
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import numpy as np
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
plt.rcParams["font.family"] = ["Noto Sans CJK SC", "DejaVu Sans"]
from diffphs.model import square_lattice, U
from diffphs.integrators import Params, ESAV
from diffphs.audio_params import *
np.seterr(all="ignore")
FIG = os.path.join(os.path.dirname(__file__), "../../reports/figures")
OUT = os.path.join(os.path.dirname(__file__), "../../experiments/results")
L = 16; lat = square_lattice(L, K0, KC, SIGMA, np.pi / 2); N = lat.N; B = 4; BLK = 64
SEC = float(os.environ.get("SEC", 4.0)); NSLOW = int(SEC * FS / BLK)
DU, DV = [0.25 * x for x in (2e-5 * (256 / 2.5) ** 2, 1e-5 * (256 / 2.5) ** 2)]
F0, k0 = 0.0367, 0.0649
dm, da, mu_max, v_crit = 0.5, 2.0, 2000.0, 0.15
kappa = np.array([0.005, 0.005, 0.005, 0.025]); lamE = 1 / (0.5 * M0 * 1000.0 ** 2)
rng = np.random.default_rng(7)
u = np.ones((B, L, L)); v = np.zeros((B, L, L))
for b in (1, 2, 3):
    for _ in range(4):
        x, y = rng.integers(0, L, 2); ix = (np.arange(x, x + 3) % L)[:, None]; iy = (np.arange(y, y + 3) % L)[None, :]
        u[b, ix, iy] = 0.5; v[b, ix, iy] = 0.25
lap = lambda a: np.roll(a, 1, -1) + np.roll(a, -1, -1) + np.roll(a, 1, -2) + np.roll(a, -1, -2) - 4 * a
def theta(u, v):
    U_, V_ = u.reshape(B, N).T, v.reshape(B, N).T
    return Params(N, B, m=M0 * (1 + dm * U_), alpha=ALPHA0 * np.exp(da * V_), beta=BETA0,
                  mu=mu_max * (V_ - v_crit), eta=ETA0)
q0 = 1e-6 * rng.standard_normal((N, B)); s = ESAV(lat, H, theta(u, v), q0, np.zeros((N, B)))
inj = np.array([1.0, 1.0, 0.0, 0.0]); src_node = 0
Kd = np.asarray(lat.K.diagonal())
log = {k: [] for k in ("E", "vmax", "u_mean", "v_mean", "frac_mu_pos", "Hloc_max", "enpu_c_min", "u_max")}
t0 = time.time()
for n in range(NSLOW):
    Hacc = np.zeros((N, B))
    for j in range(BLK):
        f = np.zeros((N, B)); f[src_node] = inj * 3e7 * rng.standard_normal(B)   # broadband input force at a corner
        s.step(f)
        if j % 8 == 0:
            vel = (s.q - s.qm) / H
            Hacc += 0.5 * s.P.m * vel ** 2 + 0.5 * Kd[:, None] * s.q ** 2 + U(s.q, s.P.alpha, s.P.beta)
    Hloc = (Hacc / (BLK // 8)).T.reshape(B, L, L)
    uvv = u * v * v
    u = u + DU * lap(u) - uvv + F0 * (1 - u) + kappa[:, None, None] * np.tanh(lamE * Hloc)
    v = v + DV * lap(v) + uvv - (F0 + k0) * v
    s.set_params(theta(u, v))
    vel = (s.q - s.qm) / H
    log["E"].append(s.energy().tolist()); log["vmax"].append(np.abs(vel).max(0).tolist())
    log["u_mean"].append(u.mean((1, 2)).tolist()); log["v_mean"].append(v.mean((1, 2)).tolist()); log["u_max"].append(u.max((1, 2)).tolist())
    log["frac_mu_pos"].append((s.P.mu > 0).mean(0).tolist()); log["Hloc_max"].append(Hloc.max((1, 2)).tolist())
    log["enpu_c_min"].append(np.min(getattr(s, "enpu_c", np.ones(B)), 0).tolist() if np.ndim(getattr(s, "enpu_c", 1)) else 1.0)
    if n % 250 == 0: print(n, round(time.time() - t0), "s", np.round(log["v_mean"][-1], 3), np.round(log["vmax"][-1], 1), flush=True)
A = {k: np.array(x) for k, x in log.items()}
fin = {"wall_s": time.time() - t0, "seconds_simulated": SEC, "finite": bool(np.isfinite(A["E"]).all()),
       "columns": ["A no seed + input", "B seed + input", "C seed, no input", "D seed, no input, 5x kappa"],
       "final_v_mean": A["v_mean"][-1].tolist(), "final_frac_mu_pos": A["frac_mu_pos"][-1].tolist(),
       "max_vel": A["vmax"].max(0).tolist(), "final_vel": A["vmax"][-1].tolist(), "u_max": A["u_max"].max(0).tolist(),
       "v_mean_late_var": (A["v_mean"][-200:].std(0)).tolist(),
       "jacobi_sweeps_mean": float(np.mean(s.jac_sweeps)), "jacobi_sweeps_max": int(np.max(s.jac_sweeps))}
json.dump(fin, open(os.path.join(OUT, "f4_coupled_loop.json"), "w"), indent=1)
t = np.arange(NSLOW) / 750.0
fig, ax = plt.subplots(2, 2, figsize=(11, 7))
for b, nm in enumerate(fin["columns"]):
    ax[0, 0].semilogy(t, np.maximum(A["vmax"][:, b], 1e-9), label=nm); ax[0, 1].plot(t, A["v_mean"][:, b], label=nm)
    ax[1, 0].plot(t, A["frac_mu_pos"][:, b], label=nm); ax[1, 1].plot(t, A["u_max"][:, b], label=nm)
ax[0, 0].set_title("max |velocity| in lattice"); ax[0, 1].set_title("mean v (morphogen)")
ax[1, 0].set_title("fraction of cells with μ>0 (self-exciting)"); ax[1, 1].set_title("max u (proposal claims u∈[0,1])")
for a in ax.flat: a.set_xlabel("s"); a.legend(fontsize=7)
fig.suptitle("F4 closed loop 16×16: ESAV-LF @48 kHz ↔ Gray-Scott @750 Hz (proposal maps)")
fig.tight_layout(); fig.savefig(os.path.join(FIG, "f2_coupled_loop.png"), dpi=110)
fig, ax = plt.subplots(1, 4, figsize=(12, 3))
for b in range(B): ax[b].imshow(v[b], cmap="magma", vmin=0, vmax=0.5); ax[b].set_title(fin["columns"][b], fontsize=8); ax[b].axis("off")
fig.tight_layout(); fig.savefig(os.path.join(FIG, "f3_coupled_loop_final_v.png"), dpi=110)
print(json.dumps(fin, indent=1))
