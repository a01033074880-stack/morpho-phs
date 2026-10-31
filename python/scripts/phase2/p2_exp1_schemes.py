# SPDX-License-Identifier: AGPL-3.0-only
"""Phase 2 exp1: numerical stability and throughput of six schemes on the 2-DOF gyroscopic honeycomb.
STAGE=check  : unit checks (IMDG exact power balance, SSDG/ESAV invariants)
STAGE=drift  : lossless energy drift vs CFL number (fs sweep)
STAGE=stress : divergence rate on randomized stress suites (impulse / active mu levels / modulated)
STAGE=alias  : SNHR / aliasing under sinusoidal drive vs an 8x-oversampled band-limited RK4 reference
STAGE=rtf    : Python throughput (us/sample) per scheme, L=8 and L=16, B=1
Results -> experiments/results/phase2/exp1_<stage>.json"""
import json, os, sys, time
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../.."))
from diffphs.gyro import GyroHoneycomb, Params, audio_params
from diffphs.gyro_schemes import SCHEMES, H_cont
OUT = os.path.join(os.path.dirname(__file__), "../../../experiments/results/phase2"); os.makedirs(OUT, exist_ok=True)
STAGE = os.environ.get("STAGE", "check"); L = int(os.environ.get("L", 8))
AP = audio_params(); m0 = AP["m"]; kc = AP["kc"]; k0 = AP["k0"]
lat = GyroHoneycomb(L, L, k0, kc); N, nd = lat.N, lat.ndof
ETA0 = 4 * 60.0 / (3 * 1000.0 ** 2)
ORDER = ["ESAV", "ESAVgate", "RK4", "Verlet", "TanhFB", "IMDG", "SSDG"]


def make(name, h, P, q0, v0, E_cap=np.inf):
    if name == "ESAVgate":
        return SCHEMES["ESAV"](lat, h, P, q0, v0, E_cap=E_cap)
    return SCHEMES[name](lat, h, P, q0, v0)


def rand_params(rng, B, mu_lo=-50, mu_hi=0, eta_scale=(0, 2), alpha_dec=(0, 0.3)):
    m = m0 * 2 ** rng.uniform(-1, 1, (N, B))
    al = (kc / 3) * 10 ** rng.uniform(*alpha_dec, (N, B)); be = k0 * rng.uniform(0, 2, (N, B))
    g = AP["g"] * rng.uniform(-1.5, 1.5, (N, B))
    mu = rng.uniform(mu_lo, mu_hi, (N, B)); eta = ETA0 * rng.uniform(*eta_scale, (N, B))
    return Params(N, B, m=m, alpha=al, beta=be, g=g, mu=mu, eta=eta)


def save(name, d):
    with open(os.path.join(OUT, f"exp1_{name}.json"), "w") as fh:
        json.dump(d, fh, indent=1)
    print("saved", name, flush=True)


if STAGE == "check":
    rng = np.random.default_rng(1); B = 3; h = 1 / 48000
    P = rand_params(rng, B, mu_lo=-50, mu_hi=50, eta_scale=(0.5, 2))
    q0 = 0.5 * rng.standard_normal((nd, B)); v0 = 3000 * rng.standard_normal((nd, B))
    res = {}
    s = SCHEMES["IMDG"](lat, h, P, q0, v0); worst = 0
    for n in range(300):
        f = 1e7 * rng.standard_normal((nd, B)) * (n % 50 == 0)
        H0 = s.H(); vb = s.step(f); H1 = s.H()
        pw = h * np.sum(vb * (f - np.repeat(-P.mu + P.eta * (vb[0::2] ** 2 + vb[1::2] ** 2), 2, 0) * vb), 0)
        worst = max(worst, float(np.max(np.abs(H1 - H0 - pw) / np.maximum(H0, H1))))
    res["IMDG_power_balance_maxrel"] = worst; res["IMDG_newton_iters_mean"] = float(np.mean(s.iters))
    P0 = rand_params(rng, B); P0.mu[:] = 0; P0.eta[:] = 0; P0.alpha[:] = 0; P0.beta[:] = 0
    s = SCHEMES["SSDG"](lat, h, P0, q0, v0); E0 = s.H()
    for n in range(2000): s.step(0.0)
    res["SSDG_linear_lossless_rel_drift_2000"] = float(np.max(np.abs(s.H() / E0 - 1)))
    s = SCHEMES["ESAV"](lat, h, P0, q0, v0); E0 = s.inv()
    for n in range(2000): s.step(0.0)
    res["ESAV_linear_lossless_rel_drift_2000"] = float(np.max(np.abs(s.inv() / E0 - 1)))
    print(res); save("check", res)

