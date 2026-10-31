# SPDX-License-Identifier: AGPL-3.0-only
"""Phase-2 figures (bench, exp1, exp3, ENPU, exp4) -> paper/figures + reports/figures/phase2 (PDF + PNG), plus
experiments/results/phase2/p2_tables.json with the numbers used in the report tables."""
import json, os, csv, glob
import numpy as np
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
RES = os.path.join(ROOT, "experiments/results/phase2"); FIGS = [os.path.join(ROOT, "paper/figures"), os.path.join(ROOT, "reports/figures/phase2")]
TAB = {}


def save(fig, name):
    for d in FIGS:
        os.makedirs(d, exist_ok=True); fig.savefig(os.path.join(d, name + ".pdf")); fig.savefig(os.path.join(d, name + ".png"), dpi=150)
    plt.close(fig)


def jl(p): return json.load(open(os.path.join(RES, p)))


# ---------------- bench ----------------
def rows(fn):
    with open(os.path.join(ROOT, "experiments", fn)) as fh:
        return [r for r in csv.DictReader(fh) if r.get("L")]
nat, avx = rows("cpp_bench_mt_box.txt"), rows("cpp_bench_mt_box_avx2.txt")
F_BOX = 3.55
fig, axs = plt.subplots(1, 2, figsize=(11, 4))
TAB["bench"] = {}
for ax, L in zip(axs, ("32", "16")):
    for (prec, pot), st in zip([("f32", "quartic"), ("f64", "quartic"), ("f32", "quartic+logcosh"), ("f64", "quartic+logcosh")], ["o-", "s-", "o--", "s--"]):
        r = [x for x in nat if x["L"] == L and x["precision"] == prec and x["potential"] == pot]
        th = [int(x["threads"]) for x in r]; us = [float(x["us_per_sample_median"]) for x in r]
        ax.plot(th, us, st, label=f"{prec} {pot} (AVX-512)")
        TAB["bench"][f"L{L}_{prec}_{pot}_native"] = dict(zip(th, us))
        if L == "32":
            ra = [x for x in avx if x["precision"] == prec and x["potential"] == pot]
            usa = [float(x["us_per_sample_median"]) for x in ra]; tha = [int(x["threads"]) for x in ra]
            TAB["bench"][f"L32_{prec}_{pot}_avx2"] = dict(zip(tha, usa))
            TAB["bench"][f"L32_{prec}_{pot}_i9_est_5.4GHz"] = {t: u * F_BOX / 5.4 for t, u in zip(tha, usa)}
            TAB["bench"][f"L32_{prec}_{pot}_i9_est_4.0GHz"] = {t: u * F_BOX / 4.0 for t, u in zip(tha, usa)}
    ax.axhline(1e6 / 48000, color="r", lw=0.8); ax.text(1, 1e6 / 48000 * 1.05, "48 kHz budget 20.8 us", color="r", fontsize=8)
    ax.set_yscale("log"); ax.set_xlabel("threads"); ax.set_ylabel("us / sample (median, 64-sample blocks + control tick)"); ax.set_title(f"{L}x{L} honeycomb cells ({int(L)**2*4} dof)")
    ax.legend(fontsize=7); ax.grid(alpha=0.3, which="both")
fig.tight_layout(); save(fig, "p2_bench_mt_scaling")

# ---------------- exp1 ----------------
dr = jl("exp1_drift.json")
fig, ax = plt.subplots(figsize=(7, 4)); TAB["drift"] = {}
for name in ("ESAV", "RK4", "Verlet", "IMDG", "SSDG"):
    rr = [r for r in dr["runs"] if r["scheme"] == name]
    TAB["drift"][name] = {r["fs"]: dict(hw=r["h_omega_max"], inv=r["inv_maxabs_dB"], H=r["H_maxabs_dB"], div=r["diverged"]) for r in rr}
    x = [r["h_omega_max"] for r in rr]; y = [max(r["inv_maxabs_dB"] or 1e3, 1e-14) if not r["diverged"] else 1e3 for r in rr]
    ax.semilogy(x, y, "o-", label=name)
