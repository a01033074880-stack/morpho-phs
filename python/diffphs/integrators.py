# SPDX-License-Identifier: AGPL-3.0-only
"""Reference integrators for the proposal's lattice PHS (column-batched, states (N,B)).

  M qdd = -K q - U'(q) + G qd - d(qd) qd + f,  d(v) = -mu + eta v^2.

Schemes
  rk4        classical RK4 on (q,p)
  verlet     explicit kick-drift-kick (velocity terms lagged)               -- baseline
  literal    proposal SSDG: Bern(h/2) o Cayley(h/2) o Kick(h) o Cayley(h/2) o Bern(h/2)
             (Psi_nl with q frozen == exact kick p -= h U'(q);  Cayley of A = J*Hlin,
             solved with a sparse LU -- the dense Q would be O(N^2) per sample)
  imdg       implicit midpoint + Gonzalez discrete gradient + Newton (exact power balance)
  esav       corrected: explicit SAV leapfrog (Bilbao-Ducceschi-Zama 2023 style) with a
             per-DOF scalar auxiliary variable; the only coupling solve is the skew G term
             (handled by a fixed-point Jacobi iteration, contraction h|G|/(2m) << 1, or
             exactly per node if G is on-site 2x2).  Exact discrete power balance.
"""
import numpy as np
import scipy.sparse as sp
import scipy.sparse.linalg as spla
from .model import U, dU, d2U
from .bernoulli import bernoulli_stable


class Params:
    def __init__(self, N, B, m=1.0, alpha=0.0, beta=0.0, mu=0.0, eta=0.0):
        f = lambda x: np.broadcast_to(np.asarray(x, float), (N, B)).copy()
        self.m, self.alpha, self.beta, self.mu, self.eta = f(m), f(alpha), f(beta), f(mu), f(eta)

    def copy(self):
        P = Params.__new__(Params)
        for k in ("m", "alpha", "beta", "mu", "eta"):
            setattr(P, k, getattr(self, k).copy())
        return P


def force(lat, q, p, P, f):
    v = p / P.m
    return -(lat.K @ q) - dU(q, P.alpha, P.beta) + lat.G @ v - (-P.mu + P.eta * v * v) * v + f


def hamiltonian(lat, q, p, P):
    return lat.energy(q, p, P.m, P.alpha, P.beta)


class RK4:
    name = "RK4"
    def __init__(self, lat, h, P, q, p):
        self.lat, self.h, self.P, self.q, self.p = lat, h, P, q.copy(), p.copy()
    def step(self, f):
        lat, h, P = self.lat, self.h, self.P
        def F(q, p): return p / P.m, force(lat, q, p, P, f)
        a1, b1 = F(self.q, self.p)
        a2, b2 = F(self.q + h / 2 * a1, self.p + h / 2 * b1)
        a3, b3 = F(self.q + h / 2 * a2, self.p + h / 2 * b2)
        a4, b4 = F(self.q + h * a3, self.p + h * b3)
        self.q = self.q + h / 6 * (a1 + 2 * a2 + 2 * a3 + a4)
        self.p = self.p + h / 6 * (b1 + 2 * b2 + 2 * b3 + b4)
    def set_params(self, Pn): self.P = Pn
    def state(self): return self.q, self.p
    def energy(self): return hamiltonian(self.lat, self.q, self.p, self.P)


class Verlet(RK4):
    name = "Störmer-Verlet"
    def step(self, f):
        lat, h, P = self.lat, self.h, self.P
        ph = self.p + h / 2 * force(lat, self.q, self.p, P, f)
        self.q = self.q + h * ph / P.m
        self.p = ph + h / 2 * force(lat, self.q, ph, P, f)


