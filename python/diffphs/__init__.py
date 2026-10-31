# SPDX-License-Identifier: AGPL-3.0-only
"""diffphs: Phase-0 audit prototypes for Project Morphohedra (NR-PHMS).

Reference (non-real-time) Python implementations used to audit the proposal's
numerical claims.  NOT the production DSP core.
"""
from .model import Lattice, square_lattice, honeycomb_scalar, U, dU, d2U, logcosh
from .bernoulli import bernoulli_literal, bernoulli_stable
