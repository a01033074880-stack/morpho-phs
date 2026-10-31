# SPDX-License-Identifier: AGPL-3.0-only
"""Phase 5 item 5: E-RSAV (relaxed SAV) vs plain ESAV-LF on the Phase-2 exp1 alias test (identical setup:
L=8 gyroscopic honeycomb, 3100 Hz sinusoidal drive at levels 0.03 / 0.3 / 1.0, pickup a few sites away, SNHR =
harmonic / non-harmonic power below 20 kHz, reference = 8x oversampled RK4 + zero-phase low-pass + decimation).
Variants: ESAV (plain), ESAV+RSAV-clamp (= C++ Config::ersav), ESAV+RSAV-budget (Python only).
Also measures the discrete-energy identity and passivity (undriven decay) for each variant.
Results -> experiments/results/phase5/p5_ersav_alias.json"""
import json, os, sys, time
import numpy as np
from scipy.signal import resample_poly
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../.."))
from diffphs.gyro import GyroHoneycomb, Params, audio_params, U
from diffphs.gyro_schemes import SCHEMES
OUT = os.path.join(os.path.dirname(__file__), "../../../experiments/results/phase5"); os.makedirs(OUT, exist_ok=True)
L = int(os.environ.get("L", 8))
AP = audio_params(); m0 = AP["m"]; kc = AP["kc"]; k0 = AP["k0"]
lat = GyroHoneycomb(L, L, k0, kc); N, nd = lat.N, lat.ndof
fs = 48000; h = 1 / fs; f0 = 3100.0; Tsec = 0.35; nA = 12000
levels = [0.03, 0.3, 1.0]; B = len(levels)
P = Params(N, B, m=m0, alpha=kc / 3, beta=0.0, g=AP["g"], mu=-200.0, eta=0.0)
src = 2 * (N // 2 + L // 2); pick = 2 * (N // 2 + L // 2 + 3)


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


q0 = np.zeros((nd, B)); v0 = np.zeros((nd, B))
out = dict(setup=dict(L=L, f0=f0, levels=levels, fs=fs, T=Tsec, mu=-200.0, alpha="kc/3"), results={})
OS = 8; s = SCHEMES["RK4"](lat, h / OS, P, q0, v0); ys = []; t0 = time.time()
for n in range(int(Tsec * fs * OS)):
    ys.append(s.step(drive(n * h / OS))[pick].copy())
yref = resample_poly(np.array(ys), 1, OS, axis=0, window=("kaiser", 12.0))
snr, Yref, fr = analyze(yref, fs)
out["results"]["reference_RK4_x8"] = dict(SNHR_dB=snr.tolist(), wall_s=time.time() - t0)
print("reference", np.round(snr, 2), flush=True)
VAR = [("ESAV", None), ("ESAV+RSAV-clamp", "clamp"), ("ESAV+RSAV-budget", "budget")]
for name, rs in VAR:
    s = SCHEMES["ESAV"](lat, h, P, q0, v0, rsav=rs); ys = []; t0 = time.time(); ident = -np.inf; prise = -np.inf; incons = []
    E_prev = s.e.energy()
    for n in range(int(Tsec * fs)):
        f = drive(n * h)
        vb = s.step(f)
        ys.append(vb[pick].copy())
        # energy identity: E^{n+1/2} - E^{n-1/2} = h*(-diss + input) - (removed by RSAV); check the passive bound
        diss, pin = s.e.last_power
        E = s.e.energy(); rise = E - E_prev - h * pin           # <= -h*diss (equality without RSAV)
        ident = max(ident, float(np.max((rise - h * diss) / np.maximum(np.abs(E), 1e-30))))   # > 0 only if RSAV adds energy
        prise = max(prise, float(np.max(rise / np.maximum(np.abs(E), 1e-30))))                 # passivity: must be <= ~0
        if n % 200 == 0:
            a = P.dof("alpha"); b = P.dof("beta")
            u2 = 2 * U(s.e.q, a, b)   # same normalisation as EsavLFMT::sav_inconsistency(): sum|psi^2-2U-c0| / sum 2U
            incons.append((np.sum(np.abs(s.e.psi ** 2 - u2 - 1.0), 0) / np.maximum(np.sum(u2, 0), 1e-300)).tolist())
        E_prev = E
    y = np.array(ys)
    sn, Y, _ = analyze(y, fs)
    herr = []
    for k in (1, 3, 5):
        i = int(round(k * f0 * nA / fs)); herr.append((10 * np.log10(Y[i - 2:i + 3].sum(0) / Yref[i - 2:i + 3].sum(0))).tolist())
    # passivity: undriven decay from a strong random state (mu = -200 < 0): E must never rise
    rng = np.random.default_rng(3)
    s2 = SCHEMES["ESAV"](lat, h, P, 0.3 * rng.standard_normal((nd, B)), 3000 * rng.standard_normal((nd, B)), rsav=rs)
    Ep = s2.e.energy(); E0 = Ep.copy(); worst = -np.inf
    for n in range(4000):
        s2.step(0.0); E = s2.e.energy(); worst = max(worst, float(np.max((E - Ep) / E0))); Ep = E
    out["results"][name] = dict(SNHR_dB=sn.tolist(), dSNHR_vs_ESAV_dB=None, harmonic_err_dB_k135=herr,
                                energy_balance_excess_rel=ident, passive_rise_max_rel=prise, sav_inconsistency_mean_per_level=np.mean(np.array(incons), 0).tolist(),
                                passive_decay_max_rise_rel=worst, passive_E_end_over_E0=(Ep / E0).tolist(), wall_s=time.time() - t0)
    print(name, "SNHR", np.round(sn, 2), "herr", np.round(np.array(herr), 2).tolist(), "excess", ident, "rise", prise, "incons", np.round(np.mean(np.array(incons), 0), 4).tolist(),
          "max rise", worst, flush=True)
base = np.array(out["results"]["ESAV"]["SNHR_dB"])
for name, _ in VAR:
    out["results"][name]["dSNHR_vs_ESAV_dB"] = (np.array(out["results"][name]["SNHR_dB"]) - base).tolist()
    out["results"][name]["dSNHR_vs_reference_dB"] = (np.array(out["results"][name]["SNHR_dB"]) - snr).tolist()
json.dump(out, open(os.path.join(OUT, "p5_ersav_alias.json"), "w"), indent=1)
print("saved", flush=True)