ax.set_xlabel("h * omega_max (CFL number; 2 = explicit limit)"); ax.set_ylabel("max |drift| of own invariant (dB); 1e3 = diverged"); ax.legend(); ax.grid(alpha=0.3)
fig.tight_layout(); save(fig, "p2_exp1_drift")
st = {}
for fn in ("exp1_stress.json", "exp1_stress_b.json"):
    if os.path.exists(os.path.join(RES, fn)): st.update(jl(fn)["suites"])
TAB["stress"] = {s: {k: v["div_rate"] for k, v in d["results"].items()} for s, d in st.items()}
if st:
    suites = list(st.keys()); schemes = ["ESAV", "ESAVgate", "RK4", "Verlet", "TanhFB", "IMDG", "SSDG"]
    M = np.array([[st[s]["results"].get(k, {}).get("div_rate", np.nan) for k in schemes] for s in suites])
    fig, ax = plt.subplots(figsize=(8, 0.5 * len(suites) + 1.5)); im = ax.imshow(M, cmap="Reds", vmin=0, vmax=1)
    for i in range(len(suites)):
        for j in range(len(schemes)): ax.text(j, i, "-" if np.isnan(M[i, j]) else f"{M[i,j]:.2f}", ha="center", va="center", fontsize=8)
    ax.set_xticks(range(len(schemes))); ax.set_xticklabels(schemes); ax.set_yticks(range(len(suites))); ax.set_yticklabels(suites); ax.set_title("divergence rate (16 random configs per suite)")
    fig.colorbar(im); fig.tight_layout(); save(fig, "p2_exp1_stress")
al = jl("exp1_alias.json"); TAB["alias"] = al["results"]
sp = json.load(open(os.path.join(RES, "exp1_alias_spectra.json")))
fig, axs = plt.subplots(1, 3, figsize=(14, 4), sharey=True)
for li in range(3):
    for name in ("reference", "ESAV", "RK4", "IMDG", "SSDG"):
        if name in sp: axs[li].plot(np.array(sp["freq"]) / 1e3, sp[name][li], lw=0.6, label=name)
    axs[li].set_title(f"drive level {al['levels'][li]} x kc"); axs[li].set_xlabel("kHz"); axs[li].set_xlim(0, 24)
axs[0].set_ylabel("dB"); axs[0].legend(fontsize=7); fig.tight_layout(); save(fig, "p2_exp1_alias_spectra")
if os.path.exists(os.path.join(RES, "exp1_rtf.json")): TAB["rtf_python"] = jl("exp1_rtf.json")

# ---------------- exp3 ----------------
e3 = jl("exp3_coupling.json"); A = e3["analysis"]
keys = list(A.keys()); TAB["exp3"] = {k: dict(flat=A[k]["flatness_mean"], cent=A[k]["centroid_mean_Hz"], cent_std=A[k]["centroid_std_Hz"],
                                             dfaC=A[k]["dfa_centroid"] and A[k]["dfa_centroid"]["alpha"], dfaR=A[k]["dfa_logrms"] and A[k]["dfa_logrms"]["alpha"],
                                             betaC=A[k]["psd_beta_centroid"], betaR=A[k]["psd_beta_logrms"], lle=A[k]["lle_waveform"]["lle_per_s"],
                                             mse_mean=float(np.mean(A[k]["mse_centroid"])) if A[k]["mse_centroid"] else None) for k in keys}
TAB["exp3_cal"] = {"lle": e3["lle_calibration"], "lle_logistic_dim2": e3["lle_calibration_logistic_dim1"], "dfa": e3["dfa_calibration"]}
fig, axs = plt.subplots(1, 3, figsize=(15, 4))
for k, c in (("none_s0", "k"), ("oneway_s0", "b"), ("closed_s0", "r"), ("closed_s0_k1.00", "g")):
    y = np.array(A[k]["centroid_series_head"]); axs[0].plot(np.arange(len(y)) * 4 / 93.75, y, c, lw=0.6, label=k)
