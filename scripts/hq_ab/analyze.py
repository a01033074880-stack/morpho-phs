# SPDX-License-Identifier: AGPL-3.0-only
# HQ A/B pack: loudness-match, ABC concat with click markers, spectrogram comparison PNGs, metric table.
import json, os, subprocess, numpy as np, soundfile as sf, pyloudnorm as pyln
from scipy import signal
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
from matplotlib import font_manager as fm
for f in ['/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc', '/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc']:
    if os.path.exists(f): fm.fontManager.addfont(f); plt.rcParams['font.family'] = fm.FontProperties(fname=f).get_name(); break
FS = 48000; TARGET = -20.0; OUT = 'pack'; os.makedirs(f'{OUT}/wav', exist_ok=True); os.makedirs(f'{OUT}/mp3', exist_ok=True); os.makedirs(f'{OUT}/png', exist_ok=True)
FON = {'glass_gong': (1100, .85), 'glass_harmonica': (2200.0753, .45), 'spiral_drone': (900, .3), 'shattered_ice': (1800.1092, .5), 'cathedral_gyro_32': (1000, .5)}
SETS = [('glass_gong', 'Glass Gong', 'Pluck / bell', ['quartic', 'hqfast', 'hqexact']),
        ('glass_harmonica', 'Glass Harmonica', 'Bowed / sustained', ['quartic', 'hqfast', 'hqexact']),
        ('spiral_drone', 'Spiral Drone', 'Spiral / Barkley texture', ['quartic', 'hqfast', 'hqexact']),
        ('shattered_ice', 'Shattered Ice', 'FX (Defect Freezer) on the drum + arpeggio loop', ['quartic', 'hqfast', 'hqexact']),
        ('cathedral_gyro_32', 'Cathedral Gyro 32', 'L32 pluck: quartic vs HQ fast (HQ exact is limited to 16x16)', ['quartic', 'hqfast'])]
LAB = {'quartic': 'Quartic', 'hqfast': 'HQ fast', 'hqexact': 'HQ exact'}
meter = pyln.Meter(FS)
def db(x): return 20 * np.log10(np.maximum(x, 1e-12))
def true_peak(x):
    return np.max(np.abs(signal.resample_poly(x, 4, 1, axis=0)))
def welch_db(m):
    f, P = signal.welch(m, FS, nperseg=16384, noverlap=12288); return f, 10 * np.log10(P + 1e-20)
def peaks(f, P, n=10):
    sel = (f > 40) & (f < 8000); idx, pr = signal.find_peaks(P * sel - 200 * (~sel), prominence=6, distance=3)
    idx = idx[np.argsort(P[idx])[::-1][:n]]
    out = []
    for i in idx:   # parabolic interpolation
        a, b, c = P[i - 1], P[i], P[i + 1]; d = 0.5 * (a - c) / (a - 2 * b + c); out.append(f[i] + d * (f[1] - f[0]))
    return np.array(sorted(out))
def cents(a, b): return 1200 * np.log2(a / b)
def peak_freq(m, t0, t1, f0, width_c=50):
    seg = m[int(t0 * FS):int(t1 * FS)] * np.hanning(int(t1 * FS) - int(t0 * FS)); N = 1 << 17
    S = np.abs(np.fft.rfft(seg, N)); f = np.fft.rfftfreq(N, 1 / FS)
    sel = np.where((f > f0 * 2 ** (-width_c / 1200)) & (f < f0 * 2 ** (width_c / 1200)))[0]; i = sel[np.argmax(S[sel])]
    a, b, c = np.log(S[i - 1:i + 2] + 1e-20); d = 0.5 * (a - c) / (a - 2 * b + c); return f[i] + d * (f[1] - f[0]), db(S[i])
def decay_t60(m):
    w = int(0.02 * FS); e = np.sqrt(np.convolve(m ** 2, np.ones(w) / w, 'same')); t = np.arange(len(m)) / FS
    L = db(e); i0 = int(8.05 * FS); seg = L[i0:int(9.9 * FS)]; tt = t[i0:int(9.9 * FS)]
    top = seg[0]; ok = seg > top - 40
    if ok.sum() < FS * 0.1: return float('nan')
    k = np.polyfit(tt[ok], seg[ok], 1)[0]
    return float(-60 / k) if k < -0.5 else float('inf')