class Literal(RK4):
    """Proposal's SSDG as literally specified (with the safe Bernoulli form so that the
    mu=0 NaN of the printed formula does not dominate the statistics; see literal_nan flag)."""
    name = "SSDG (proposal, literal)"
    def __init__(self, lat, h, P, q, p, literal_bernoulli=False):
        super().__init__(lat, h, P, q, p)
        self.literal_bernoulli = literal_bernoulli
        self.refactor()
    def refactor(self):
        lat, h = self.lat, self.h
        m = self.P.m[:, 0]
        if not np.allclose(self.P.m, m[:, None]):
            raise ValueError("literal scheme here requires batch-uniform mass")
        N = lat.N
        Minv = sp.diags(1.0 / m)
        A = sp.bmat([[None, Minv], [-lat.K, lat.G @ Minv]], format="csc")
        I = sp.identity(2 * N, format="csc")
        self.lu = spla.splu((I - (h / 4) * A).tocsc())
        self.Bp = (I + (h / 4) * A).tocsr()
        self.nfactor = getattr(self, "nfactor", 0) + 1
    def set_params(self, Pn):
        mchanged = not np.array_equal(Pn.m, self.P.m)
        self.P = Pn
        if mchanged: self.refactor()
    def bern(self, dt):
        P = self.P
        if self.literal_bernoulli:
            from .bernoulli import bernoulli_literal
            self.p = bernoulli_literal(self.p, P.m, P.mu, P.eta, dt)
        else:
            self.p = bernoulli_stable(self.p, P.m, P.mu, P.eta, dt)
    def cayley(self):
        x = np.vstack([self.q, self.p])
        x = self.lu.solve(self.Bp @ x)
        N = self.lat.N
        self.q, self.p = x[:N], x[N:]
    def step(self, f):
        h = self.h
        self.bern(h / 2); self.cayley()
        self.p = self.p - h * (dU(self.q, self.P.alpha, self.P.beta) - f)
        self.cayley(); self.bern(h / 2)


class IMDG(RK4):
    """Implicit midpoint + Gonzalez dG; Newton on the block-diagonal batched system."""
    name = "implicit dG + Newton"
    def __init__(self, lat, h, P, q, p, tol=1e-11, maxit=40):
        super().__init__(lat, h, P, q, p)
        self.tol, self.maxit = tol, maxit
        self.iters = []; self.fail = np.zeros(q.shape[1], bool)
    @staticmethod
    def dg(q0, q1, a, b):
        d = q1 - q0
        small = np.abs(d) < 1e-7 * (1 + np.abs(q0))
        quart = 0.25 * a * (q1 ** 3 + q1 ** 2 * q0 + q1 * q0 ** 2 + q0 ** 3)
        dquart = 0.25 * a * (3 * q1 ** 2 + 2 * q1 * q0 + q0 ** 2)
        from .model import logcosh
        with np.errstate(all="ignore"):
            lc = b * (logcosh(q1) - logcosh(q0)) / np.where(small, 1, d)
            dlc = (b * np.tanh(q1) * d - b * (logcosh(q1) - logcosh(q0))) / np.where(small, 1, d * d)
        qm = 0.5 * (q0 + q1)
        lc = np.where(small, b * np.tanh(qm), lc)
        dlc = np.where(small, 0.5 * b / np.cosh(np.clip(qm, -350, 350)) ** 2, dlc)
        return quart + lc, dquart + dlc
    def step(self, f):
        lat, h, P = self.lat, self.h, self.P
        q0, p0 = self.q, self.p
        N, B = q0.shape
        q1 = q0 + h * p0 / P.m
        KG = (h / 2) * lat.K - lat.G
        bigKG = sp.kron(sp.identity(B, format="csr"), KG, format="csr")
        conv = np.zeros(B, bool); it = 0
        for it in range(1, self.maxit + 1):
            vb = (q1 - q0) / h
            dg, ddg = self.dg(q0, q1, P.alpha, P.beta)
            r = (2 * P.m * vb - 2 * p0
                 - h * (-(lat.K @ (0.5 * (q0 + q1))) - dg + lat.G @ vb - (-P.mu + P.eta * vb * vb) * vb + f))
            # scale-aware convergence test
            scale = np.abs(2 * P.m * vb).max(0) + np.abs(2 * p0).max(0) + 1e-300
            err = np.abs(r).max(0) / scale
            conv = err < self.tol
            if conv.all() or not np.isfinite(r).all():
                break
            diag = 2 * P.m / h + h * ddg + (-P.mu + 3 * P.eta * vb * vb)
            Jm = bigKG + sp.diags(diag.T.ravel())
            dq = spla.spsolve(Jm.tocsc(), r.T.ravel()).reshape(B, N).T
            q1 = q1 - dq
        self.iters.append(it)
        self.fail |= ~conv
        vb = (q1 - q0) / h
        self.q = q1; self.p = 2 * P.m * vb - p0


