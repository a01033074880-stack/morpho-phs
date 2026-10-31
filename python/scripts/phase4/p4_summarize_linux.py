# SPDX-License-Identifier: AGPL-3.0-only
"""Summarise Phase 4 Linux before/after bench JSON into a markdown table file (no numbers are invented:
every cell is read from experiments/results/phase4/linux_{before,after}/*.json)."""
import json, glob, os, sys
R = os.path.join(os.path.dirname(__file__), '..', '..', '..', 'experiments', 'results', 'phase4')
R = os.path.abspath(R)
def load(p):
    d = json.load(open(p)); return d['rows']
def key(r): return (r['block'], r['L'], r['threads'])
def fmt(r):
    if r is None: return '—'
    ov = f"{r['overruns']}/{r['blocks_measured']}"
    return f"{r['us_per_sample_median']:.2f} / {r['block_p99_us']:.0f} / {r['block_max_us']:.0f} / {r['p99_over_budget']:.3f} / {ov}"
out = []
def table(title, sets, blocks, Ls, threads):
    out.append(f"\n### {title}\n\n单元格 = µs/样本中位数 / 块 p99 µs / 块 max µs / p99÷预算 / 超时块数\n")
    names = list(sets)
    out.append('| 块 | L | 线程 | ' + ' | '.join(names) + ' |')
    out.append('|---|---|---|' + '---|' * len(names))
    idx = {n: {key(r): r for r in rows} for n, rows in sets.items()}
    for b in blocks:
        for L in Ls:
            for t in threads:
                out.append(f'| {b} | {L} | {t} | ' + ' | '.join(fmt(idx[n].get((b, L, t))) for n in names) + ' |')
B = os.path.join(R, 'linux_before'); A = os.path.join(R, 'linux_after')
V = ['p3spin', 'baseline', 'pin', 'pset']
def opt(p): return load(p) if os.path.exists(p) else []
qa = {'before(Phase3 CLI)': opt(f'{B}/before_quiet.json')}
qa.update({v: opt(f'{A}/A_quiet_unpaced_{v}.json') for v in V})
table('A. 安静、不 paced（与 Phase 3 CLI 同方式）', qa, [64, 128, 512], [16, 32], [3, 4, 5, 6, 8])
qc = {v: opt(f'{A}/C_quiet_paced_{v}.json') for v in V}
table('C. 安静、paced（按宿主块周期调用）', qc, [64, 128, 512], [16, 32], [3, 4, 5, 6, 8])
lb = {'before(Phase3 CLI)': opt(f'{B}/before_loaded3.json')}
lb.update({v: opt(f'{A}/B_loaded3_unpaced_{v}.json') for v in V})
table('B1. 3 个外部忙循环、不 paced', lb, [128], [32], [3, 4, 5, 6, 8])
lp = {v: opt(f'{A}/B_loaded3_paced_{v}.json') for v in V}
table('B2. 3 个外部忙循环、paced', lp, [128], [32], [3, 4, 5, 6, 8])
eq = {f'{v}+caller阻塞': opt(f'{A}/E_quiet_paced_{v}_callerblock.json') for v in ['pin', 'pset']}
table('E1. 安静、paced，专用池时宿主线程阻塞等待（caller_pool_spin=false，最终默认）', eq, [128], [32], [3, 4, 5, 6])
el = {f'{v}+caller阻塞': opt(f'{A}/E_loaded3_paced_{v}_callerblock.json') for v in ['pin', 'pset']}
table('E2. 3 个外部忙循环、paced，宿主线程阻塞等待', el, [128], [32], [3, 4, 5, 6])
open(os.path.join(R, 'linux_summary.md'), 'w').write('# Phase 4 Linux 前后对比（自动生成）\n' + '\n'.join(out) + '\n')
print('\n'.join(out))
