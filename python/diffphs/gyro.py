# SPDX-License-Identifier: AGPL-3.0-only
"""Phase-1 reference model: 2-DOF gyroscopic honeycomb port-Hamiltonian lattice + ESAV-LF.

Continuous model (site i, displacement x_i in R^2, DOF ordering dof = 2*site + comp):
    M xdd = -K x - grad U(x) + G xd - D(xd) xd + B u,      y = B^T xd
    H = 1/2 xd^T M xd + 1/2 x^T K x + sum_dof U(x_dof),  U(s) = alpha/4 s^4 + beta*logcosh(s)
    K = k0 I + sum_bonds kc (e_i-e_j)(e_i-e_j)^T (x) n n^T   (central NN springs, PSD)
    G = blockdiag(g_i J2), J2 = [[0,1],[-1,0]]            (on-site gyroscopic, skew)
    D = blockdiag((-mu_i + eta_i |xd_i|^2) I2)             (Rayleigh / negative resistance)

ESAV-LF (see docs/theory_v1.md, Def. 3):
    M (q+ - 2q + q-)/h^2 = -K q - g.*(psi+ + psi-)/2 + (G - D^n) (q+ - q-)/(2h) + f
    psi+ = psi- + g.*(q+ - q-),   g = U'(q)/sqrt(2U(q)+c0),  D^n uses the lagged velocity.
Per site the linear system for w = q+ - q- is a 2x2 block -> exact explicit solve.

Arrays are column-batched: shape (ndof, B)."""
import numpy as np
import scipy.sparse as sp
from .model import logcosh

A1 = np.array([1.0, 0.0]); A2 = np.array([0.5, np.sqrt(3) / 2]); DB = (A1 + A2) / 3.0


def U(q, a, b):
    return 0.25 * a * q ** 4 + b * logcosh(q)


def dU(q, a, b):
    return a * q ** 3 + b * np.tanh(q)


class GyroHoneycomb:
    """Parallelogram flake of Lx x Ly unit cells (2 sites per cell), open boundaries."""

    def __init__(self, Lx, Ly, k0, kc, remove_sites=()):
        self.Lx, self.Ly = Lx, Ly
        sites = {}
        coords = []
        for cy in range(Ly):
            for cx in range(Lx):
                for s in (0, 1):
                    sites[(cx, cy, s)] = len(coords)
                    coords.append(cx * A1 + cy * A2 + (DB if s else 0 * DB))
        coords = np.array(coords)
        keep = np.ones(len(coords), bool); keep[list(remove_sites)] = False
        remap = -np.ones(len(coords), int); remap[keep] = np.arange(keep.sum())
        self.coords = coords[keep]
        self.N = int(keep.sum()); self.ndof = 2 * self.N
        bonds = []
        for (cx, cy, s), i in sites.items():
            if s:
                continue
            for ox, oy in ((0, 0), (-1, 0), (0, -1)):
                j = sites.get((cx + ox, cy + oy, 1))
                if j is None or not keep[i] or not keep[j]:
                    continue
                bonds.append((remap[i], remap[j]))
        self.bonds = np.array(bonds)
        rows, cols, vals = [], [], []
        deg = np.zeros(self.N, int)
        for i, j in self.bonds:
            r = self.coords[j] - self.coords[i]; n = r / np.linalg.norm(r); P = kc * np.outer(n, n)
            deg[i] += 1; deg[j] += 1
            for a in range(2):
                for b in range(2):
                    for (I, J, sgn) in ((i, i, 1), (j, j, 1), (i, j, -1), (j, i, -1)):
                        rows.append(2 * I + a); cols.append(2 * J + b); vals.append(sgn * P[a, b])
        self.K = (sp.coo_matrix((vals, (rows, cols)), shape=(self.ndof, self.ndof)).tocsr()
                  + k0 * sp.identity(self.ndof, format="csr"))
        self.deg = deg
        self.edge = deg < 3
        # site graph Laplacian (for the excitable control layer), hexagonal plaquettes
        A = sp.coo_matrix((np.ones(2 * len(self.bonds)),
                           (np.r_[self.bonds[:, 0], self.bonds[:, 1]], np.r_[self.bonds[:, 1], self.bonds[:, 0]])),
                          shape=(self.N, self.N)).tocsr()
        self.Adj = A
        self.Lap = A - sp.diags(np.asarray(A.sum(1)).ravel())
        hexes = []
        for cy in range(Ly - 1):
            for cx in range(Lx - 1):
                # hexagon around the centre of the rhombus (cx,cy)-(cx+1,cy+1)
                ring = [(cx, cy, 1), (cx + 1, cy, 0), (cx + 1, cy, 1), (cx + 1, cy + 1, 0), (cx, cy + 1, 1), (cx, cy + 1, 0)]
                ids = [sites[t] for t in ring]
                if all(keep[k] for k in ids):
                    hexes.append([remap[k] for k in ids])
        self.hexes = np.array(hexes, int).reshape(-1, 6)

    def omega_max(self, m_min):
        from scipy.sparse.linalg import eigsh
        return float(np.sqrt(eigsh(self.K, k=1, which="LA", return_eigenvectors=False)[0] / m_min))


