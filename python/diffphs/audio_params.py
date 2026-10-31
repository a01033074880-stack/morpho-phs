# SPDX-License-Identifier: AGPL-3.0-only
"""Audio-rate parameter set used by the Phase-0 audits (SI-like units, m0 = 1).
Chosen so that the linear lattice spans ~150 Hz .. ~4 kHz and the Duffing term doubles
the stiffness at |q| ~ 1 (q is dimensionless displacement)."""
import numpy as np
FS = 48000.0
H = 1.0 / FS
M0 = 1.0
K0 = (2 * np.pi * 150.0) ** 2          # grounding spring
KC = (2 * np.pi * 4000.0) ** 2 / 8.0    # NN spring (square lattice band top ~4 kHz)
ALPHA0 = KC / 3.0                       # 3*alpha*q^2 = kc at |q| = 1
BETA0 = K0                              # logcosh softening/hardening scale
SIGMA = 2 * np.pi * 600.0               # gyroscopic coupling magnitude (rad/s * m)
MU0 = 60.0                              # negative-resistance gain (1/s * m)
ETA0 = 4 * MU0 / (3 * 1000.0 ** 2)      # Rayleigh: limit-cycle |v| ~ 1000
