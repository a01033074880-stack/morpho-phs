# SPDX-License-Identifier: AGPL-3.0-only
"""Phase-2 exp1: six time-stepping schemes for the SAME 2-DOF gyroscopic honeycomb PHS
(diffphs.gyro), batched over columns (ndof, B).  Continuous model, site i, x_i in R^2:
    m xdd = -K x - U'(x) + G xd - d(|xd_i|^2) xd + f,   d = -mu + eta |xd_i|^2,   (G v)_i = g_i (v_y, -v_x)
    H = 1/2 m |v|^2 + 1/2 x^T K x + sum U,  U(s) = alpha/4 s^4 + beta logcosh s.
Schemes (all expose step(f) -> output velocity estimate (ndof,B), H() continuous energy, inv() own invariant):
  ESAV     ESAV-LF (diffphs.gyro.GyroESAV), optional energy gate E_cap
  RK4      classical RK4 on (q, v)
  Verlet   Stormer-Verlet KDK, velocity-dependent forces (G, D) lagged (explicit)
  TanhFB   Verlet + heuristic tanh-limited negative-resistance feedback (no passivity guarantee):
           -d(v) v replaced by  mu v_s tanh(|v|/v_s) v/|v| - eta |v|^2 v, v_s = sqrt(|mu|/eta)
  IMDG     implicit midpoint + Gonzalez discrete gradient for U + Newton (exact discrete power balance)
  SSDG     proposal's literal splitting  Bern(h/2) o Cay(h/2) o Kick(h) o Cay(h/2) o Bern(h/2)
           (linear part A = [[0, M^-1],[-K, G M^-1]] by Cayley/sparse LU; Bern = exact site-vector
            Bernoulli flow of the Rayleigh port; Kick = exact nonlinear flow with q frozen)."""
import numpy as np
import scipy.sparse as sp
import scipy.sparse.linalg as spla
from .gyro import GyroESAV, U, dU
from .model import logcosh


def Gv(P, v):
    out = np.empty_like(v)
    out[0::2] = P.g * v[1::2]
    out[1::2] = -P.g * v[0::2]
    return out


def site_v2(v):
    return v[0::2] ** 2 + v[1::2] ** 2


def H_cont(lat, P, q, v):
    m = P.dof("m"); a, b = P.dof("alpha"), P.dof("beta")
    return 0.5 * np.sum(m * v * v, 0) + 0.5 * np.sum(q * (lat.K @ q), 0) + np.sum(U(q, a, b), 0)


class Base:
    name = "base"
    def __init__(self, lat, h, P, q0, v0):
        self.lat, self.h, self.P = lat, h, P
        self.q, self.v = q0.copy(), v0.copy()
    def set_params(self, Pn): self.P = Pn
    def H(self): return H_cont(self.lat, self.P, self.q, self.v)
    def inv(self): return self.H()
    def damp(self, v):
        return np.repeat(-self.P.mu + self.P.eta * site_v2(v), 2, axis=0) * v
    def acc(self, q, v, f):
        P = self.P
        return (-(self.lat.K @ q) - dU(q, P.dof("alpha"), P.dof("beta")) + Gv(P, v) - self.damp(v) + f) / P.dof("m")


class RK4(Base):
    name = "RK4"
    def step(self, f):
        h, q, v = self.h, self.q, self.v
        a1 = self.acc(q, v, f); k1 = v
        a2 = self.acc(q + h / 2 * k1, v + h / 2 * a1, f); k2 = v + h / 2 * a1
        a3 = self.acc(q + h / 2 * k2, v + h / 2 * a2, f); k3 = v + h / 2 * a2
        a4 = self.acc(q + h * k3, v + h * a3, f); k4 = v + h * a3
        self.q = q + h / 6 * (k1 + 2 * k2 + 2 * k3 + k4)
        self.v = v + h / 6 * (a1 + 2 * a2 + 2 * a3 + a4)
        return self.v


class Verlet(Base):
    name = "Stormer-Verlet"
    def step(self, f):
        h = self.h
        vh = self.v + 0.5 * h * self.acc(self.q, self.v, f)
        self.q = self.q + h * vh
        self.v = vh + 0.5 * h * self.acc(self.q, vh, f)
        return self.v