class Params:
    """Per-site parameters, each (N, B).  alpha, beta >= 0 (convexity, needed by ENPU)."""
    keys = ("m", "alpha", "beta", "mu", "eta", "g")

    def __init__(self, N, B, **kw):
        for k in self.keys:
            setattr(self, k, np.broadcast_to(np.asarray(kw.get(k, 0.0 if k != "m" else 1.0), float), (N, B)).copy())

    def copy(self):
        P = Params.__new__(Params)
        for k in self.keys:
            setattr(P, k, getattr(self, k).copy())
        return P

    def dof(self, k):
        return np.repeat(getattr(self, k), 2, axis=0)


class GyroESAV:
    """Batched ESAV-LF integrator with ENPU and energy-gated gain."""

    def __init__(self, lat, h, P, q0, v0, c0=1.0, E_cap=np.inf, rsav=None, rsav_gamma=0.95):
        # rsav (Phase 5, E-RSAV relaxation of the SAV variable after each step; None = plain ESAV-LF):
        #   "clamp" : psi <- copysign(min(|psi~|, sqrt(2U(q^{n+1})+c0)), psi~)  (only removes energy; = C++ ersav=true)
        #   "budget": like clamp, and psi may also be raised toward sqrt(2U+c0) by at most rsav_gamma x the energy that
        #             the damping removed from that DOF in this step (classic relaxed-SAV dissipation budget; passive).
        self.lat, self.h, self.P, self.c0, self.E_cap = lat, h, P, c0, E_cap
        self.rsav, self.rsav_gamma = rsav, rsav_gamma
        self.q = q0.copy(); self.qm = q0 - h * v0      # first-order start (energy is defined by (q, qm) pair)
        a, b = P.dof("alpha"), P.dof("beta")
        self.psi = np.sqrt(2 * U(self.q, a, b) + c0); self.psim = np.sqrt(2 * U(self.qm, a, b) + c0)
        self.last_power = None

    # ---- discrete energy  E^{n-1/2} for the pair (q^n, q^{n-1}) ----
    def energy(self, q=None, qm=None, psi=None, psim=None, P=None):
        q = self.q if q is None else q; qm = self.qm if qm is None else qm
        psi = self.psi if psi is None else psi; psim = self.psim if psim is None else psim
        P = self.P if P is None else P
        h = self.h; v = (q - qm) / h; m = P.dof("m")
        return (0.5 * np.sum(m * v * v, 0) + 0.5 * np.sum(q * (self.lat.K @ qm), 0)
                + 0.25 * np.sum(psi ** 2 + psim ** 2, 0) - 0.5 * self.c0 * self.lat.ndof)

    def step(self, f=0.0, return_vbar=False):
        lat, h, P = self.lat, self.h, self.P
        q, qm = self.q, self.qm
        a, b = P.dof("alpha"), P.dof("beta"); m = P.dof("m")
        gg = dU(q, a, b) / np.sqrt(2 * U(q, a, b) + self.c0)
        vl = (q - qm) / h
        v2 = vl[0::2] ** 2 + vl[1::2] ** 2
        mu = P.mu
        if np.isfinite(self.E_cap):
            gate = self.energy() < self.E_cap
            mu = np.where(gate[None, :], mu, np.minimum(mu, 0.0))
        d = np.repeat(-mu + P.eta * v2, 2, axis=0)
        diag = m / h ** 2 + 0.5 * gg * gg + d / (2 * h)
        rhs = 2 * m * (q - qm) / h ** 2 - lat.K @ q - gg * self.psim + f
        c = P.g / (2 * h)
        ax, ay = diag[0::2], diag[1::2]; rx, ry = rhs[0::2], rhs[1::2]
        det = ax * ay + c * c
        w = np.empty_like(q)
        w[0::2] = (ay * rx + c * ry) / det
        w[1::2] = (ax * ry - c * rx) / det
        qp = qm + w
        psip = self.psim + gg * w
        vbar = w / (2 * h)
        if self.rsav:
            cap = np.sqrt(2 * U(qp, a, b) + self.c0)
            if self.rsav == "clamp":
                psip = np.copysign(np.minimum(np.abs(psip), cap), psip)
            elif self.rsav == "budget":
                diss = np.maximum(h * d * vbar * vbar, 0.0)     # energy removed by the damping of this DOF in this step
                up = np.sqrt(psip * psip + 4.0 * self.rsav_gamma * diss)   # 0.25*(psi^2 - psi~^2) <= gamma * diss
                psip = np.copysign(np.where(np.abs(psip) > cap, cap, np.minimum(up, cap)), psip)
            else:
                raise ValueError(self.rsav)
        self.last_power = (-np.sum(vbar * d * vbar, 0), np.sum(vbar * f, 0) if np.ndim(f) else 0.0 * vbar[0])
        self.qm, self.q = q, qp
        self.psim, self.psi = self.psi, psip
        return vbar if return_vbar else None

    def enpu(self, Pn):
        """Energy-non-increasing parameter update (Theorem 3).  Returns scale factor s."""
        h = self.h
        E_old = self.energy()
        qb = 0.5 * (self.q + self.qm)
        v = (self.q - self.qm) / h * np.sqrt(self.P.dof("m") / Pn.dof("m"))
        a, b = Pn.dof("alpha"), Pn.dof("beta")

        def cand(s):
            q = s * (qb + 0.5 * h * v); qm = s * (qb - 0.5 * h * v)
            return q, qm, np.sqrt(2 * U(q, a, b) + self.c0), np.sqrt(2 * U(qm, a, b) + self.c0)

        c1 = cand(1.0)
        E1 = self.energy(*c1, P=Pn)
        target = np.maximum(E_old, 0.0)
        with np.errstate(all="ignore"):
            s = np.where(E1 > target, target / E1, 1.0)
        s = np.clip(np.where(np.isfinite(s), s, 0.0), 0.0, 1.0)
        self.q, self.qm, self.psi, self.psim = cand(s[None, :])
        self.P = Pn
        return s


def audio_params(f_unit=1500.0, k0r=0.1, Omr=0.6, fs=48000.0):
    """Realistic audio-rate parameters: frequency unit w_u = 2 pi f_unit = sqrt(kc/m)."""
    wu = 2 * np.pi * f_unit
    return dict(fs=fs, h=1.0 / fs, m=1.0, kc=wu ** 2, k0=k0r * wu ** 2, g=Omr * wu, wu=wu)