elif STAGE == "drift":
    rng = np.random.default_rng(2); B = 4; T_phys = float(os.environ.get("TPHYS", 0.25))
    P = rand_params(rng, B); P.mu[:] = 0; P.eta[:] = 0
    q0 = 0.3 * rng.standard_normal((nd, B)); v0 = 2000 * rng.standard_normal((nd, B))
    wmax = lat.omega_max(m0 * 0.5)
    out = dict(T_phys=T_phys, omega_max=wmax, runs=[])
    for fs in [48000, 24000, 16000, 12500]:
        h = 1 / fs; nsteps = int(T_phys * fs); stride = max(1, nsteps // 400)
        for name in ["ESAV", "RK4", "Verlet", "IMDG", "SSDG"]:
            s = make(name, h, P, q0, v0); Hc0, I0 = s.H(), s.inv(); t0 = time.time()
            Hs, Is = [], []; bad = False
            for n in range(nsteps):
                s.step(0.0)
                if n % stride == 0 or n == nsteps - 1:
                    Hc, I = s.H(), s.inv()
                    if not np.all(np.isfinite(Hc)): bad = True; break
                    Hs.append(Hc); Is.append(I)
            Hs, Is = np.array(Hs), np.array(Is)
            with np.errstate(all="ignore"):
                dH = 10 * np.log10(np.abs(Hs / Hc0)); dI = 10 * np.log10(np.abs(Is / I0))
            r = dict(fs=fs, h_omega_max=h * wmax, scheme=name, diverged=bad,
                     H_maxabs_dB=float(np.nanmax(np.abs(dH))) if len(Hs) else None, H_final_dB=float(np.mean(dH[-1])) if len(Hs) else None,
                     inv_maxabs_dB=float(np.nanmax(np.abs(dI))) if len(Is) else None, inv_final_dB=float(np.mean(dI[-1])) if len(Is) else None,
                     trace_inv_dB=np.nanmean(dI, 1).tolist()[::4], wall_s=time.time() - t0)
            out["runs"].append(r); print(fs, name, r["inv_maxabs_dB"], r["H_maxabs_dB"], bad, round(r["wall_s"], 1), flush=True)
            save("drift", out)

elif STAGE == "stress":
    B = int(os.environ.get("B", 16)); T = int(os.environ.get("T", 4800)); h = 1 / 48000
    suites = [("impulse", dict(active=0, mod=False)), ("active_mu60", dict(active=60, mod=False)), ("active_mu600", dict(active=600, mod=False)),
              ("active_mu6000", dict(active=6000, mod=False)), ("active_mu30000", dict(active=30000, mod=False)), ("modulated", dict(active=0, mod=True)),
              ("active_mu600_modulated", dict(active=600, mod=True))]
    only = os.environ.get("SUITES"); suites = [s for s in suites if not only or s[0] in only.split(",")]
    fn = os.path.join(OUT, os.environ.get("STRESS_FILE", "exp1_stress.json"))
    out = json.load(open(fn)) if os.path.exists(fn) else dict(B=B, T=T, suites={})
    for sname, cfg in suites:
        rng = np.random.default_rng(abs(hash(sname)) % 2 ** 31 if False else sum(map(ord, sname)))
        act = cfg["active"]
        P = rand_params(rng, B, mu_lo=0 if act else -50, mu_hi=act if act else 0, eta_scale=(0.5, 2) if act else (0, 2), alpha_dec=(-1, 1.5))
        q0 = 10 ** rng.uniform(-1, 1.3, (1, B)) * rng.standard_normal((nd, B)); v0 = 10 ** rng.uniform(2, 4.5, (1, B)) * rng.standard_normal((nd, B))
        pulses = [(int(rng.integers(0, T)), 10 ** rng.uniform(6, 9) * rng.standard_normal((nd, B))) for _ in range(8)]
        pd = dict(pulses)
        mods = []
        if cfg["mod"]:
            for k in range(T // 64):
                Pn = rand_params(rng, B, mu_lo=0 if act else -50, mu_hi=act if act else 0, eta_scale=(0.5, 2) if act else (0, 2), alpha_dec=(-1, 1.5)); mods.append(Pn)
        Ecap = float(np.max(H_cont(lat, P, q0, v0))) * 10 + 1e6
        res = {}
        for name in ORDER:
            s = make(name, h, P, q0, v0, E_cap=Ecap); t0 = time.time(); div = np.zeros(B, bool); first = np.full(B, -1)
            Hmax = np.zeros(B)
            for n in range(T):
                if cfg["mod"] and n % 64 == 0 and n // 64 < len(mods): s.set_params(mods[n // 64])
                f = pd.get(n, 0.0)
                with np.errstate(all="ignore"):
                    s.step(f)
                if n % 16 == 0 or n == T - 1:
                    with np.errstate(all="ignore"):
                        Hc = s.H(); qa = np.max(np.abs(s.q), 0)
                    bad = ~np.isfinite(Hc) | (Hc > 1e15) | ~np.isfinite(qa) | (qa > 1e6)
                    newly = bad & ~div; first[newly] = n; div |= bad
                    Hmax = np.where(div, Hmax, np.maximum(Hmax, np.nan_to_num(Hc)))
                    if div.all(): break
            extra = {}
            if name == "IMDG": extra = dict(newton_fail=int(s.fail.sum()), newton_iters_mean=float(np.mean(s.iters)))
            res[name] = dict(div_rate=float(div.mean()), n_div=int(div.sum()), first_div_sample=first.tolist(), log10_Hmax_median_nondiv=float(np.median(np.log10(Hmax[~div] + 1))) if (~div).any() else None,
                             wall_s=time.time() - t0, **extra)
            print(sname, name, res[name]["n_div"], "/", B, extra, round(res[name]["wall_s"], 1), flush=True)
        out["suites"][sname] = dict(cfg=cfg, E_cap=Ecap, results=res)
        json.dump(out, open(fn, "w"), indent=1); print("saved stress", sname, flush=True)

elif STAGE == "alias":
    from scipy.signal import resample_poly
    fs = 48000; h = 1 / fs; f0 = 3100.0; Tsec = 0.35; nA = 12000
    levels = [0.03, 0.3, 1.0]; B = len(levels)
    P = Params(N, B, m=m0, alpha=kc / 3, beta=0.0, g=AP["g"], mu=-200.0, eta=0.0)
    src = 2 * (N // 2 + L // 2); pick = 2 * (N // 2 + L // 2 + 3)   # x dof of a central site, pickup a few sites away
    def drive(t):
        f = np.zeros((nd, B)); f[src] = np.array(levels) * kc * np.sin(2 * np.pi * f0 * t); return f
    def analyze(y, fsr):
        y = y[-nA:]; w = np.blackman(nA); Y = np.abs(np.fft.rfft((y - y.mean(0)) * w[:, None], axis=0)) ** 2
        fr = np.fft.rfftfreq(nA, 1 / fsr); band = fr <= 20000
        harm = np.zeros_like(fr, bool)
        for k in range(1, 8):
            harm |= np.abs(fr - k * f0) <= 3 * fsr / nA
        Ph = Y[band & harm].sum(0); Pn = Y[band & ~harm].sum(0)
        return 10 * np.log10(Ph / Pn), Y, fr
    q0 = np.zeros((nd, B)); v0 = np.zeros((nd, B)); out = dict(f0=f0, levels=levels, results={})
    # reference: RK4 at 8x, zero-phase polyphase low-pass to 20 kHz, decimate
    OS = 8; s = SCHEMES["RK4"](lat, h / OS, P, q0, v0); ys = []
    t0 = time.time()
    for n in range(int(Tsec * fs * OS)):
        ys.append(s.step(drive(n * h / OS))[pick].copy())
    yref = resample_poly(np.array(ys), 1, OS, axis=0, window=("kaiser", 12.0))
    sn, Yref, fr = analyze(yref, fs); out["results"]["reference_RK4_x8"] = dict(SNHR_dB=sn.tolist(), wall_s=time.time() - t0)
    print("ref", sn, flush=True)
    spec = dict(freq=fr[::4].tolist(), reference=(10 * np.log10(Yref[::4] + 1e-300)).T.tolist())
    for name in ["ESAV", "RK4", "Verlet", "IMDG", "SSDG"]:
        s = make(name, h, P, q0, v0); ys = []; t0 = time.time()
        with np.errstate(all="ignore"):
            for n in range(int(Tsec * fs)):
                ys.append(s.step(drive(n * h))[pick].copy())
        y = np.array(ys)
        if not np.all(np.isfinite(y)):
            out["results"][name] = dict(SNHR_dB=None, diverged=True); continue
        sn, Y, _ = analyze(y, fs)
        # harmonic-amplitude error vs reference (first 3 harmonics present below 20 kHz)
        herr = []
        for k in (1, 3, 5):
            i = int(round(k * f0 * nA / fs)); herr.append((10 * np.log10(Y[i - 2:i + 3].sum(0) / Yref[i - 2:i + 3].sum(0))).tolist())
        out["results"][name] = dict(SNHR_dB=sn.tolist(), harmonic_err_dB_k135=herr, wall_s=time.time() - t0)
        spec[name] = (10 * np.log10(Y[::4] + 1e-300)).T.tolist()
        print(name, sn, flush=True)
    save("alias", out); json.dump(spec, open(os.path.join(OUT, "exp1_alias_spectra.json"), "w"))

elif STAGE == "rtf":
    out = {}
    for LL in (8, 16):
        lt = GyroHoneycomb(LL, LL, k0, kc); globals()["lat"] = lt; NN, ndd = lt.N, lt.ndof
        rng = np.random.default_rng(3)
        globals()["N"] = NN
        P = rand_params(rng, 1); q0 = 0.1 * rng.standard_normal((ndd, 1)); v0 = 100 * rng.standard_normal((ndd, 1))
        for name in ["ESAV", "RK4", "Verlet", "TanhFB", "IMDG", "SSDG"]:
            s = make(name, 1 / 48000, P, q0, v0); n = 600 if name == "IMDG" else 3000
            for _ in range(50): s.step(0.0)
            t0 = time.perf_counter()
            for _ in range(n): s.step(0.0)
            us = (time.perf_counter() - t0) / n * 1e6
            out[f"L{LL}_{name}"] = dict(us_per_sample=us, RTF=us / (1e6 / 48000)); print(LL, name, round(us, 1), flush=True)
    save("rtf", out)
