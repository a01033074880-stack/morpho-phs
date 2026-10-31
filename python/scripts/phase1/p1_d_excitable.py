# SPDX-License-Identifier: AGPL-3.0-only
"""Phase 1 (D): Barkley excitable control layer on the honeycomb site graph (8..32 cells).
D1 spiral survival vs size (random broken fronts) at the control-rate step dt=0.016 vs reference dt=0.002
D2 dt-robustness sweep;  D3 target waves under periodic pacing;  D4 audio-driven triggering (one-way)."""
import json, os, sys, time
import numpy as np
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../.."))
from diffphs.gyro import GyroHoneycomb
from diffphs.excitable import Barkley
ROOT = os.path.join(os.path.dirname(__file__), "../../..")
OUT = f"{ROOT}/experiments/results/phase1"; FIG = f"{ROOT}/reports/figures/phase1"
os.makedirs(OUT, exist_ok=True); os.makedirs(FIG, exist_ok=True)
FS, BLK = 48000, 64; FC = FS / BLK            # control tick 750 Hz
TAU = 1 / 12                                  # 1 model time unit = 83.3 ms  -> dt per tick = 0.016
DT_CTRL = 1 / (FC * TAU)
R = dict(params=dict(a=0.75, b=0.03, eps=0.02, D=0.5), tau_s=TAU, control_rate_Hz=FC, dt_ctrl=DT_CTRL,
         citation="D. Barkley, Physica D 49 (1991) 61-70, doi:10.1016/0167-2789(91)90194-E")
rng = np.random.default_rng(5)

def period_of(series, t, tmin):
    up = np.nonzero((series[1:] > 0.5) & (series[:-1] <= 0.5))[0]; tt = t[up]; tt = tt[tt > tmin]
    return float(np.median(np.diff(tt))) if len(tt) > 2 else float("nan")

