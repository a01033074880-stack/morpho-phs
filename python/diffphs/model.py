# SPDX-License-Identifier: AGPL-3.0-only
"""Lattice port-Hamiltonian model of the proposal (scalar DOF per node) and helpers.

  H = sum p^2/(2m) + 1/2 q^T K q + sum U(q),   U(q) = alpha/4 q^4 + beta*logcosh(q)
  qdot = p/m
  pdot = -K q - U'(q) + G (p/m) - d(v) v + f,   v = p/m,  d(v) = -mu + eta v^2
G is real skew-symmetric (proposal: (G v)_i = sum_j sigma_ij sin(Phi) v_j).
States are column-batched: q, p have shape (N, B).
"""
import numpy as np
import scipy.sparse as sp


def logcosh(q):
    a = np.abs(q)
    return a + np.log1p(np.exp(-2.0 * a)) - np.log(2.0)


def U(q, alpha, beta):
    return 0.25 * alpha * q ** 4 + beta * logcosh(q)


def dU(q, alpha, beta):
    return alpha * q ** 3 + beta * np.tanh(q)


def d2U(q, alpha, beta):
    return 3.0 * alpha * q ** 2 + beta / np.cosh(np.clip(q, -350, 350)) ** 2


class Lattice:
    def __init__(self, K, G, N, coords=None, edge=None):
        self.K = sp.csr_matrix(K)
        self.G = sp.csr_matrix(G)
        self.N = N
        self.coords = coords
        self.edge = edge  # boolean mask of boundary nodes

    def energy(self, q, p, m, alpha, beta):
        Kq = self.K @ q
        return (np.sum(p * p / (2.0 * m), axis=0) + 0.5 * np.sum(q * Kq, axis=0)
                + np.sum(U(q, alpha, beta), axis=0))


def square_lattice(L, k0, kc, sigma, Phi, periodic=False):
    """L x L square lattice, scalar DOF, grounding spring k0, NN springs kc.
    Proposal-style gyroscopic coupling: sigma_ij = +sigma for j = right/up neighbour of i,
    -sigma for left/down (=> G skew)."""
    N = L * L
    idx = lambda x, y: (y % L) * L + (x % L)
    rows, cols, vals = [], [], []
    grows, gcols, gvals = [], [], []
    deg = np.zeros(N)
    for y in range(L):
        for x in range(L):
            i = idx(x, y)
            for dx, dy in ((1, 0), (0, 1)):
                xn, yn = x + dx, y + dy
                if not periodic and (xn >= L or yn >= L):
                    continue
                j = idx(xn, yn)
                rows += [i, j]; cols += [j, i]; vals += [-kc, -kc]
                deg[i] += 1; deg[j] += 1
                s = sigma * np.sin(Phi)
                grows += [i, j]; gcols += [j, i]; gvals += [s, -s]
    K = sp.coo_matrix((vals, (rows, cols)), shape=(N, N)).tocsr() + sp.diags(k0 + kc * deg)
    G = sp.coo_matrix((gvals, (grows, gcols)), shape=(N, N)).tocsr()
    xs, ys = np.meshgrid(np.arange(L), np.arange(L))
    coords = np.stack([xs.ravel(), ys.ravel()], 1).astype(float)
    edge = ((coords[:, 0] == 0) | (coords[:, 0] == L - 1) | (coords[:, 1] == 0) | (coords[:, 1] == L - 1))
    return Lattice(K, G, N, coords, edge)


def honeycomb_scalar(Lx, Ly, k0, kc, sigma2, periodic=False, haldane=True):
    """Scalar honeycomb flake (Lx x Ly unit cells, 2 sites/cell) with Haldane-type
    next-nearest-neighbour gyroscopic coupling G_ij = sigma2*nu_ij along NNN vectors
    b in {a1, a2-a1, -a2}; haldane=True uses nu = +1 on A and -1 on B (sublattice-odd,
    Haldane-type, gap-opening); haldane=False uses the same sign on both (sublattice-even,
    which in k-space is proportional to the identity and opens no gap)."""
    a1 = np.array([1.0, 0.0]); a2 = np.array([0.5, np.sqrt(3) / 2])
    dA = np.array([0.0, 0.0]); dB = (a1 + a2) / 3.0
    def sid(cx, cy, s):
        if periodic:
            cx %= Lx; cy %= Ly
        elif not (0 <= cx < Lx and 0 <= cy < Ly):
            return None
        return 2 * (cy * Lx + cx) + s
    N = 2 * Lx * Ly
    coords = np.zeros((N, 2))
    for cy in range(Ly):
        for cx in range(Lx):
            R = cx * a1 + cy * a2
            coords[sid(cx, cy, 0)] = R + dA
            coords[sid(cx, cy, 1)] = R + dB
    K = sp.lil_matrix((N, N)); G = sp.lil_matrix((N, N))
    deg = np.zeros(N)
    nn = [(0, 0), (-1, 0), (0, -1)]  # B neighbours of A(cx,cy): B(cx,cy), B(cx-1,cy), B(cx,cy-1)
    nnn = [(1, 0), (-1, 1), (0, -1)]  # 3 NNN vectors a1, a2-a1, -a2 (counter-clockwise set)
    for cy in range(Ly):
        for cx in range(Lx):
            i = sid(cx, cy, 0)
            for ox, oy in nn:
                j = sid(cx + ox, cy + oy, 1)
                if j is None:
                    continue
                K[i, j] -= kc; K[j, i] -= kc; deg[i] += 1; deg[j] += 1
            for s in (0, 1):
                i = sid(cx, cy, s)
                for ox, oy in nnn:
                    j = sid(cx + ox, cy + oy, s)
                    if j is None:
                        continue
                    sg = sigma2 if (s == 0 or not haldane) else -sigma2  # Haldane: sublattice-odd
                    G[i, j] += sg; G[j, i] -= sg
    K = K.tocsr() + sp.diags(k0 + kc * deg)
    edge = deg < 3
    return Lattice(K, G.tocsr(), N, coords, edge)
