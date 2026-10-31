# SPDX-License-Identifier: AGPL-3.0-only
"""Signal-complexity metrics for Phase-2 exp3 (all implemented here from their textbook definitions).
  frame_features : spectral flatness (Wiener entropy) and spectral centroid per STFT frame
  dfa            : detrended fluctuation analysis, order 1 (Peng et al. 1994) -> exponent alpha
  psd_slope      : Welch PSD log-log slope beta (1/f^beta)
  sampen / mse   : sample entropy (Richman & Moorman 2000); multiscale SE by coarse graining (Costa et al. 2002)
  lle_rosenstein : largest Lyapunov exponent from a time series (Rosenstein et al. 1993)"""
import numpy as np
from scipy.signal import welch
from scipy.spatial import cKDTree


def frame_features(x, fs, nfft=2048, hop=512, fmin=50.0, fmax=16000.0):
    w = np.hanning(nfft); nfr = 1 + (len(x) - nfft) // hop
    idx = np.arange(nfft)[None, :] + hop * np.arange(nfr)[:, None]
    X = np.abs(np.fft.rfft(x[idx] * w, axis=1)) ** 2 + 1e-30
    f = np.fft.rfftfreq(nfft, 1 / fs); b = (f >= fmin) & (f <= fmax)
    P = X[:, b]
    flat = np.exp(np.mean(np.log(P), 1)) / np.mean(P, 1)
    cent = (P * f[b]).sum(1) / P.sum(1)
    rms = np.sqrt(np.mean(x[idx] ** 2, 1))
    return dict(flatness=flat, centroid=cent, rms=rms, frame_rate=fs / hop)


def dfa(x, smin=8, smax=None, nscales=20):
    x = np.asarray(x, float); y = np.cumsum(x - x.mean()); N = len(y)
    smax = smax or N // 8
    S = np.unique(np.round(np.logspace(np.log10(smin), np.log10(smax), nscales)).astype(int))
    F = []
    for s in S:
        nseg = N // s; seg = y[:nseg * s].reshape(nseg, s); t = np.arange(s)
        A = np.vstack([t, np.ones(s)]).T; coef, *_ = np.linalg.lstsq(A, seg.T, rcond=None)
        res = seg.T - A @ coef; F.append(np.sqrt(np.mean(res ** 2)))
    F = np.array(F); ls, lf = np.log10(S), np.log10(F)
    a, b = np.polyfit(ls, lf, 1); r2 = 1 - np.sum((lf - (a * ls + b)) ** 2) / np.sum((lf - lf.mean()) ** 2)
    mid = len(S) // 2
    a_short = np.polyfit(ls[:mid + 1], lf[:mid + 1], 1)[0]; a_long = np.polyfit(ls[mid:], lf[mid:], 1)[0]
    return dict(alpha=float(a), r2=float(r2), alpha_short=float(a_short), alpha_long=float(a_long), scales=S.tolist(), F=F.tolist())


def psd_slope(x, fs, fmin, fmax):
    f, P = welch(x - np.mean(x), fs=fs, nperseg=min(len(x), 1024))
    b = (f >= fmin) & (f <= fmax)
    beta = -np.polyfit(np.log10(f[b]), np.log10(P[b]), 1)[0]
    return float(beta), f.tolist(), P.tolist()


def sampen(x, m=2, r=None):
    x = np.asarray(x, float); r = 0.15 * np.std(x) if r is None else r
    def count(mm):
        emb = np.lib.stride_tricks.sliding_window_view(x, mm)[: len(x) - m]
        t = cKDTree(emb)
        return t.count_neighbors(t, r, p=np.inf) - len(emb)       # ordered pairs, excluding self matches
    B, A = count(m), count(m + 1)
    return float(-np.log(A / B)) if A > 0 and B > 0 else float("nan")


def mse(x, scales=range(1, 13), m=2):
    r = 0.15 * np.std(x)
    out = []
    for s in scales:
        n = len(x) // s; cg = np.asarray(x[: n * s], float).reshape(n, s).mean(1)
        out.append(sampen(cg, m, r))
    return out


def lle_rosenstein(x, fs, dim=8, tau=None, theiler=None, kmax=150, fit=(5, 60)):
    x = np.asarray(x, float); x = (x - x.mean()) / (x.std() + 1e-30)
    if tau is None:
        ac = np.correlate(x[:4096], x[:4096], "full")[4095:]; z = np.where(ac <= 0)[0]
        tau = int(z[0]) if len(z) else 1
    M = len(x) - (dim - 1) * tau
    emb = np.stack([x[i * tau: i * tau + M] for i in range(dim)], 1)
    if theiler is None:
        theiler = 4 * tau
    tree = cKDTree(emb[: M - kmax]); Mv = M - kmax
    d, j = tree.query(emb[:Mv], k=2 * theiler + 8)
    nn = np.full(Mv, -1)
    for i in range(Mv):
        ok = np.abs(j[i] - i) > theiler
        ok &= j[i] < Mv
        if ok.any(): nn[i] = j[i][ok][0]
    sel = nn >= 0; ii = np.where(sel)[0]; jj = nn[sel]
    div = []
    for k in range(kmax):
        dk = np.linalg.norm(emb[ii + k] - emb[jj + k], axis=1)
        div.append(np.mean(np.log(dk[dk > 0])))
    div = np.array(div); t = np.arange(kmax) / fs
    lam = np.polyfit(t[fit[0]:fit[1]], div[fit[0]:fit[1]], 1)[0]
    return dict(lle_per_s=float(lam), tau=tau, dim=dim, theiler=theiler, curve=div.tolist())
