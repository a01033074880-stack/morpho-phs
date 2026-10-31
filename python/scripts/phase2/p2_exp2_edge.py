# SPDX-License-Identifier: AGPL-3.0-only
"""Phase 2 exp2: chiral edge waves on the 2-DOF gyroscopic honeycomb (ESAV-LF, 48 kHz, demo parameters).
STAGE=impulse : broadband impulse responses at every bottom-edge site, Omega in {+,0,-} x absorber in
                {none, radial sponge (Phase-1 demo), frame}; -> frequency-resolved directionality D(f),
                group delay, distance sweep, drum-loop-weighted broadband ratios and in-gap filter fixes.
STAGE=burst   : in-gap / in-band tone bursts, Omega in {+,0,-}, defects {none, vacancy notch, heavy-mass cluster,
                Omega=0 patch}; -> space-time edge fields, backscattering ratio and transmission in dB.
Results -> experiments/results/phase2/exp2_*.json / .npz"""
import json, os, sys, time
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../.."))
from diffphs.gyro import GyroHoneycomb, GyroESAV, Params, audio_params
OUT = os.path.join(os.path.dirname(__file__), "../../../experiments/results/phase2"); os.makedirs(OUT, exist_ok=True)
STAGE = os.environ.get("STAGE", "impulse")
AP = audio_params(); h = AP["h"]; wu = AP["wu"]
Lx, Ly = 40, 14
ALPHA = 0.05 * wu ** 2           # demo value (quartic); responses below are in the linear regime
GAPS = json.load(open(os.path.join(OUT, "../phase1/p1_b_topology.json")))["gaps_Hz"]


def nearest(xy, x, y): return int(np.argmin((xy[:, 0] - x) ** 2 + (xy[:, 1] - y) ** 2))


def absorber(xy, kind, xs):
    N = len(xy)
    if kind == "none":
        return np.full(N, -8.0)
    if kind == "sponge":          # Phase-1 demo: radial, beyond 14 units from the source
        d = np.hypot(xy[:, 0] - xs, xy[:, 1]); r = np.clip((d - 14) / 6, 0, 1)
        return -8.0 - 3000.0 * r * r
    if kind == "frame":           # Phase-1 B3: graded damping on side and top frame, bottom edge corridor free
        u = xy[:, 0] - xy[:, 1] / np.sqrt(3)
        dside = np.maximum(0, np.maximum(u - (Lx - 7), 6 - u)); dtop = np.maximum(0, xy[:, 1] - (Ly * np.sqrt(3) / 2 - 5))
        return -8.0 - 4000.0 * (np.clip(dside / 6, 0, 1) ** 2 + np.clip(dtop / 5, 0, 1) ** 2)
    raise ValueError(kind)


if STAGE == "impulse":
    lat = GyroHoneycomb(Lx, Ly, AP["k0"], AP["kc"]); xy = lat.coords; N = lat.N
    xs = 20.0; src = nearest(xy, xs, 0.0)
    cols = [(o, a) for a in ("none", "sponge", "frame") for o in (+1, 0, -1)]; B = len(cols)
    mu = np.stack([absorber(xy, a, xs) for _, a in cols], 1)
    g = np.array([o * AP["g"] for o, _ in cols])[None, :]
    P = Params(N, B, m=1.0, alpha=ALPHA, beta=0.0, mu=mu, eta=0.0, g=g)
    bot = np.where(xy[:, 1] < 0.5)[0]; bot = bot[np.argsort(xy[bot, 0])]
    T = int(os.environ.get("T", 16384))
    I = GyroESAV(lat, h, P, np.zeros((2 * N, B)), np.zeros((2 * N, B)))
    rec = np.zeros((T, len(bot), 2, B), np.float32); t0 = time.time()
    A = 2e4                          # impulse amplitude (force * 1 sample); peak displacement << 1 -> linear regime
    for n in range(T):
        f = np.zeros((2 * N, B))
        if n == 0: f[2 * src] = A / h * 1e-4
        vb = I.step(f, return_vbar=True)
        rec[n, :, 0] = vb[2 * bot]; rec[n, :, 1] = vb[2 * bot + 1]
    print("impulse sim", time.time() - t0, "s; max |q|", float(np.abs(I.q).max()), flush=True)
    np.savez_compressed(os.path.join(OUT, "exp2_impulse.npz"), rec=rec, bot=bot, xb=xy[bot, 0], src=src, xs=xs,
                        cols=np.array([f"{o:+d}|{a}" for o, a in cols]), drive=A / h * 1e-4)
    print("saved", flush=True)