class TanhFB(Verlet):
    name = "tanh-limited feedback (heuristic)"
    def damp(self, v):
        P = self.P; v2 = site_v2(v); s = np.sqrt(v2) + 1e-300
        vs = np.sqrt(np.abs(P.mu) / np.maximum(P.eta, 1e-300)) + 1e-300
        lim = np.where(P.mu > 0, P.mu * vs * np.tanh(s / vs) / s, P.mu)   # saturating gain only for the active part
        return np.repeat(-lim + P.eta * v2, 2, axis=0) * v


class IMDG(Base):
    name = "implicit dG + Newton"
    def __init__(self, lat, h, P, q0, v0, tol=1e-12, maxit=30):
        super().__init__(lat, h, P, q0, v0)
        self.tol, self.maxit, self.iters, self.fail = tol, maxit, [], np.zeros(q0.shape[1], bool)

    @staticmethod
    def dg(q0, q1, a, b):
        d = q1 - q0
        small = np.abs(d) < 1e-6 * (1 + np.abs(q0))
        quart = 0.25 * a * (q1 ** 3 + q1 ** 2 * q0 + q1 * q0 ** 2 + q0 ** 3)
        dquart = 0.25 * a * (3 * q1 ** 2 + 2 * q1 * q0 + q0 ** 2)
        with np.errstate(all="ignore"):
            dd = np.where(small, 1.0, d)
            lc = b * (logcosh(q1) - logcosh(q0)) / dd
            dlc = (b * np.tanh(q1) * dd - b * (logcosh(q1) - logcosh(q0))) / (dd * dd)
        qm = 0.5 * (q0 + q1)
        lc = np.where(small, b * np.tanh(qm), lc)
        dlc = np.where(small, 0.5 * b / np.cosh(np.clip(qm, -350, 350)) ** 2, dlc)
        return quart + lc, dquart + dlc

    def step(self, f):
        lat, h, P = self.lat, self.h, self.P
        q0, v0 = self.q, self.v
        nd, B = q0.shape; N = nd // 2
        m = P.dof("m"); a, b = P.dof("alpha"), P.dof("beta")
        vb = v0 + 0.5 * h * self.acc(q0, v0, f)       # explicit predictor
        # constant part of the Jacobian: kron(I_B, h^2/2 K)
        Kb = sp.kron(sp.identity(B, format="csr"), (0.5 * h * h) * lat.K, format="csr")
        # block-diag site index pattern (row-major over batch then dof)
        base = (np.arange(B)[:, None] * nd + 2 * np.arange(N)[None, :]).ravel()   # (B*N,)
        rows = np.concatenate([base, base, base + 1, base + 1]); cols = np.concatenate([base, base + 1, base, base + 1])
        conv = np.zeros(B, bool); it = 0
        for it in range(1, self.maxit + 1):
            q1 = q0 + h * vb
            dgv, ddg = self.dg(q0, q1, a, b)
            v2 = site_v2(vb)
            dmp = np.repeat(-P.mu + P.eta * v2, 2, axis=0) * vb
            F = -(lat.K @ (0.5 * (q0 + q1))) - dgv + Gv(P, vb) - dmp + f
            r = 2 * m * (vb - v0) - h * F
            scale = np.abs(2 * m * vb).max(0) + np.abs(2 * m * v0).max(0) + np.abs(h * f).max(0) + 1e-300
            err = np.abs(r).max(0) / scale
            conv = err < self.tol
            if conv.all() or not np.isfinite(r).all():
                break
            # site 2x2 blocks: 2m I + h^2 ddg - h G + h [(-mu+eta|v|^2) I + 2 eta v v^T]
            vx, vy = vb[0::2], vb[1::2]; dd = -P.mu + P.eta * v2
            mx = m[0::2]
            Jxx = 2 * mx + h * h * ddg[0::2] + h * (dd + 2 * P.eta * vx * vx)
            Jyy = 2 * mx + h * h * ddg[1::2] + h * (dd + 2 * P.eta * vy * vy)
            Jxy = -h * P.g + h * 2 * P.eta * vx * vy      # -h * (G)_xy, (G)_xy = g
            Jyx = h * P.g + h * 2 * P.eta * vx * vy       # -h * (G)_yx, (G)_yx = -g
            vals = np.concatenate([Jxx.T.ravel(), Jxy.T.ravel(), Jyx.T.ravel(), Jyy.T.ravel()])
            J = Kb + sp.coo_matrix((vals, (rows, cols)), shape=(nd * B, nd * B)).tocsr()
            dv = spla.spsolve(J.tocsc(), r.T.ravel()).reshape(B, nd).T
            vb = vb - dv
        self.iters.append(it); self.fail |= ~conv
        self.q = q0 + h * vb
        self.v = 2 * vb - v0
        return vb


