# SPDX-License-Identifier: AGPL-3.0-only
"""Phase-2 exp4: differentiable ESAV-LF (PyTorch, CPU) one-shot inverse design vs a modal filter-bank baseline.

Model (same scheme as python/diffphs/gyro.py GyroESAV, torch re-implementation, B = 1, float64):
  honeycomb flake LX x LY cells, fs = 16 kHz, impulse force (1 ms raised cosine) at one site,
  output = linear readout of the per-step mid-point velocity vbar (learned weights over all dofs).
Learned parameters (all through bounded reparametrisations so the CFL h*w_max < 2 always holds):
  f_u in [250, 1200] Hz (kc = (2 pi f_u)^2), k0/kc in [0.01, 0.5], g/w_u in [-1, 1], alpha/kc in [0, 0.05],
  per-site mass m_i in [0.5, 2], per-site damping -mu_i in [0.5, 200] 1/s (passive: mu <= 0),
  input direction (2), readout weights (2N).
Baseline: K exponentially-decaying sinusoids, params (log f, log decay, amp) -> 3K, K chosen to match the
  ESAV parameter count.  Same loss (multi-resolution STFT), same optimiser (Adam), same iteration budget.
Targets (all generated here, so licence = own work / CC0):
  T1 "esav_hidden": an ESAV-LF render with hidden random parameters (identifiability / in-class test);
  T2 "risset_bell": Risset's bell (Risset 1969 catalogue, partial ratios/durations as in the standard
      Csound/Dodge&Jerse reproduction) at 300 Hz, analytic synthesis;
  T3 "plucked_string": Karplus-Strong string at 196 Hz (harmonic, out-of-class for a 2-D lattice).
STAGE env: time | fit (TARGETS=..., ITERS=...)."""
import json, os, sys, time, math
import numpy as np
import torch
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))
from diffphs.gyro import GyroHoneycomb

torch.set_num_threads(int(os.environ.get("TORCH_THREADS", "1")))
torch.set_default_dtype(torch.float64)
ROOT = os.path.join(os.path.dirname(__file__), "..", "..", "..")
OUT = os.path.join(ROOT, "experiments", "results", "phase2", "exp4")
os.makedirs(OUT, exist_ok=True)
FS = 16000.0; H = 1.0 / FS
DUR = float(os.environ.get("DUR", "0.5")); T = int(FS * DUR)
LX, LY = int(os.environ.get("LX", "4")), int(os.environ.get("LY", "4"))
lat = GyroHoneycomb(LX, LY, 0.0, 1.0)        # unit-kc stiffness pattern; k0, kc scaled at run time
N = lat.N; NDOF = 2 * N
Kpat = torch.tensor((lat.K).toarray())       # = sum_bonds (e e^T)(x)nn^T  (k0 = 0, kc = 1)
LAMMAX = float(np.linalg.eigvalsh(Kpat.numpy()).max())
SRC = 0
C0 = 1.0


def sig(x, lo, hi): return lo + (hi - lo) * torch.sigmoid(x)


def esav_params(seed=0, scale=1.0):
    g = torch.Generator().manual_seed(seed)
    r = lambda *s: scale * torch.randn(*s, generator=g)
    return {"fu": r(1), "k0r": r(1), "gr": r(1), "al": r(1) - 2.0, "m": r(N), "dmp": r(N),
            "din": r(2), "w": 0.1 * torch.randn(NDOF, generator=g)}


def esav_physical(p):
    fu = sig(p["fu"], 250.0, 1200.0); wu = 2 * math.pi * fu; kc = wu ** 2
    return dict(fu=fu, kc=kc, k0=sig(p["k0r"], 0.01, 0.5) * kc, g=sig(p["gr"], -1.0, 1.0) * wu,
                alpha=sig(p["al"], 0.0, 0.05) * kc, m=sig(p["m"], 0.5, 2.0), dmp=sig(p["dmp"], 0.5, 200.0))


# NOTE on the psi recursion: GyroESAV keeps (psi^n, psi^{n-1}) and uses psi^{n-1}.  Implemented exactly below.
def esav_render_exact(p, T=T):
    ph = esav_physical(p)
    K = ph["kc"] * Kpat + ph["k0"] * torch.eye(NDOF)
    m = ph["m"].repeat_interleave(2); d = ph["dmp"].repeat_interleave(2)
    a = ph["alpha"]; g = ph["g"]
    din = p["din"] / (p["din"].norm() + 1e-9)
    nf = int(0.001 * FS); env = 0.5 * (1 - torch.cos(2 * math.pi * torch.arange(nf) / nf))
    idx = torch.tensor([2 * SRC, 2 * SRC + 1])
    fvec = torch.zeros(NDOF).index_put((idx,), din)
    q = torch.zeros(NDOF); qm = torch.zeros(NDOF)
    psi = torch.full((NDOF,), math.sqrt(C0)); psim = psi.clone()
    diag0 = m / H ** 2 + d / (2 * H); c = g / (2 * H)
    outs = []
    for n in range(T):
        gg = a * q ** 3 / torch.sqrt(0.5 * a * q ** 4 + C0)
        diag = diag0 + 0.5 * gg * gg
        rhs = 2 * m * (q - qm) / H ** 2 - K @ q - gg * psim
        if n < nf: rhs = rhs + 1e4 * env[n] * fvec
        ax, ay = diag[0::2], diag[1::2]; rx, ry = rhs[0::2], rhs[1::2]
        det = ax * ay + c * c
        w = torch.stack(((ay * rx + c * ry) / det, (ax * ry - c * rx) / det), 1).reshape(-1)
        qm, q = q, qm + w
        psim, psi = psi, psim + gg * w
        outs.append(w)
    return (torch.stack(outs, 0) / (2 * H)) @ p["w"]


