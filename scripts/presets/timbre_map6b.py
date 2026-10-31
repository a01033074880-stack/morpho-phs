#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Phase 6b timbre map from qa_analyze.py features: (a) median spectral centroid vs spectral flux, (b) PCA (z-scored
log-centroid, log-rolloff85, flux, flatness, low-band ratio).  Also lists each preset's nearest neighbour in the
z-scored feature space (small distance = candidate near-duplicate, to be checked by ear).
usage: timbre_map6b.py <analysis.json> <out.png> [<nn.json>]
"""
import json, sys
import numpy as np
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt

A = json.load(open(sys.argv[1]))
F = np.array([[np.log(a["centroid_hz"]), np.log(a["rolloff85_hz"]), a["flux"], a["flatness"], a["low_ratio"]] for a in A])
Z = (F - F.mean(0)) / (F.std(0) + 1e-12)
U, S, Vt = np.linalg.svd(Z, full_matrices=False)
P = Z @ Vt[:2].T; ev = S ** 2 / np.sum(S ** 2)
cats = list(dict.fromkeys(a["category"] for a in A))
cmap = plt.get_cmap("tab20")
fig, ax = plt.subplots(1, 2, figsize=(17, 8), dpi=110)
for i, a in enumerate(A):
    c = cmap(cats.index(a["category"]) % 20); m = "o" if a["mode"] == 2 else "s"
    for k, (x, y) in enumerate([(a["centroid_hz"], a["flux"]), (P[i, 0], P[i, 1])]):
        ax[k].scatter(x, y, color=c, marker=m, s=46, edgecolor="k", linewidth=0.4, zorder=3)
        ax[k].annotate(a["name"], (x, y), fontsize=6.5, xytext=(3, 3), textcoords="offset points")
ax[0].set_xscale("log"); ax[0].set_xlabel("median spectral centroid [Hz] (log)"); ax[0].set_ylabel("mean spectral flux")
ax[0].set_title("(a) centroid vs flux")
ax[1].set_xlabel(f"PC1 ({ev[0]*100:.0f} %)"); ax[1].set_ylabel(f"PC2 ({ev[1]*100:.0f} %)"); ax[1].set_title("(b) PCA of 5 timbre features")
for k in range(2): ax[k].grid(alpha=0.3)
h = [plt.Line2D([], [], marker="o", ls="", color=cmap(i % 20), label=c) for i, c in enumerate(cats)]
h += [plt.Line2D([], [], marker="o", ls="", color="grey", label="instrument"), plt.Line2D([], [], marker="s", ls="", color="grey", label="FX")]
ax[1].legend(handles=h, fontsize=7, loc="best", ncol=2)
fig.suptitle("MORPHO-PHS Phase 6b factory bank - timbre map (57 presets, demo renders)")
fig.tight_layout(); fig.savefig(sys.argv[2])
D = np.linalg.norm(Z[:, None] - Z[None], axis=2); np.fill_diagonal(D, np.inf)
nn = []
for i, a in enumerate(A):
    same = [j for j in range(len(A)) if A[j]["mode"] == 2 and a["mode"] == 2 or A[j]["mode"] != 2 and a["mode"] != 2]
    j = min(same, key=lambda j: D[i, j]); nn.append({"name": a["name"], "nearest": A[j]["name"], "dist": round(float(D[i, j]), 3)})
nn.sort(key=lambda r: r["dist"])
if len(sys.argv) > 3: json.dump({"explained_variance": ev.round(3).tolist(), "loadings": Vt[:2].round(3).tolist(), "nn": nn}, open(sys.argv[3], "w"), indent=1)
for r in nn[:10]: print(r)
print("median NN distance", np.median([r["dist"] for r in nn]))