class ESAV(RK4):
    """Corrected explicit scheme (two-step leapfrog + per-DOF SAV + centred G/R terms)."""
    name = "ESAV-LF (corrected)"
    def __init__(self, lat, h, P, q, p, c0=1.0, jacobi_tol=1e-13, jacobi_max=60, enpu=True):
        super().__init__(lat, h, P, q, p)
        self.c0, self.jtol, self.jmax, self.enpu = c0, jacobi_tol, jacobi_max, enpu
        # second-order start: q_{-1} = q0 - h v0 + h^2/2 a0
        # conservative part only: a huge Rayleigh force at an extreme initial velocity would
        # otherwise inject an O(h^2 eta v^3) start-up error
        a0 = (-(lat.K @ q) - dU(q, P.alpha, P.beta) + lat.G @ (p / P.m)) / P.m
        self.qm = q - h * p / P.m + 0.5 * h * h * a0
        self.psi = np.sqrt(2 * U(q, P.alpha, P.beta) + c0)
        self.psim = np.sqrt(2 * U(self.qm, P.alpha, P.beta) + c0)
        self.jac_sweeps = []
        self.Gnz = lat.G.nnz > 0
    def step(self, f):
        lat, h, P = self.lat, self.h, self.P
        q, qm = self.q, self.qm
        g = dU(q, P.alpha, P.beta) / np.sqrt(2 * U(q, P.alpha, P.beta) + self.c0)
        vlag = (q - qm) / h
        d = -P.mu + P.eta * vlag * vlag
        diag = P.m / h ** 2 + 0.5 * g * g + d / (2 * h)
        rhs = 2 * P.m * (q - qm) / h ** 2 - lat.K @ q - g * self.psim + f
        w = rhs / diag
        if self.Gnz:
            for k in range(self.jmax):
                wn = (rhs + (lat.G @ w) / (2 * h)) / diag
                dif = np.abs(wn - w).max() / (np.abs(wn).max() + 1e-300)
                w = wn
                if dif < self.jtol: break
            self.jac_sweeps.append(k + 1)
        qp = qm + w
        psip = self.psim + g * w
        self.qm, self.q = q, qp
        self.psim, self.psi = self.psi, psip
        self.p = P.m * (qp - qm) / (2 * h)  # centred momentum estimate at old time level
    def energy(self):
        """Scheme invariant E^{n+1/2} (minus the constant N*c0/2)."""
        lat, h, P = self.lat, self.h, self.P
        v = (self.q - self.qm) / h
        return (np.sum(0.5 * P.m * v * v, 0) + 0.5 * np.sum(self.q * (lat.K @ self.qm), 0)
                + 0.25 * np.sum(self.psi ** 2 + self.psim ** 2, 0) - 0.5 * self.c0 * lat.N)
    def set_params(self, Pn):
        """Energy-non-increasing parameter update (ENPU).
        Write E = 1/2 v^T (M - h^2 K/4) v + 1/2 qb^T K qb + psi-part, qb = (q+qm)/2.
        On a mass change, target v_t = v*sqrt(m/m') (keeps m v^2 per node), keep qb fixed
        and scale v_t by c = min(1, sqrt((E0 - b)/a)) so that E_new <= E0 always (a >= 0
        under the linear CFL condition).  alpha/beta changes leave E unchanged because
        psi is an independent state variable (SAV)."""
        if self.enpu and not np.array_equal(Pn.m, self.P.m):
            E0 = self.energy(); h = self.h; lat = self.lat
            v = (self.q - self.qm) / h * np.sqrt(self.P.m / Pn.m)
            qb = 0.5 * (self.q + self.qm)
            a = np.sum(0.5 * Pn.m * v * v, 0) - (h * h / 8) * np.sum(v * (lat.K @ v), 0)
            b = 0.5 * np.sum(qb * (lat.K @ qb), 0) + 0.25 * np.sum(self.psi ** 2 + self.psim ** 2, 0) - 0.5 * self.c0 * lat.N
            with np.errstate(all="ignore"):
                c = np.sqrt(np.clip((E0 - b) / a, 0.0, 1.0))
            c = np.where(np.isfinite(c), c, 0.0)
            self.P = Pn
            self.q = qb + 0.5 * h * c * v
            self.qm = qb - 0.5 * h * c * v
            self.enpu_c = c
        else:
            self.P = Pn
    def state(self): return self.q, self.P.m * (self.q - self.qm) / self.h