class SSDG(Base):
    name = "literal SSDG (proposal)"
    def __init__(self, lat, h, P, q0, v0):
        super().__init__(lat, h, P, q0, v0)
        self._fac()
    def _fac(self):
        lat, P, h = self.lat, self.P, self.h
        nd, B = self.q.shape
        self.lus = []
        for c in range(B):
            Pc = type("Pc", (), {})()
            m = np.repeat(P.m[:, c], 2); g = P.g[:, c]
            # G M^-1 as sparse (site 2x2 [[0, g/m],[-g/m, 0]])
            N = nd // 2; i = np.arange(N)
            G = sp.coo_matrix((np.r_[g / P.m[:, c], -g / P.m[:, c]], (np.r_[2 * i, 2 * i + 1], np.r_[2 * i + 1, 2 * i])), shape=(nd, nd))
            Mi = sp.diags(1.0 / m)
            A = sp.bmat([[None, Mi], [-lat.K, G]], format="csc")
            I = sp.identity(2 * nd, format="csc")
            self.lus.append((spla.splu((I - (h / 4) * A).tocsc()), (I + (h / 4) * A).tocsr()))
    def set_params(self, Pn):
        self.P = Pn; self._fac()
    def cay(self, p):
        nd = self.q.shape[0]
        x = np.vstack([self.q, p])
        for c, (lu, Bp) in enumerate(self.lus):
            x[:, c] = lu.solve(Bp @ x[:, c])
        self.q = x[:nd]; return x[nd:]
    def bern(self, p, dt):
        P = self.P; m = P.m
        r2 = site_v2(p)                           # |p_site|^2
        a = P.mu / m; b = P.eta / m ** 3          # d r/dt = a r - b r^3
        z = 2 * a * dt
        with np.errstate(all="ignore"):
            phi = np.where(np.abs(z) < 1e-8, 1 - z / 2, np.expm1(-z) / (-z))
            w = np.exp(-z) / r2 + 2 * b * dt * phi      # 1/r^2 flow (affine)
            scale = np.where(r2 > 0, np.sqrt(1.0 / (w * r2)), 1.0)
        return p * np.repeat(scale, 2, axis=0)
    def step(self, f):
        h, P = self.h, self.P
        m = P.dof("m")
        p = m * self.v
        p = self.bern(p, h / 2); p = self.cay(p)
        p = p - h * (dU(self.q, P.dof("alpha"), P.dof("beta")) - f)
        p = self.cay(p); p = self.bern(p, h / 2)
        self.v = p / m
        return self.v


class ESAV(Base):
    name = "ESAV-LF"
    def __init__(self, lat, h, P, q0, v0, E_cap=np.inf, enpu=True, rsav=None):
        self.lat, self.h, self.P = lat, h, P
        self.e = GyroESAV(lat, h, P, q0, v0, E_cap=E_cap, rsav=rsav)
        self.use_enpu = enpu
        self.v = v0.copy()
    @property
    def q(self): return self.e.q
    def set_params(self, Pn):
        if self.use_enpu: self.e.enpu(Pn)
        else: self.e.P = Pn
        self.P = Pn
    def step(self, f):
        vb = self.e.step(f, return_vbar=True)
        self.v = (self.e.q - self.e.qm) / self.h
        return vb
    def inv(self): return self.e.energy()
    def H(self): return H_cont(self.lat, self.P, self.e.q, (self.e.q - self.e.qm) / self.h)


SCHEMES = {"ESAV": ESAV, "RK4": RK4, "Verlet": Verlet, "TanhFB": TanhFB, "IMDG": IMDG, "SSDG": SSDG}
