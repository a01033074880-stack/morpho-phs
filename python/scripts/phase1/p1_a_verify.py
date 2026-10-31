# SPDX-License-Identifier: AGPL-3.0-only
"""Phase 1 (A): machine verification of the ESAV-LF discrete power balance.
 A1  sympy: exact polynomial identity  E^{n+1/2}-E^{n-1/2} - h(vb^T f - vb^T D vb) == (w/2)^T R
     where R is the scheme residual (R=0 on scheme trajectories), for a 1-site (2 DOF) and a
     2-site (4 DOF) cell with fully symbolic K (symmetric), m, g, d, f, U-gradient factors.
 A2  mpmath (50 digits): run the scheme on a single cell and 2x2 / 3x3 honeycomb flakes with
     random parameters, random inputs, mu<0 / mu>0, and measure the balance residual and the
     passivity inequality  E^{n+1/2}-E^{n-1/2} <= h u^T y  (mu<=0)."""
import json, os, sys, time
import numpy as np, sympy as S, mpmath as mp
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../.."))
from diffphs.gyro import GyroHoneycomb
OUT = os.path.join(os.path.dirname(__file__), "../../../experiments/results/phase1")
os.makedirs(OUT, exist_ok=True)
res = {}

# ---------------- A1 symbolic ----------------
def symbolic_identity(nsite):
    n = 2 * nsite
    h, c0 = S.symbols("h c0", positive=True)
    q = S.Matrix(S.symbols(f"q0:{n}")); qm = S.Matrix(S.symbols(f"qm0:{n}")); w = S.Matrix(S.symbols(f"w0:{n}"))
    psi = S.Matrix(S.symbols(f"psi0:{n}")); psim = S.Matrix(S.symbols(f"psim0:{n}")); gg = S.Matrix(S.symbols(f"g0:{n}"))
    f = S.Matrix(S.symbols(f"f0:{n}"))
    m = S.symbols(f"m0:{nsite}", positive=True); gy = S.symbols(f"gy0:{nsite}"); dd = S.symbols(f"d0:{nsite}")
    Ksym = S.zeros(n, n)
    for i in range(n):
        for j in range(i, n):
            s = S.Symbol(f"k{i}_{j}"); Ksym[i, j] = s; Ksym[j, i] = s
    M = S.diag(*[m[i // 2] for i in range(n)]); D = S.diag(*[dd[i // 2] for i in range(n)])
    G = S.zeros(n, n)
    for s_ in range(nsite):
        G[2 * s_, 2 * s_ + 1] = gy[s_]; G[2 * s_ + 1, 2 * s_] = -gy[s_]
    qp = qm + w; psip = psim + gg.multiply_elementwise(w)
    def E(qa, qb, pa, pb):  # E for pair (qa = newer, qb = older)
        v = (qa - qb) / h
        return (v.T * M * v)[0] / 2 + (qa.T * Ksym * qb)[0] / 2 + ((pa.T * pa)[0] + (pb.T * pb)[0]) / 4 - n * c0 / 2
    vb = w / (2 * h)
    R = M * (qp - 2 * q + qm) / h ** 2 + Ksym * q + gg.multiply_elementwise(psip + psim) / 2 - (G - D) * vb - f
    lhs = E(qp, q, psip, psi) - E(q, qm, psi, psim) - h * ((vb.T * f)[0] - (vb.T * D * vb)[0])
    # psi^{n} enters both energies identically and cancels
    ident = S.expand(lhs - (w.T * R)[0] / 2)
    return ident == 0, len(S.Add.make_args(S.expand(lhs)))

t = time.time()
for ns in (1, 2):
    ok, nterms = symbolic_identity(ns)
    res[f"A1_symbolic_{ns}site"] = dict(identity_holds=bool(ok), expanded_terms=nterms)
    print("A1", ns, ok, nterms, flush=True)
res["A1_time_s"] = time.time() - t

# ---------------- A2 mpmath ----------------
mp.mp.dps = 50
def lc(x):
    return abs(x) + mp.log1p(mp.e ** (-2 * abs(x))) - mp.log(2)

def run_mp(lat_K, nsite, steps, rng, mu_sign, fs=48000.0):
    n = 2 * nsite
    K = [[mp.mpf(float(lat_K[i, j])) for j in range(n)] for i in range(n)]
    h = mp.mpf(1) / fs; c0 = mp.mpf(1)
    m = [mp.mpf(rng.uniform(0.5, 2.0)) for _ in range(nsite)]
    al = [mp.mpf(rng.uniform(0, 3e7)) for _ in range(n)]; be = [mp.mpf(rng.uniform(0, 1e6)) for _ in range(n)]
    gy = [mp.mpf(rng.uniform(-6000, 6000)) for _ in range(nsite)]
    mu = [mp.mpf(mu_sign * rng.uniform(0, 80)) for _ in range(nsite)]
    eta = [mp.mpf(rng.uniform(0, 1e-4)) for _ in range(nsite)]
    Uf = lambda x, i: al[i] / 4 * x ** 4 + be[i] * lc(x)
    dUf = lambda x, i: al[i] * x ** 3 + be[i] * mp.tanh(x)
    q = [mp.mpf(rng.normal(0, 0.5)) for _ in range(n)]
    qm = [q[i] - h * mp.mpf(rng.normal(0, 300)) for i in range(n)]
    psi = [mp.sqrt(2 * Uf(q[i], i) + c0) for i in range(n)]; psim = [mp.sqrt(2 * Uf(qm[i], i) + c0) for i in range(n)]
    def energy(q, qm, psi, psim):
        e = mp.mpf(0)
        for i in range(n):
            v = (q[i] - qm[i]) / h
            e += m[i // 2] * v * v / 2 + q[i] * sum(K[i][j] * qm[j] for j in range(n)) / 2 + (psi[i] ** 2 + psim[i] ** 2) / 4
        return e - n * c0 / 2
    worst_rel = mp.mpf(0); worst_pass = mp.mpf(-1); E = energy(q, qm, psi, psim); E0 = E
    for st in range(steps):
        f = [mp.mpf(rng.normal(0, 2e3)) for _ in range(n)]
        gg = [dUf(q[i], i) / mp.sqrt(2 * Uf(q[i], i) + c0) for i in range(n)]
        d = []
        for s_ in range(nsite):
            vx = (q[2 * s_] - qm[2 * s_]) / h; vy = (q[2 * s_ + 1] - qm[2 * s_ + 1]) / h
            d.append(-mu[s_] + eta[s_] * (vx * vx + vy * vy))
        diag = [m[i // 2] / h ** 2 + gg[i] ** 2 / 2 + d[i // 2] / (2 * h) for i in range(n)]
        rhs = [2 * m[i // 2] * (q[i] - qm[i]) / h ** 2 - sum(K[i][j] * q[j] for j in range(n)) - gg[i] * psim[i] + f[i] for i in range(n)]
        w = [None] * n
        for s_ in range(nsite):
            c = gy[s_] / (2 * h); ax, ay = diag[2 * s_], diag[2 * s_ + 1]; rx, ry = rhs[2 * s_], rhs[2 * s_ + 1]
            det = ax * ay + c * c
            w[2 * s_] = (ay * rx + c * ry) / det; w[2 * s_ + 1] = (ax * ry - c * rx) / det
        qp = [qm[i] + w[i] for i in range(n)]; psip = [psim[i] + gg[i] * w[i] for i in range(n)]
        vb = [wi / (2 * h) for wi in w]
        pin = sum(vb[i] * f[i] for i in range(n)); pdis = sum(vb[i] * d[i // 2] * vb[i] for i in range(n))
        En = energy(qp, q, psip, psi)
        resid = En - E - h * (pin - pdis)
        scale = abs(E) + abs(En) + h * (abs(pin) + abs(pdis))
        worst_rel = max(worst_rel, abs(resid) / scale)
        if mu_sign <= 0:
            worst_pass = max(worst_pass, (En - E - h * pin) / scale)
        qm, q, psim, psi, E = q, qp, psi, psip, En
    return float(worst_rel), float(worst_pass), float(E0), float(E)

rng = np.random.default_rng(1)
t = time.time()
cases = []
from diffphs.gyro import GyroHoneycomb
k0, kc = 0.1 * (2 * np.pi * 1500) ** 2, (2 * np.pi * 1500) ** 2
single_K = np.array([[k0 + kc, 0.3 * kc], [0.3 * kc, k0 + 0.5 * kc]])
for name, Kd, ns, steps, reps in (("single_cell", single_K, 1, 400, 6),
                                   ("flake_2x2", GyroHoneycomb(2, 2, k0, kc).K.toarray(), 8, 150, 3),
                                   ("flake_3x3", GyroHoneycomb(3, 3, k0, kc).K.toarray(), 18, 60, 2)):
    for mu_sign in (-1, +1):
        out = [run_mp(Kd, ns, steps, rng, mu_sign) for _ in range(reps)]
        r = dict(steps=steps, reps=reps, max_rel_balance_residual=max(o[0] for o in out),
                 max_rel_passivity_excess=(max(o[1] for o in out) if mu_sign < 0 else None))
        res[f"A2_{name}_mu{'neg' if mu_sign < 0 else 'pos'}"] = r
        print("A2", name, mu_sign, r, flush=True)
res["A2_time_s"] = time.time() - t
res["A2_dps"] = 50
json.dump(res, open(f"{OUT}/p1_a_verify.json", "w"), indent=1)