def centroid(m):
    f, t, Z = signal.stft(m, FS, nperseg=2048); S = np.abs(Z) ** 2; w = S.sum(0)
    c = (f[:, None] * S).sum(0) / np.maximum(w, 1e-20); return float(np.sum(c * w) / np.sum(w))
def click(n):
    out = []
    for k in range(n):
        c = np.zeros(int(0.12 * FS)); L = int(0.004 * FS); tt = np.arange(L) / FS
        c[:L] = 0.25 * np.sin(2 * np.pi * 3000 * tt) * np.hanning(L); out.append(c)
    return np.concatenate(out)
rows = {}
for key, name, role, modes in SETS:
    X = {}; raw_lufs = {}
    for m in modes:
        x, fs = sf.read(f'raw/{key}__{m}.wav', dtype='float64'); assert fs == FS; X[m] = x; raw_lufs[m] = meter.integrated_loudness(x)
    g = {m: 10 ** ((TARGET - raw_lufs[m]) / 20) for m in modes}
    tp = max(true_peak(X[m] * g[m]) for m in modes); trim = min(0.0, -1.0 - db(tp))   # keep every version <= -1 dBTP
    Y = {m: X[m] * g[m] * 10 ** (trim / 20) for m in modes}
    lufs_final = TARGET + trim
    for m in modes:
        sf.write(f'{OUT}/wav/{key}__{m}.wav', Y[m].astype('float32'), FS, subtype='PCM_24')
    gap = np.zeros(int(0.5 * FS)); parts = []
    for k, m in enumerate(modes):
        c = click(k + 1); parts += [np.stack([c, c], 1), np.zeros((int(0.3 * FS), 2)), Y[m], np.zeros((len(gap), 2))]
    abc = np.concatenate(parts); sf.write(f'{OUT}/wav/{key}__ABC.wav', abc.astype('float32'), FS, subtype='PCM_24')
    # metrics on mono mid
    M = {m: Y[m].mean(1) for m in modes}
    fq, Pq = welch_db(M['quartic']); pq = peaks(fq, Pq)
    met = {}
    tone, hq = FON[key]; f_on = np.sqrt(0.2 * hq) * tone      # on-site (small-amplitude log-cosh) frequency, beta/m
    fQ = None
    for m in modes:
        f, P = welch_db(M[m])
        # strongest partial of the chord section (4-8 s, same notes in every version)
        fc, Pc = signal.welch(M[m][int(4.1 * FS):int(7.9 * FS)], FS, nperseg=16384); sel = (fc > 60) & (fc < 6000)
        f0 = fc[sel][np.argmax(Pc[sel])]
        fe, le = peak_freq(M[m], 4.15, 5.15, f0); fl, ll = peak_freq(M[m], 8.3, 9.8, f0)
        if m == 'quartic': fQ = fe
        met[m] = dict(rms_db_before=float(db(np.sqrt(np.mean(X[m] ** 2)))), lufs_raw=float(raw_lufs[m]), centroid_hz=centroid(M[m]),
                      strongest_hz=float(fe), predicted_hz=float(np.sqrt(fQ ** 2 + f_on ** 2)) if m != 'quartic' else float(fe),
                      drift_cents=float(cents(fl, fe)), drift_level_db=float(ll - le), t60_s=decay_t60(M[m]))
    if 'hqexact' in modes:
        _, Pf = welch_db(M['hqfast']); _, Pe = welch_db(M['hqexact']); ok = (Pe > Pe.max() - 50) | (Pf > Pf.max() - 50)
        met['hqexact']['fast_vs_exact_mean_abs_db'] = float(np.mean(np.abs(Pe[ok] - Pf[ok])))
        a_, b_ = X['hqfast'], X['hqexact']; met['hqexact']['fast_vs_exact_null_db'] = float(db(np.sqrt(np.mean((a_ - b_) ** 2)) / np.sqrt(np.mean(a_ ** 2))))
    rows_fon = f_on
    for m in modes: met[m]['rms_diff_db'] = met[m]['rms_db_before'] - met['quartic']['rms_db_before']
    rows[key] = dict(name=name, role=role, lufs=lufs_final, f_onsite_hz=float(rows_fon), metrics=met)
    # figure
    nrow = len(modes) + 1
    fig = plt.figure(figsize=(12, 2.6 * nrow + 1.6)); gs = fig.add_gridspec(nrow + 1, 1, height_ratios=[1] * nrow + [0.55])
    specs = {}
    for k, m in enumerate(modes):
        f, t, Z = signal.stft(M[m], FS, nperseg=4096, noverlap=3072); S = db(np.abs(Z)); specs[m] = S
    vmax = max(s.max() for s in specs.values())
    for k, m in enumerate(modes):
        ax = fig.add_subplot(gs[k]); ax.pcolormesh(t, f, specs[m], vmin=vmax - 80, vmax=vmax, cmap='magma', shading='auto')
        ax.set_yscale('log'); ax.set_ylim(40, 16000); ax.set_ylabel('Hz'); ax.set_title(f'{name}: {LAB[m]}  (matched to {lufs_final:.1f} LUFS-I)', fontsize=10, loc='left')
        ax.set_xticklabels([])
    other = 'hqexact' if 'hqexact' in modes else 'hqfast'
    ax = fig.add_subplot(gs[len(modes)])
    D = np.clip(specs[other] - specs['quartic'], -24, 24); D[(specs[other] < vmax - 70) & (specs['quartic'] < vmax - 70)] = 0
    im = ax.pcolormesh(t, f, D, vmin=-24, vmax=24, cmap='RdBu_r', shading='auto'); ax.set_yscale('log'); ax.set_ylim(40, 16000); ax.set_ylabel('Hz'); ax.set_xlabel('s')
    ax.set_title(f'Difference {LAB[other]} - Quartic (dB, after loudness match; red = louder in HQ)', fontsize=10, loc='left')
    fig.colorbar(im, ax=ax, pad=0.01, fraction=0.03)
    ax = fig.add_subplot(gs[nrow]); ax.axis('off')
    cell = [[LAB[m], f"{met[m]['centroid_hz']:.0f}", f"{met[m]['strongest_hz']:.1f}", ('-' if m == 'quartic' else f"{met[m]['predicted_hz']:.0f}"),
             f"{met[m]['drift_cents']:+.1f} ({met[m]['drift_level_db']:+.0f} dB)", (f"{met[m]['t60_s']:.2f}" if np.isfinite(met[m]['t60_s']) else 'n/a'), f"{met[m]['rms_diff_db']:+.1f}"] for m in modes]
    tb = ax.table(cellText=cell, colLabels=['Mode', 'Centroid\nHz', 'Strongest\npartial Hz', f'Predicted\nsqrt(fQ^2+fon^2)\nfon = {rows_fon:.0f} Hz', 'Loud->quiet drift\ncents (level)', 'T60\ntail s', 'RMS vs Quartic\nbefore match dB'],
                  loc='center', cellLoc='center'); tb.auto_set_font_size(False); tb.set_fontsize(9); tb.scale(1, 1.25)
    for (r_, c_), cl in tb.get_celld().items():
        if r_ == 0: cl.set_height(cl.get_height() * 2.4)
    fig.tight_layout(); fig.savefig(f'{OUT}/png/{key}_hq_ab.png', dpi=110); plt.close(fig)
    for fn in [f'{key}__{m}' for m in modes] + [f'{key}__ABC']:
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', f'{OUT}/wav/{fn}.wav', '-codec:a', 'libmp3lame', '-b:a', '160k', f'{OUT}/mp3/{fn}.mp3'], check=True)
json.dump(rows, open(f'{OUT}/hq_ab_metrics.json', 'w'), indent=1)
for k, r in rows.items():
    print(r['name'], f"{r['lufs']:.1f}")
    for m, v in r['metrics'].items(): print('  ', m, {a: round(b, 2) for a, b in v.items()})