def esav_energy_check(p, T=2000):
    """Discrete energy of the torch render must be non-increasing after the force ends (passive, no input)."""
    with torch.no_grad():
        ph = esav_physical(p)
        K = ph["kc"] * Kpat + ph["k0"] * torch.eye(NDOF)
        m = ph["m"].repeat_interleave(2); d = ph["dmp"].repeat_interleave(2); a = ph["alpha"]; g = ph["g"]
        q = torch.zeros(NDOF); q[0] = 1e-3; qm = torch.zeros(NDOF)
        psi = torch.sqrt(0.5 * a * q ** 4 + C0); psim = torch.full((NDOF,), math.sqrt(C0))
        c = g / (2 * H); E = []
        for n in range(T):
            v = (q - qm) / H
            E.append(float(0.5 * (m * v * v).sum() + 0.5 * q @ (K @ qm) + 0.25 * (psi ** 2 + psim ** 2).sum() - 0.5 * C0 * NDOF))
            gg = a * q ** 3 / torch.sqrt(0.5 * a * q ** 4 + C0)
            diag = m / H ** 2 + d / (2 * H) + 0.5 * gg * gg
            rhs = 2 * m * (q - qm) / H ** 2 - K @ q - gg * psim
            ax, ay = diag[0::2], diag[1::2]; rx, ry = rhs[0::2], rhs[1::2]; det = ax * ay + c * c
            w = torch.stack(((ay * rx + c * ry) / det, (ax * ry - c * rx) / det), 1).reshape(-1)
            qm, q = q, qm + w; psim, psi = psi, psim + gg * w
        E = np.array(E); return float(np.max(np.diff(E)) / E[0]), float(E[-1] / E[0])


# ---------------- modal baseline ----------------
def modal_params(K, seed=0):
    g = torch.Generator().manual_seed(seed)
    lf = torch.linspace(math.log(150.0), math.log(5000.0), K) + 0.02 * torch.randn(K, generator=g)
    return {"lf": lf, "ld": torch.full((K,), math.log(5.0)), "amp": 0.01 * torch.randn(K, generator=g)}


def modal_render(p, T=T):
    t = torch.arange(T) / FS
    f = torch.exp(p["lf"]).clamp(20.0, FS / 2 - 100); dcy = torch.exp(p["ld"])
    return (p["amp"][None, :] * torch.exp(-dcy[None, :] * t[:, None]) * torch.sin(2 * math.pi * f[None, :] * t[:, None])).sum(1)


