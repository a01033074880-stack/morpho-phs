# SPDX-License-Identifier: AGPL-3.0-only
"""Band structure / Chern-number tools for linear gyroscopic lattices  M qdd = -K q + G qd.

First-order form x=(q,p): xdot = J Hm x, J=[[0,I],[-I,G]], Hm=blkdiag(K, M^-1).
With y = Hm^{1/2} x, ydot = S y, S = Hm^{1/2} J Hm^{1/2} anti-Hermitian (Bloch), and
L = i S is Hermitian with eigenvalues +-omega.  Chern numbers of positive-frequency bands
are computed with the Fukui-Hatsugai-Suzuki lattice method on eigenvectors of L(k)."""
import numpy as np
from scipy.linalg import eigh


class TBModel:
    """Tight-binding style description: norb DOFs per cell, lattice vectors a1,a2.
    terms: list of (o1, o2, (n1, n2), kval, gval) meaning matrix element between DOF o1 in
    cell R and DOF o2 in cell R + n1 a1 + n2 a2 (both directions must be listed)."""

    def __init__(self, norb, a1, a2, mass, terms):
        self.norb = norb; self.a1 = np.asarray(a1, float); self.a2 = np.asarray(a2, float)
        self.mass = np.broadcast_to(np.asarray(mass, float), (norb,)).copy()
        self.terms = terms
        A = np.array([self.a1, self.a2])
        self.B = 2 * np.pi * np.linalg.inv(A).T  # rows b1, b2

    def bloch(self, k):
        Kk = np.zeros((self.norb, self.norb), complex); Gk = np.zeros_like(Kk)
        for o1, o2, (n1, n2), kv, gv in self.terms:
            ph = np.exp(1j * np.dot(k, n1 * self.a1 + n2 * self.a2))
            Kk[o1, o2] += kv * ph; Gk[o1, o2] += gv * ph
        return Kk, Gk


def first_order_L(Kk, Gk, mass):
    n = Kk.shape[0]
    w, V = eigh(0.5 * (Kk + Kk.conj().T))
    if w.min() <= 0:
        raise ValueError("K(k) not positive definite; add grounding k0")
    Ks = (V * np.sqrt(w)) @ V.conj().T
    Hs = np.zeros((2 * n, 2 * n), complex)
    Hs[:n, :n] = Ks; Hs[n:, n:] = np.diag(1 / np.sqrt(mass))
    J = np.zeros((2 * n, 2 * n), complex)
    J[:n, n:] = np.eye(n); J[n:, :n] = -np.eye(n); J[n:, n:] = Gk
    S = Hs @ J @ Hs
    L = 1j * S
    return 0.5 * (L + L.conj().T)


def bands(model, kpts):
    out = []
    for k in kpts:
        Kk, Gk = model.bloch(k)
        w = np.linalg.eigvalsh(first_order_L(Kk, Gk, model.mass))
        out.append(np.sort(w[w > 0]))
    return np.array(out)


def chern_fhs(model, band_sets, Nk=48):
    """band_sets: list of lists of positive-band indices (0 = lowest positive band)."""
    n = model.norb
    vecs = np.empty((Nk, Nk), object)
    for i in range(Nk):
        for j in range(Nk):
            k = (i / Nk) * model.B[0] + (j / Nk) * model.B[1]
            Kk, Gk = model.bloch(k)
            w, V = eigh(first_order_L(Kk, Gk, model.mass))
            order = np.argsort(w)
            V = V[:, order]
            vecs[i, j] = V[:, n:]  # positive-frequency eigenvectors, ascending
    res = []
    for bs in band_sets:
        def link(a, b):
            d = np.linalg.det(a[:, bs].conj().T @ b[:, bs])
            return d / abs(d)
        F = 0.0
        for i in range(Nk):
            for j in range(Nk):
                ip, jp = (i + 1) % Nk, (j + 1) % Nk
                U1 = link(vecs[i, j], vecs[ip, j]); U2 = link(vecs[ip, j], vecs[ip, jp])
                U3 = link(vecs[ip, jp], vecs[i, jp]); U4 = link(vecs[i, jp], vecs[i, j])
                F += np.angle(U1 * U2 * U3 * U4)
        res.append(F / (2 * np.pi))
    return res