def run_spiral(L, dt, x0, yf, flip, T=300.0, snap_at=None):
    lat = GyroHoneycomb(L, L, 1, 1); M = Barkley(lat, dt=dt); M.broken_front(x0, yf, flip)
    k = max(1, int(round(0.016 / dt))); n = int(T / (k * dt)); probe = []; snap = None; chg_t = []
    umin, umax = 0.0, 0.0
    for i in range(n):
        M.step(k); probe.append(M.u[lat.N // 2]); umin = min(umin, M.u.min()); umax = max(umax, M.u.max())
        if i % int(5 / (k * dt)) == 0: chg_t.append(int(np.abs(M.charges()).sum()))
        if snap_at is not None and abs(i * k * dt - snap_at) < k * dt / 2: snap = (M.u.copy(), M.charges().copy(), lat)
    t = np.arange(n) * k * dt; c = M.charges()
    return dict(alive=bool(M.u.max() > 0.5), charges_end=int(np.abs(c).sum()), net_charge=int(c.sum()),
                period=period_of(np.array(probe), t, 50.0), umin=float(umin), umax=float(umax),
                charge_series=chg_t), snap

t0 = time.time()
# D1
D1 = {}
for L in [8, 12, 16, 24, 32]:
    rows = []
    for s in range(20):
        x0, yf, fl = rng.uniform(0.15, 0.5), rng.uniform(0.3, 0.7), bool(rng.integers(2))
        r, _ = run_spiral(L, DT_CTRL, x0, yf, fl)
        if s < 4:
            rr, _ = run_spiral(L, 0.002, x0, yf, fl); r["ref_dt0.002"] = {k: rr[k] for k in ("alive", "charges_end", "period")}
        rows.append(r)
    surv = [r["alive"] and r["charges_end"] > 0 for r in rows]
    per = [r["period"] for r in rows if r["alive"]]
    refp = [(r["period"], r["ref_dt0.002"]["period"]) for r in rows if "ref_dt0.002" in r and r["alive"] and r["ref_dt0.002"]["alive"]]
    D1[L] = dict(N_sites=GyroHoneycomb(L, L, 1, 1).N, n=len(rows), spiral_survival_frac=float(np.mean(surv)),
                 period_model_median=float(np.nanmedian(per)) if per else None,
                 period_s_median=float(np.nanmedian(per) * TAU) if per else None,
                 period_rel_err_vs_ref=[float(a / b - 1) for a, b in refp],
                 ref_agreement=[(r["alive"] and r["charges_end"] > 0) == (r["ref_dt0.002"]["alive"] and r["ref_dt0.002"]["charges_end"] > 0) for r in rows if "ref_dt0.002" in r],
                 charges_end_hist=np.bincount([r["charges_end"] for r in rows]).tolist(),
                 u_range=[min(r["umin"] for r in rows), max(r["umax"] for r in rows)])
    print("D1", L, D1[L], round(time.time() - t0, 1), flush=True)
R["D1_spiral_survival"] = D1
# D2
D2 = {}
for dt in [0.004, 0.016, 0.024, 0.032, 0.048, 0.064]:
    r, _ = run_spiral(16, dt, 0.25, 0.5, False, T=200)
    D2[dt] = {k: r[k] for k in ("alive", "charges_end", "period", "umin", "umax")}
    print("D2", dt, D2[dt], flush=True)
R["D2_dt_sweep_L16"] = D2

# D3 target waves / pacing
lat = GyroHoneycomb(16, 16, 1, 1); X = lat.coords; ctr = np.argmin(np.sum((X - X.mean(0)) ** 2, 1))
far = np.argmax(np.sum((X - X[ctr]) ** 2, 1)); dist_far = float(np.linalg.norm(X[far] - X[ctr]))
# single-pulse travel time centre -> far corner
M = Barkley(lat, dt=DT_CTRL); st = np.zeros(lat.N); st[lat.Adj[ctr].indices] = 1; st[ctr] = 1; M.step(1, st); T_travel = None
for i in range(1, int(60 / DT_CTRL)):
    M.step(1)
    if M.u[far] > 0.5: T_travel = i * DT_CTRL; break
D3 = {}
for P in [2, 3, 4, 5, 6, 8, 10, 14]:
    M = Barkley(lat, dt=DT_CTRL); nst = 0; resp = 0; prev = 0
    nT = int(150 / DT_CTRL); kP = int(round(P / DT_CTRL)); t_last_counted = 150 - T_travel - 1
    for i in range(nT):
        stim = None
        if i % kP == 0:
            stim = np.zeros(lat.N); stim[lat.Adj[ctr].indices] = 1.0; stim[ctr] = 1.0
            if i * DT_CTRL <= t_last_counted: nst += 1
        M.step(1, stim)
        cur = M.u[far] > 0.5
        if cur and not prev and i * DT_CTRL <= t_last_counted + T_travel + 0.5: resp += 1
        prev = cur
    D3[P] = dict(stimuli=nst, far_responses=resp, ratio=resp / nst, period_s=P * TAU)
    print("D3", P, D3[P], flush=True)
R["D3_pacing_L16"] = dict(dist_far=dist_far, travel_time_model=T_travel, travel_time_s=T_travel * TAU, wave_speed_sites_per_s=dist_far / (T_travel * TAU), refractory_note="ratio<1 => stimuli fall in refractory tail (period-doubling / Wenckebach-like block)", rows=D3)

# D4 audio-driven triggering
def drum_loop(sec=8.0, bpm=120, seed=3):
    r = np.random.default_rng(seed); n = int(sec * FS); x = np.zeros(n); t16 = 60 / bpm / 4
    kick = [0, 6, 8, 11]; snare = [4, 12]; onsets = []
    for step in range(int(sec / t16)):
        i0 = int(step * t16 * FS); s = step % 16
        if s in kick:
            tt = np.arange(int(0.3 * FS)) / FS; f = 50 + 100 * np.exp(-tt * 30)
            seg = np.sin(2 * np.pi * np.cumsum(f) / FS) * np.exp(-tt * 9); onsets.append((i0, "kick"))
        elif s in snare:
            tt = np.arange(int(0.2 * FS)) / FS
            seg = (0.6 * r.normal(0, 1, len(tt)) + 0.4 * np.sin(2 * np.pi * 190 * tt)) * np.exp(-tt * 22) * 0.7; onsets.append((i0, "snare"))
        else:
            tt = np.arange(int(0.05 * FS)) / FS; seg = np.diff(r.normal(0, 1, len(tt) + 1)) * np.exp(-tt * 90) * 0.15
        e = min(n, i0 + len(seg)); x[i0:e] += seg[:e - i0]
    return x / np.max(np.abs(x)) * 0.8, onsets
x, onsets = drum_loop(); np.save(f"{OUT}/drum_loop.npy", x.astype(np.float32))
lat = GyroHoneycomb(16, 16, 1, 1); X = lat.coords
portA = np.argmin(np.sum((X - (X.min(0) + [3, 3])) ** 2, 1))      # kick port
portB = np.argmin(np.sum((X - (X.max(0) - [3, 3])) ** 2, 1))      # snare port
far = np.argmin(np.sum((X - X.mean(0)) ** 2, 1))
# control-rate features (one-way: audio -> medium); 1-pole envelope of |x| low/high bands
from scipy.signal import butter, sosfilt
lo = sosfilt(butter(2, 150, "low", fs=FS, output="sos"), x); hi = sosfilt(butter(2, 2000, "high", fs=FS, output="sos"), x)
nb = len(x) // BLK; envL = np.zeros(nb); envH = np.zeros(nb); a_env = np.exp(-1 / (0.005 * FC)); eL = eH = 0.0
for i in range(nb):
    eL = max(np.sqrt(np.mean(lo[i * BLK:(i + 1) * BLK] ** 2)), a_env * eL); envL[i] = eL
    eH = max(np.sqrt(np.mean(hi[i * BLK:(i + 1) * BLK] ** 2)), a_env * eH); envH[i] = eH
thL, thH = 0.5 * envL.max(), 0.5 * envH.max(); rL, rH = 0.2 * envL.max(), 0.2 * envH.max()   # hysteresis on/off
ringA = [j for j in range(lat.N) if 3.5 < np.linalg.norm(X[j] - X[portA]) < 4.5]
ringB = [j for j in range(lat.N) if 3.5 < np.linalg.norm(X[j] - X[portB]) < 4.5]
true_on = [(i0 / FS, k) for i0, k in onsets]
def rising(u): return np.nonzero((u[1:] > 0.5) & (u[:-1] <= 0.5))[0] + 1
def run_d4(S):
    M = Barkley(lat, dt=DT_CTRL); trig = []; armL = armH = True; uA, uB, uF, rA, rB = [], [], [], [], []; cf = []
    for i in range(nb):
        stim = None
        if armL and envL[i] > thL:
            stim = np.zeros(lat.N); stim[portA] = 1; stim[lat.Adj[portA].indices] = 1; trig.append((i, "A")); armL = False
        elif envL[i] < rL: armL = True
        if armH and envH[i] > thH:
            stim = np.zeros(lat.N) if stim is None else stim; stim[portB] = 1; stim[lat.Adj[portB].indices] = 1; trig.append((i, "B")); armH = False
        elif envH[i] < rH: armH = True
        if stim is not None:   # counterfactual: does this stimulus create excitation that would not exist otherwise?
            import copy
            Ma, Mb = copy.deepcopy(M), copy.deepcopy(M); Ma.step(S, stim); Mb.step(S); dmax = 0.0
            for _ in range(int(2.0 / (DT_CTRL * S))):
                Ma.step(S); Mb.step(S); dmax = max(dmax, float(np.sum(np.maximum(Ma.u - Mb.u, 0) > 0.5)))
            cf.append(dmax)
        M.step(S, stim); uA.append(M.u[portA]); uB.append(M.u[portB]); uF.append(M.u[far]); rA.append(M.u[ringA].max()); rB.append(M.u[ringB].max())
    uA, uB, uF, rA, rB = map(np.array, (uA, uB, uF, rA, rB))
    det = [(i / FC, p) for i, p in trig]; lat_ms = []; matched = set()
    for t_on, k in true_on:
        p = "A" if k == "kick" else "B"; c = [(t, j) for j, (t, q) in enumerate(det) if q == p and 0 <= t - t_on < 0.05]
        if c: lat_ms.append(1000 * (c[0][0] - t_on)); matched.add(c[0][1])
    win = int(2.0 / (DT_CTRL * S))
    def launched_from(i, r):
        seg = r[i:i + win]; return bool(np.any((seg[1:] > 0.5) & (seg[:-1] <= 0.5)))
    launched = {p: int(sum(1 for (i, q), d in zip(trig, cf) if q == p and d >= 10)) for p in ("A", "B")}
    tau = TAU / S
    out = dict(tau_s=tau, substeps=S, refractory_period_s_approx=5.0 * tau, triggers=len(trig), triggers_A=sum(p == "A" for _, p in trig), triggers_B=sum(p == "B" for _, p in trig),
               triggers_matched_to_true_onsets=len(matched), extra_triggers=len(trig) - len(matched), detected_onsets=len(lat_ms),
               detection_latency_ms_median=float(np.median(lat_ms)) if lat_ms else None, launched_waves=launched,
               launch_rate=(launched["A"] + launched["B"]) / max(1, len(trig)), counterfactual_extra_excited_sites=[int(d) for d in cf], far_site_activations=len(rising(uF)))
    return out, (uA, uB, uF)
D4 = {}; traces = None
for S in (1, 2, 4):
    D4[S], tr = run_d4(S)
    if S == 2: traces = tr
    print("D4", S, D4[S], flush=True)
uA, uB, uF = traces
R["D4_audio_trigger"] = dict(loop_s=len(x) / FS, true_onsets=len(onsets), kicks=sum(k == "kick" for _, k in onsets), snares=sum(k == "snare" for _, k in onsets),
                             note="launched = counterfactual test: with vs without that stimulus, >=10 extra sites excited (u diff>0.5) within 2 model units; failures = stimulus in refractory tail; S substeps per 750 Hz tick => medium time scale tau=TAU/S",
                             by_substeps=D4)
# figures
fig, axs = plt.subplots(1, 3, figsize=(15, 4.6))
_, snap = run_spiral(32, DT_CTRL, 0.25, 0.5, False, T=60, snap_at=50.0)
u, c, l = snap; ax = axs[0]
ax.scatter(l.coords[:, 0], l.coords[:, 1], c=u, s=8, cmap="inferno", vmin=0, vmax=1)
hc = l.coords[l.hexes].mean(1); nz = c != 0
ax.scatter(hc[nz, 0], hc[nz, 1], marker="o", facecolors="none", edgecolors=["cyan" if q > 0 else "lime" for q in c[nz]], s=120, lw=2)
ax.set_title(f"Spiral, 32x32 cells (2048 sites), t=50 (={50*TAU:.2f}s)\ncircles: plaquette winding charge"); ax.set_aspect("equal"); ax.axis("off")
ax = axs[1]; Ls = list(D1.keys())
ax.bar([str(k) for k in Ls], [D1[k]["spiral_survival_frac"] for k in Ls]); ax.set_ylim(0, 1.05)
ax.set_title(f"D1 spiral survival after 300 units ({300*TAU:.0f}s), 20 seeds/size\ncontrol-rate dt={DT_CTRL:.3f}"); ax.set_xlabel("L (cells per side)")
ax = axs[2]; tt = np.arange(nb) / FC
ax.plot(tt, uA, lw=.8, label="u @ kick port"); ax.plot(tt, uB, lw=.8, label="u @ snare port"); ax.plot(tt, uF, lw=.8, label="u @ centre")
for t_on, k in true_on: ax.axvline(t_on, color="r" if k == "kick" else "b", alpha=.15)
ax.set_xlim(0, 4); ax.set_xlabel("s"); ax.legend(fontsize=7); ax.set_title("D4 audio-driven triggering (drum loop, 16x16, tau=%.0f ms)" % (1000 * TAU / 2))
plt.tight_layout(); plt.savefig(f"{FIG}/p1_d_excitable.png", dpi=130)
R["wall_s"] = time.time() - t0
json.dump(R, open(f"{OUT}/p1_d_excitable.json", "w"), indent=1, default=float)
print("done", R["wall_s"])
