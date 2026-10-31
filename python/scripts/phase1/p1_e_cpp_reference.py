# SPDX-License-Identifier: AGPL-3.0-only
"""Reference trajectory for the C++ cross-check (tests/test_main.cpp).  Deterministic closed-form inputs."""
import os, sys
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../.."))
from diffphs.gyro import GyroHoneycomb, GyroESAV, Params, audio_params
ROOT = os.path.join(os.path.dirname(__file__), "../../..")
AP = audio_params(); h = AP["h"]; L = 4
lat = GyroHoneycomb(L, L, AP["k0"], AP["kc"]); N = lat.N; i = np.arange(N)
def params(phase):
    return Params(N, 1, m=(1 + 0.3 * np.sin(i + phase))[:, None], alpha=(AP["kc"] * 0.1 * (1 + np.cos(i)))[:, None],
                  beta=(AP["k0"] * (1 + 0.5 * np.sin(2 * i)))[:, None], mu=(-5 - 3 * np.cos(i))[:, None],
                  eta=np.full((N, 1), 1e-6), g=(AP["g"] * np.cos(0.7 * i + phase))[:, None])
k = np.arange(2 * N)
q0 = (0.3 * np.sin(1.3 * k))[:, None]; v0 = (200 * np.cos(0.9 * k))[:, None]
I = GyroESAV(lat, h, params(0.0), q0, v0, c0=1.0)
ports = [(3, 1.0, 0.0), (17, 0.0, 1.0), (30, 0.6, 0.8)]
B = np.zeros((2 * N, 3))
for p, (s, bx, by) in enumerate(ports): B[2 * s, p] = bx; B[2 * s + 1, p] = by
ys = []; Es = [I.energy()[0]]
for n in range(400):
    u = np.array([1e4 * np.sin(2 * np.pi * 440 * n * h), 5e3 * np.cos(2 * np.pi * 660 * n * h), 2e3 * (n % 50 == 0)])
    vb = I.step((B @ u)[:, None], return_vbar=True)
    ys.append(B.T @ vb[:, 0])
    if n == 199:
        s = I.enpu(params(0.5))
    Es.append(I.energy()[0])
with open(f"{ROOT}/tests/ref_L4.txt", "w") as fh:
    fh.write(f"{L} {AP['fs']:.17g} {AP['k0']:.17g} {AP['kc']:.17g} {AP['g']:.17g}\n")
    fh.write(" ".join(f"{x:.17g}" for x in I.q[:, 0]) + "\n")
    fh.write(" ".join(f"{x:.17g}" for x in np.array(ys).ravel()) + "\n")
    fh.write(" ".join(f"{x:.17g}" for x in Es) + "\n")
print("wrote; final E", Es[-1], "enpu s", s)
