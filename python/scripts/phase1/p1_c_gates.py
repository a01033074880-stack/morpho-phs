# SPDX-License-Identifier: AGPL-3.0-only
"""Phase 1 (C): randomized stress gates for ESAV-LF + ENPU + energy-gated gain on the
2-DOF gyroscopic honeycomb (Lx x Lx cells), 48 kHz, T samples per trial.
 SCEN=passive : mu in [-50,0], extreme initial states + random force pulses.  Checks finiteness and
                per-step passivity  E^{n+1/2}-E^{n-1/2} <= h vb^T f  (rel. tol 1e-10).
 SCEN=modul   : as passive + every 64 samples (750 Hz) random jumps of m in [0.5,2]m0, alpha, beta,
                g (+-), mu<=0, applied with ENPU.  Checks E_after_ENPU <= max(E_before,0) and the
                cumulative passivity  E_n <= E_0 + W_in.
 SCEN=active  : mu in [0,mu_max] > 0, eta>0, energy cap E_cap (Theorem 4) + modulation; checks the
                a-priori bound  E~ <= max(E~_0, rho E~_cap)  with E~ = E + ndof c0/2."""
import json, os, sys, time
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../.."))
from diffphs.gyro import GyroHoneycomb, GyroESAV, Params, audio_params
OUT = os.path.join(os.path.dirname(__file__), "../../../experiments/results/phase1")
os.makedirs(OUT, exist_ok=True)
SCEN = os.environ.get("SCEN", "passive"); L = int(os.environ.get("L", 8))
NTR = int(os.environ.get("NTR", 10000)); BS = int(os.environ.get("BS", 500)); T = int(os.environ.get("T", 2048))
AP = audio_params(); h = AP["h"]; m0 = AP["m"]
lat = GyroHoneycomb(L, L, AP["k0"], AP["kc"]); N, nd = lat.N, lat.ndof
wmax_m0 = lat.omega_max(m0)
MU_MAX = float(os.environ.get("MU_MAX", 60.0)); ETA0 = 4 * 60.0 / (3 * 1000.0 ** 2)
c0 = 1.0
TAG = os.environ.get("TAG", SCEN)
rng = np.random.default_rng({"passive": 11, "modul": 12, "active": 13}[SCEN] + (1000 if TAG != SCEN else 0))

def rand_params(B, active):
    m = m0 * 2 ** rng.uniform(-1, 1, (N, B))
    al = (AP["kc"] / 3) * rng.uniform(0, 2, (N, B)); be = AP["k0"] * rng.uniform(0, 2, (N, B))
    g = AP["g"] * rng.uniform(-1.5, 1.5, (N, B))
    mu = rng.uniform(0, MU_MAX, (N, B)) if active else -rng.uniform(0, 50, (N, B))
    eta = ETA0 * rng.uniform(0.5, 2, (N, B)) if active else ETA0 * rng.uniform(0, 2, (N, B))
    return Params(N, B, m=m, alpha=al, beta=be, g=g, mu=mu, eta=eta)

m_min = m0 * 0.5
gamma = h * wmax_m0 * np.sqrt(m0 / m_min) / 2
delta = h * MU_MAX / (m_min * (1 - gamma ** 2)); rho = (1 + delta) / (1 - delta)
E_CAP = float(os.environ.get("E_CAP", 1e9))
stats = dict(trials=0, nonfinite=0, max_step_passivity_excess_rel=-np.inf, max_cum_passivity_excess_rel=-np.inf,
             enpu_calls=0, enpu_scaled=0, max_enpu_increase_rel=-np.inf, max_bound_ratio=0.0, max_E_over_E0=0.0)