# ---------------- loss ----------------
FFTS = (256, 512, 1024, 2048)
def stft_mag(x, n):
    return torch.stft(x, n, hop_length=n // 4, window=torch.hann_window(n), return_complex=True).abs()


def mrstft(x, y):
    tot = 0.0
    for n in FFTS:
        X, Y = stft_mag(x, n), stft_mag(y, n)
        sc = torch.linalg.norm(Y - X) / (torch.linalg.norm(Y) + 1e-12)
        lm = torch.mean(torch.abs(torch.log(X + 1e-5) - torch.log(Y + 1e-5)))
        tot = tot + sc + lm
    return tot / len(FFTS)


# ---------------- targets ----------------
def target(name):
    t = np.arange(T) / FS
    if name == "esav_hidden":
        p = esav_params(seed=12345, scale=1.0)
        with torch.no_grad(): y = esav_render_exact(p).numpy()
        meta = {k: (v.detach().numpy().round(6).tolist()) for k, v in esav_physical(p).items() if k in ("fu", "k0", "g", "alpha")}
        meta["source"] = "own render, hidden seed 12345"
    elif name == "risset_bell":
        f0 = 300.0
        # (amplitude, relative duration, frequency ratio, detune Hz) after Risset's bell (Dodge & Jerse 1997 table)
        P = [(1, 1, .56, 0), (.67, .9, .56, 1), (1, .65, .92, 0), (1.8, .55, .92, 1.7), (2.67, .325, 1.19, 0),
             (1.67, .35, 1.7, 0), (1.46, .25, 2, 0), (1.33, .2, 2.74, 0), (1.33, .15, 3, 0), (1, .1, 3.76, 0), (1.33, .075, 4.07, 0)]
        dur = 4.0; y = np.zeros(T)
        for A, rd, ra, dt in P:
            y += A * np.exp(-6.9 * t / (rd * dur)) * np.sin(2 * np.pi * (ra * f0 + dt) * t)
        meta = {"f0": f0, "source": "synthesised Risset bell (additive), own work"}
    elif name == "plucked_string":
        rng = np.random.default_rng(7); P = int(round(FS / 196.0)); buf = rng.uniform(-1, 1, P); y = np.zeros(T)
        for n in range(T):
            y[n] = buf[n % P]; buf[n % P] = 0.996 * 0.5 * (buf[n % P] + buf[(n + 1) % P])
        meta = {"f0": FS / P, "source": "Karplus-Strong, own work"}
    else:
        raise ValueError(name)
    y = y / np.max(np.abs(y)) * 0.5
    return torch.tensor(y), meta


def fit(render, params, y, iters, lr, tag):
    for v in params.values(): v.requires_grad_(True)
    opt = torch.optim.Adam(params.values(), lr=lr)
    hist = []; best = (1e9, None); t0 = time.time()
    for it in range(iters):
        opt.zero_grad()
        x = render(params)
        L = mrstft(x, y)
        if not torch.isfinite(L):
            hist.append(float("nan")); break
        L.backward()
        torch.nn.utils.clip_grad_norm_(list(params.values()), 10.0)
        opt.step()
        hist.append(float(L))
        if float(L) < best[0]: best = (float(L), {k: v.detach().clone() for k, v in params.items()})
        if it % 10 == 0 or it == iters - 1:
            print(f"[{tag}] it {it} loss {float(L):.4f} t {time.time()-t0:.0f}s", flush=True)
            # incremental checkpoint so an interruption loses at most 10 iterations of history
            ck = os.path.join(OUT, tag.replace("/", "_") + "_ckpt.json")
            json.dump({"tag": tag, "it": it, "hist": hist, "best_loss": best[0], "wall_s": time.time() - t0}, open(ck + ".tmp", "w"))
            os.replace(ck + ".tmp", ck)
    return best, hist, time.time() - t0


def nparams(p): return int(sum(v.numel() for v in p.values()))


def write_wav(path, x):
    from scipy.io import wavfile
    x = np.asarray(x, float); x = x / (np.max(np.abs(x)) + 1e-12) * 0.7
    wavfile.write(path, int(FS), (x * 32767).astype(np.int16))


if __name__ == "__main__":
    stage = os.environ.get("STAGE", "time")
    if stage == "time":
        p = esav_params(1)
        for v in p.values(): v.requires_grad_(True)
        y, _ = target("risset_bell")
        t0 = time.time(); x = esav_render_exact(p); t1 = time.time(); L = mrstft(x, y); L.backward(); t2 = time.time()
        dE, Erat = esav_energy_check(esav_params(3))
        print(json.dumps({"N": N, "ndof": NDOF, "T": T, "fwd_s": t1 - t0, "bwd_s": t2 - t1, "nparams_esav": nparams(p),
                          "lam_max_Kpat": LAMMAX, "energy_max_rel_increase": dE, "energy_ratio_end": Erat}))
    else:
        iters = int(os.environ.get("ITERS", "150"))
        audio = os.path.join(ROOT, "experiments", "audio", "phase2"); os.makedirs(audio, exist_ok=True)
        for name in os.environ.get("TARGETS", "esav_hidden,risset_bell,plucked_string").split(","):
            res_path = os.path.join(OUT, f"exp4_{name}.json"); res = {}
            y, meta = target(name)
            pe = esav_params(seed=1, scale=0.3)
            be, he, te = fit(esav_render_exact, pe, y, iters, 0.05, f"{name}/esav")
            K = max(1, nparams(pe) // 3)
            pm = modal_params(K)
            bm, hm, tm = fit(modal_render, pm, y, iters, 0.05, f"{name}/modal")
            with torch.no_grad():
                xe = esav_render_exact(be[1]); xm = modal_render(bm[1])
                L0 = float(mrstft(torch.zeros_like(y) + 1e-9, y))
            ph = {k: v.detach().numpy().round(5).tolist() for k, v in esav_physical(be[1]).items() if k in ("fu", "k0", "g", "alpha")}
            res[name] = {"meta": meta, "iters": iters, "loss_silence": L0,
                         "esav": {"nparams": nparams(pe), "best_loss": be[0], "final_loss": he[-1], "wall_s": te, "hist": he, "fitted_globals": ph},
                         "modal": {"nparams": nparams(pm), "K": K, "best_loss": bm[0], "final_loss": hm[-1], "wall_s": tm, "hist": hm}}
            np.savez(os.path.join(OUT, f"{name}_renders.npz"), target=y.numpy(), esav=xe.numpy(), modal=xm.numpy())
            for k, x in (("target", y.numpy()), ("esav_fit", xe.numpy()), ("modal_fit", xm.numpy())):
                write_wav(os.path.join(audio, f"exp4_{name}_{k}.wav"), x)
            json.dump(res, open(res_path, "w"), indent=1)
            print(json.dumps({name: {"esav": be[0], "modal": bm[0], "silence": L0}}), flush=True)
