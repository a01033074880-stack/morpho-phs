# SPDX-License-Identifier: AGPL-3.0-only
"""Exact flow of the Rayleigh active/dissipative port  dp/dt = (mu - eta p^2/m^2) p/m.

With a = mu/m, b = eta/m^3 this is dp/dt = a p - b p^3 (Bernoulli).  w = 1/p^2 obeys
dw/dt = -2a w + 2b, hence  w(t) = w0 e^{-2at} + (b/a)(1 - e^{-2at}).
"""
import numpy as np


def phi1(z):
    """(e^z - 1)/z, accurate near z = 0."""
    z = np.asarray(z, dtype=float)
    out = np.empty_like(z)
    small = np.abs(z) < 1e-5
    zs = z[small]
    out[small] = 1.0 + zs / 2.0 + zs * zs / 6.0
    zl = z[~small]
    with np.errstate(over="ignore"):
        out[~small] = np.expm1(zl) / zl
    return out


def bernoulli_literal(p, m, mu, eta, dt):
    """Formula exactly as written in the proposal (NaN at mu = 0, cancellation near 0)."""
    with np.errstate(all="ignore"):
        p = np.asarray(p, float); mu = np.asarray(mu, float); m = np.asarray(m, float)
        num = p * np.exp((mu / m) * dt)
        den = np.sqrt(1.0 + (eta / (mu * m * m)) * p * p * (np.exp(2.0 * mu * dt / m) - 1.0))
        return num / den


def bernoulli_stable(p, m, mu, eta, dt):
    """Overflow-free, mu-uniform form:  1/p_new^2 = e^{-z}/p^2 + 2 b dt phi1(-z), z = 2 a dt.

    Valid for any sign of mu (incl. mu = 0) and any dt >= 0; |p_new| is monotone between
    |p| and the sub-flow fixed point m*sqrt(mu/eta) (mu>0) or decays to 0 (mu<=0).
    """
    p = np.asarray(p, dtype=float)
    a = np.asarray(mu, dtype=float) / m
    b = np.asarray(eta, dtype=float) / (m * m * m)
    z = 2.0 * a * dt
    # 1/p_new^2 = e^{-z}/p^2 + 2 b dt phi1(-z)   (w = 1/p^2 is affine in time-flow)
    with np.errstate(all="ignore"):
        ez = np.exp(np.minimum(-z, 700.0))
        c = 2.0 * b * dt * phi1(-z)
        invp2 = 1.0 / (p * p)
        w = np.where(ez == 0.0, 0.0, ez * invp2) + c
        out = np.where(p == 0.0, 0.0, np.sign(p) / np.sqrt(w))
    return out