t0 = time.time(); reach = 0
for batch in range(NTR // BS):
    B = BS; active = SCEN == "active"
    P = rand_params(B, active)
    q0 = rng.normal(0, 1, (nd, B)) * 10 ** rng.uniform(float(os.environ.get("Q0_EXP_MIN", -1)), float(os.environ.get("Q0_EXP_MAX", 2)), (1, B))
    v0 = rng.normal(0, 1, (nd, B)) * 10 ** rng.uniform(-1 if active else 1, float(os.environ.get("V0_EXP_MAX", 6)), (1, B))
    I = GyroESAV(lat, h, P, q0, v0, c0=c0, E_cap=(E_CAP if active else np.inf))
    E = I.energy(); E0 = E.copy(); W = np.zeros(B)
    Et0 = E0 + 0.5 * nd * c0
    bound = np.maximum(Et0, rho * (E_CAP + 0.5 * nd * c0))
    npulse = 4; pt = rng.integers(0, T, (npulse, B)); pn = rng.integers(0, nd, (npulse, B)); pa = rng.normal(0, 1, (npulse, B)) * 10 ** rng.uniform(4, 9, (npulse, B))
    if active:
        pa[:] = 0.0   # Theorem 4 bound is stated for f = 0 after the initial shock
    dead = np.zeros(B, bool); Emax_tr = np.zeros(B)
    for n in range(T):
        f = np.zeros((nd, B))
        hit = pt == n
        if hit.any():
            r, c = np.nonzero(hit); f[pn[r, c], c] = pa[r, c]
        I.step(f)
        pdis, pin = I.last_power
        En = I.energy()
        scale = np.abs(E) + np.abs(En) + h * (np.abs(pin) + np.abs(pdis)) + 1e-300
        if not active:
            stats["max_step_passivity_excess_rel"] = max(stats["max_step_passivity_excess_rel"], float(np.max((En - E - h * pin) / scale)))
        W += h * pin
        if SCEN in ("modul", "active") and n % 64 == 63:
            Pn = rand_params(B, active)
            Eb = I.energy(); s = I.enpu(Pn); Ea = I.energy()
            stats["enpu_calls"] += B; stats["enpu_scaled"] += int(np.sum(s < 1))
            stats["max_enpu_increase_rel"] = max(stats["max_enpu_increase_rel"], float(np.max((Ea - np.maximum(Eb, 0)) / (np.abs(Eb) + 1e-300))))
            En = Ea
        fin = np.isfinite(En) & np.isfinite(I.q).all(0)
        dead |= ~fin
        if not active:
            stats["max_cum_passivity_excess_rel"] = max(stats["max_cum_passivity_excess_rel"], float(np.max(np.where(fin, (En - E0 - W) / (np.abs(E0) + np.abs(W) + 1e-300), -np.inf))))
        else:
            Emax_tr = np.maximum(Emax_tr, np.where(fin, En, 0))
            stats["max_bound_ratio"] = max(stats["max_bound_ratio"], float(np.max(np.where(fin, (En + 0.5 * nd * c0) / bound, 0))))
        stats["max_E_over_E0"] = max(stats["max_E_over_E0"], float(np.max(np.where(fin, (En + 0.5 * nd * c0) / Et0, 0))))
        E = En
    stats["trials"] += B; stats["nonfinite"] += int(dead.sum())
    if active: reach += int(np.sum((Emax_tr > 0.5 * E_CAP) & (E0 < 0.5 * E_CAP)))
    print(batch, {k: v for k, v in stats.items()}, round(time.time() - t0, 1), flush=True)
stats["frac_trials_reaching_half_cap"] = float(reach / max(1, stats["trials"])) if active else None
stats.update(scen=SCEN, tag=TAG, mu_max=MU_MAX, L=L, T=T, ndof=nd, wall_s=time.time() - t0, gamma=gamma, delta=delta, rho=rho, E_cap=E_CAP,
             f_max_lin_Hz_at_m_min=wmax_m0 * np.sqrt(m0 / m_min) / (2 * np.pi))
json.dump(stats, open(f"{OUT}/p1_c_gate_{TAG}_L{L}.json", "w"), indent=1)