def ribbon_bands(model, W, kxs):
    """Ribbon periodic along a1 (Bloch phase k), W cells along a2 with open boundaries.
    Returns freqs (len(kxs), nmodes) and edge weight of each mode (energy fraction in the
    outer 2 cells on each side)."""
    n = model.norb; Nd = n * W
    allw, alle = [], []
    for kx in kxs:
        K = np.zeros((Nd, Nd), complex); G = np.zeros_like(K)
        for c in range(W):
            for o1, o2, (n1, n2), kv, gv in model.terms:
                c2 = c + n2
                if not (0 <= c2 < W):
                    continue
                ph = np.exp(1j * kx * n1)
                K[c * n + o1, c2 * n + o2] += kv * ph
                G[c * n + o1, c2 * n + o2] += gv * ph
        L = first_order_L(K, G, np.tile(model.mass, W))
        w, V = eigh(L)
        sel = w > 0
        w = w[sel]; V = V[:, sel]
        dens = np.abs(V[:Nd]) ** 2 + np.abs(V[Nd:]) ** 2
        cellw = dens.reshape(W, n, -1).sum(1)
        edge = cellw[:2].sum(0) + cellw[-2:].sum(0)
        top = cellw[-2:].sum(0) - cellw[:2].sum(0)
        allw.append(w); alle.append(np.stack([edge, top]))
    return np.array(allw), np.array(alle)


# ---------- concrete models ----------
def square_scalar_model(k0, kc, sigma, Phi, m=1.0):
    s = sigma * np.sin(Phi)
    terms = [(0, 0, (0, 0), k0 + 4 * kc, 0.0)]
    for d in ((1, 0), (0, 1)):
        terms.append((0, 0, d, -kc, +s))
        terms.append((0, 0, (-d[0], -d[1]), -kc, -s))
    return TBModel(1, (1, 0), (0, 1), m, terms)


def honeycomb_scalar_model(k0, kc, sigma2, haldane=True, m=1.0):
    terms = [(0, 0, (0, 0), k0 + 3 * kc, 0.0), (1, 1, (0, 0), k0 + 3 * kc, 0.0)]
    for d in ((0, 0), (-1, 0), (0, -1)):  # A(R) -- B(R+d)
        terms.append((0, 1, d, -kc, 0.0)); terms.append((1, 0, (-d[0], -d[1]), -kc, 0.0))
    for d in ((1, 0), (-1, 1), (0, -1)):
        for s in (0, 1):
            g = sigma2 if (s == 0 or not haldane) else -sigma2
            terms.append((s, s, d, 0.0, +g)); terms.append((s, s, (-d[0], -d[1]), 0.0, -g))
    return TBModel(2, (1, 0), (0.5, np.sqrt(3) / 2), m, terms)


def honeycomb_gyro2dof_model(k0, kc, Omega, m=1.0):
    """2-DOF (x,y) per site, central NN springs, on-site gyroscopic coupling
    G_site = m*Omega*[[0,1],[-1,0]] (Nash 2015 / Wang-Lu-Bertoldi 2015 type)."""
    a1 = np.array([1.0, 0]); a2 = np.array([0.5, np.sqrt(3) / 2]); dB = (a1 + a2) / 3
    terms = []
    onsite = np.zeros((4, 4))
    nnl = ((0, 0), (-1, 0), (0, -1))
    for d in nnl:
        r = dB + d[0] * a1 + d[1] * a2  # vector from A(R) to B(R+d)
        nvec = r / np.linalg.norm(r); P = np.outer(nvec, nvec)
        onsite[:2, :2] += kc * P; onsite[2:, 2:] += kc * P
        for a in range(2):
            for b in range(2):
                terms.append((a, 2 + b, d, -kc * P[a, b], 0.0))
                terms.append((2 + b, a, (-d[0], -d[1]), -kc * P[a, b], 0.0))
    onsite += k0 * np.eye(4)
    Gs = m * Omega * np.array([[0, 1], [-1, 0]])
    for a in range(4):
        for b in range(4):
            g = 0.0
            if a // 2 == b // 2:
                g = Gs[a % 2, b % 2]
            if onsite[a, b] != 0 or g != 0:
                terms.append((a, b, (0, 0), onsite[a, b], g))
    return TBModel(4, a1, a2, m, terms)
