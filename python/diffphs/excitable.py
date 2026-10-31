# SPDX-License-Identifier: AGPL-3.0-only
"""Barkley excitable medium (D. Barkley, Physica D 49 (1991) 61-70) on the honeycomb site graph.
   du/dt = u(1-u)(u-(v+b)/a)/eps + D (L u),   dv/dt = u - v     (L = graph Laplacian, <= 0)
Explicit Euler in model time; one control tick = S substeps.  Vectorised over a batch axis (N,B)."""
import numpy as np

class Barkley:
    def __init__(self, lat, a=0.75, b=0.03, eps=0.02, D=0.5, dt=0.004):
        self.lat, self.a, self.b, self.eps, self.D, self.dt = lat, a, b, eps, D, dt
        self.Lap = lat.Lap.tocsr(); self.hexes = lat.hexes
        self.u = np.zeros(lat.N); self.v = np.zeros(lat.N)
    def step(self, n=1, stim=None):
        u, v = self.u, self.v
        for _ in range(n):
            if stim is not None:
                u = np.minimum(u + stim, 1.0)
            ut = (v + self.b) / self.a
            u, v = u + self.dt * (u * (1 - u) * (u - ut) / self.eps + self.D * (self.Lap @ u)), v + self.dt * (u - v)
            stim = None
        self.u, self.v = u, v
    def charges(self, uc=0.45, vc=0.25):
        th = np.arctan2(self.v - vc, self.u - uc)
        hx = self.hexes; d = np.diff(th[np.c_[hx, hx[:, :1]]], axis=1)
        d = (d + np.pi) % (2 * np.pi) - np.pi
        return np.round(d.sum(1) / (2 * np.pi)).astype(int)
    def broken_front(self, x0frac=0.25, yfrac=0.5, flip=False):
        X = self.lat.coords; xs = X[:, 0] - X[:, 0].min(); ys = X[:, 1] - X[:, 1].min()
        xp = xs - ys / np.sqrt(3); W, H = xp.max(), ys.max()
        sel = (ys < yfrac * H) if not flip else (ys >= yfrac * H)
        self.u[:] = 0; self.v[:] = 0
        self.u[(xp >= x0frac * W) & (xp < x0frac * W + 1.5) & sel] = 1
        self.v[(xp < x0frac * W) & sel] = 0.5