elif STAGE == "burst":
    xs, xdef = 16.0, 24.0
    base = GyroHoneycomb(Lx, Ly, AP["k0"], AP["kc"])
    notch = list(np.where((np.abs(base.coords[:, 0] - xdef) < 1.1) & (base.coords[:, 1] < 1.5))[0])
    f_gap1 = 0.5 * (GAPS[0]["lo"] + GAPS[0]["hi"]); f_gap2 = 0.5 * (GAPS[1]["lo"] + GAPS[1]["hi"]); f_bulk = 1300.0
    freqs = {"gap1": f_gap1, "gap2": f_gap2, "bulk1300": f_bulk}
    T = 7200; tt = np.arange(T) * h; tc, sg = 0.012, 0.004; env = np.exp(-0.5 * ((tt - tc) / sg) ** 2)
    out = dict(setup=dict(Lx=Lx, Ly=Ly, src_x=xs, defect_x=xdef, burst_sigma_s=sg, absorber="frame", freqs=freqs,
                          right="bottom corridor y<1.5, x in (27,33)", left="bottom corridor y<1.5, x in (6,11)"), runs={})
    spacetime = {}
    for dname in ("none", "vacancy_notch", "heavy_mass_x25", "Omega0_patch"):
        lat = GyroHoneycomb(Lx, Ly, AP["k0"], AP["kc"], remove_sites=notch if dname == "vacancy_notch" else ())
        xy = lat.coords; N = lat.N; src = nearest(xy, xs, 0.0)
        cols = [(o, fk) for o in (+1, 0, -1) for fk in freqs]; B = len(cols)
        m = np.ones((N, B)); g = np.tile(np.array([o * AP["g"] for o, _ in cols])[None, :], (N, 1))
        near = (np.abs(xy[:, 0] - xdef) < 1.6) & (xy[:, 1] < 2.0)
        if dname == "heavy_mass_x25": m[near] = 25.0
        if dname == "Omega0_patch": g[near] = 0.0
        mu = absorber(xy, "frame", xs)[:, None] * np.ones((1, B))
        P = Params(N, B, m=m, alpha=ALPHA, beta=0.0, mu=mu, eta=0.0, g=g)
        I = GyroESAV(lat, h, P, np.zeros((2 * N, B)), np.zeros((2 * N, B)))
        ybot = xy[:, 1] < 1.5; xx = xy[:, 0]
        right = ybot & (xx > 27) & (xx < 33); left = ybot & (xx > 6) & (xx < 11)
        bot = np.where(xy[:, 1] < 0.5)[0]; bot = bot[np.argsort(xx[bot])]
        fr = np.array([freqs[fk] for _, fk in cols]); Er = np.zeros(B); El = np.zeros(B)
        st = np.zeros((T // 8, len(bot), B), np.float32); kin_all = np.zeros(B)
        for n in range(T):
            f = np.zeros((2 * N, B)); f[2 * src] = 3e5 * env[n] * np.sin(2 * np.pi * fr * tt[n])
            vb = I.step(f, return_vbar=True)
            e = 0.5 * (vb[0::2] ** 2 + vb[1::2] ** 2)
            Er += e[right].sum(0); El += e[left].sum(0)
            if n % 8 == 0: st[n // 8] = e[bot]
        for b, (o, fk) in enumerate(cols):
            out["runs"][f"{dname}|Omega{o:+d}|{fk}"] = dict(f_Hz=float(fr[b]), E_right=float(Er[b]), E_left=float(El[b]))
        spacetime[dname] = dict(st=st, xb=xx[bot], cols=[f"{o:+d}|{fk}" for o, fk in cols])
        print(dname, "done", flush=True)
    # derived metrics
    for k, r in out["runs"].items():
        d, o, fk = k.split("|"); c = out["runs"][f"none|{o}|{fk}"]
        r["right_over_left_dB"] = 10 * np.log10(r["E_right"] / r["E_left"])
        fwd, bwd = ("E_right", "E_left") if o != "Omega-1" else ("E_left", "E_right")
        r["forward"] = "right" if o != "Omega-1" else "left"
        r["backscatter_dB"] = 10 * np.log10(r[bwd] / c[fwd])          # back-travelling energy vs clean incident (forward) energy
        r["transmission_vs_clean_dB"] = 10 * np.log10(r[fwd] / c[fwd])
    json.dump(out, open(os.path.join(OUT, "exp2_burst.json"), "w"), indent=1)
    np.savez_compressed(os.path.join(OUT, "exp2_spacetime.npz"), **{f"{d}_st": v["st"] for d, v in spacetime.items()}, **{f"{d}_xb": v["xb"] for d, v in spacetime.items()},
                        xb=spacetime["none"]["xb"], cols=np.array(spacetime["none"]["cols"]), dt_ms=8 / 48.0)
    for k, r in out["runs"].items():
        print(k, round(r["right_over_left_dB"], 2), "bs", round(r["backscatter_dB"], 2), "T", round(r["transmission_vs_clean_dB"], 3))