axs[0].set_xlabel("s"); axs[0].set_ylabel("spectral centroid (Hz)"); axs[0].legend(fontsize=7)
xs = np.arange(len(keys)); w = 0.4
axs[1].bar(xs - w / 2, [TAB["exp3"][k]["dfaC"] or 0 for k in keys], w, label="DFA alpha centroid")
axs[1].bar(xs + w / 2, [TAB["exp3"][k]["dfaR"] or 0 for k in keys], w, label="DFA alpha log-RMS")
for v, lab in ((0.5, "white"), (1.0, "1/f"), (1.5, "brown")): axs[1].axhline(v, ls=":", color="k", lw=0.7); axs[1].text(len(keys) - 0.5, v, lab, fontsize=7)
axs[1].set_xticks(xs); axs[1].set_xticklabels(keys, rotation=70, fontsize=7); axs[1].legend(fontsize=7)
axs[2].bar(xs, [TAB["exp3"][k]["mse_mean"] or 0 for k in keys]); axs[2].set_xticks(xs); axs[2].set_xticklabels(keys, rotation=70, fontsize=7); axs[2].set_ylabel("mean multiscale SampEn (centroid)")
fig.tight_layout(); save(fig, "p2_exp3_coupling")

# ---------------- ENPU ----------------
en = json.load(open(os.path.join(RES, "enpu/enpu_ab.json"))); TAB["enpu"] = en
fig, axs = plt.subplots(1, 3, figsize=(14, 4)); sc = list(en.keys()); vn = ["A_reproject", "B_keepstate", "C_selective"]; xs = np.arange(len(sc))
for i, v in enumerate(vn):
    axs[0].bar(xs + (i - 1) * 0.27, [max(en[s][v]["energy_removed_by_enpu"], 1e-3) for s in sc], 0.27, label=v)
    axs[1].bar(xs + (i - 1) * 0.27, [en[s][v]["hf_dB_vs_ref"] for s in sc], 0.27, label=v)
    axs[2].bar(xs + (i - 1) * 0.27, [max(en[s][v]["sav_incons_mean"], 1e-12) for s in sc], 0.27, label=v)
axs[0].set_yscale("log"); axs[0].set_title("energy removed by ENPU (8 s, block 64)"); axs[1].set_title(">12 kHz energy vs per-sample reference (dB)")
axs[2].set_yscale("log"); axs[2].set_title("mean SAV inconsistency at ticks")
for ax in axs: ax.set_xticks(xs); ax.set_xticklabels(sc); ax.legend(fontsize=7)
fig.tight_layout(); save(fig, "p2_enpu_ab")

# ---------------- exp4 ----------------
TAB["exp4"] = {}
fl = sorted(glob.glob(os.path.join(RES, "exp4/exp4_*.json")))
if fl:
    fig, axs = plt.subplots(2, len(fl), figsize=(5 * len(fl), 7), squeeze=False)
    from scipy import signal
    for j, fn in enumerate(fl):
        d = json.load(open(fn)); name = list(d.keys())[0]; r = d[name]
        TAB["exp4"][name] = {k: (r[k] if k not in ("esav", "modal") else {kk: vv for kk, vv in r[k].items() if kk != "hist"}) for k in r}
        axs[0, j].plot(r["esav"]["hist"], label=f"ESAV-LF ({r['esav']['nparams']} p)"); axs[0, j].plot(r["modal"]["hist"], label=f"modal ({r['modal']['nparams']} p)")
        axs[0, j].axhline(r["loss_silence"], ls=":", color="k", lw=0.7); axs[0, j].set_title(name); axs[0, j].set_xlabel("iteration"); axs[0, j].set_ylabel("MR-STFT loss"); axs[0, j].legend(fontsize=7)
        z = np.load(os.path.join(RES, f"exp4/{name}_renders.npz"))
        for k, ls in (("target", "k"), ("esav", "b"), ("modal", "r")):
            f, P = signal.welch(z[k], 16000, nperseg=2048); axs[1, j].semilogy(f, P / P.max(), ls, lw=0.7, label=k)
        axs[1, j].set_xlim(0, 6000); axs[1, j].set_ylim(1e-8, 2); axs[1, j].set_xlabel("Hz"); axs[1, j].legend(fontsize=7)
    fig.tight_layout(); save(fig, "p2_exp4_inverse")
json.dump(TAB, open(os.path.join(RES, "p2_tables.json"), "w"), indent=1, default=float)
print("figures done")
