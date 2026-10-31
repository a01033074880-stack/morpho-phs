#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Phase 6b preset documentation: presets/PRESET_LIST.md (user list, Chinese + English) and
experiments/results/phase6b/bank_summary.json (presets + QA + CPU + demo + origin, used by the report).
usage: docs6b.py   (reads presets/*.morpho, experiments/results/phase6b/{qa/analysis.json,cpu/cpu_table.json,demos/demos.json})
"""
import json, pathlib, sys
ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/presets"))
import bank6b

R = ROOT / "experiments/results/phase6b"
qa = {a["name"]: a for a in json.load(open(R / "qa/analysis.json"))}
cpu = {c["name"]: c for c in json.load(open(R / "cpu/cpu_table.json"))}
origin = {b["name"]: b.get("origin") for b in bank6b.BANK}
CLS = {"eco": "eco ●○○", "standard": "standard ●●○", "high": "high ●●●"}
rows = []
for f in sorted((ROOT / "presets").glob("*.morpho")):
    p = json.loads(f.read_text(encoding="utf-8")); n = p["name"]; q = qa[n]; c = cpu[n]
    rows.append(dict(file=f.name, num=f.name[:3], name=n, category=p["category"], mode=p["params"]["mode"], lattice=p["params"]["lattice"],
                     potential=p["params"]["potential"], exciter=p["params"].get("exciter"), tags=p["tags"], en=p["description"], zh=p["description_zh"],
                     macros=p["macros"]["names"], cpu_class=p["cpu_class"], i9_p99_load=c["i9_p99_over_budget"], cpu_source=c.get("cpu_source", "estimated"), est_i9_p99_load=c.get("est_i9_p99_over_budget"),
                     i9_median_us=c.get("i9_median_us"), i9_overruns=c.get("i9_overruns"),
                     box_us=c["box_us"], threads=c["threads"], lufs_i=round(q["lufs_i"], 1), lufs_target=q["lufs_target"], true_peak=round(q["true_peak_dbtp"], 1),
                     peak_limited=q["peak_limited"], tail_s=q["tail_s"], centroid_hz=round(q["centroid_hz"]), rolloff_hz=round(q["rolloff85_hz"]),
                     flux=round(q["flux"], 3), dc_dbfs=round(q["dc_dbfs"], 1), fails=q["fails"], origin=origin.get(n) or "new (Phase 6b)",
                     demo=pathlib.Path(q["wav"]).stem))
(R / "bank_summary.json").write_text(json.dumps(rows, indent=1, ensure_ascii=False))

L = ["<!-- SPDX-License-Identifier: AGPL-3.0-only -->", "# MORPHO-PHS 出厂预设列表 / Factory preset list (Phase 6b2)", "",
     "由 `scripts/presets/docs6b.py` 根据 `presets/*.morpho` 与 Phase 6b2 QA/CPU 测量自动生成。Generated from the preset files and the Phase 6b2 measurements.", "",
     "- CPU 等级 / CPU class：i9-13900HX（MSVC 构建）实时预算（48 kHz、128 样本块，20.83 µs/样本）的 p99 占比：eco < 20 %，standard 20–50 %，high ≥ 50 %。CPU 列括号内为 p99/预算：来自 2026-10-31 的 i9 实测（`experiments/results/phase6b2/i9/qa.json`）；标“估”的是 6b2 改动后尚未在 i9 复测的预设，用保守估计（`scripts/presets/cpu6b.py`）。",
     "- 响度 / Loudness：乐器 −18 LUFS-I，效果器 −20 LUFS-I（= 演示干声输入，旁通对比时响度中性），真峰值 ≤ −1 dBTP；6b2 起样本峰值 ≤ 0.80（限幅器拐点 0.9 以下 1 dB），FX 另按 25 个噪声种子的最坏峰值留余量。标 “峰值受限” 的打击类预设按峰值归一（EBU 单发声音做法），积分响度低于目标。",
     "- 试听 / Demos：`experiments/results/phase6b/demos/<文件名>.{wav,ogg,mp3}`（演示乐句 8 s + 余音）；6b2 改动的预设见 `experiments/results/phase6b2/demos/`。", ""]
for bank, title in (("Instrument", "乐器 / Instrument (MORPHO-PHS)"), ("FX", "效果器 / FX (MORPHO-PHS FX)")):
    rs = [r for r in rows if (r["mode"] == "Instrument") == (bank == "Instrument")]
    L += [f"## {title} — {len(rs)} 个预设", "", "| # | 名称 Name | 类别 Category | 晶格 | 势能 | CPU | LUFS-I | 余音 s | 说明 |", "|---|---|---|---|---|---|---|---|---|"]
    for r in rs:
        pl = " (峰值受限)" if r["peak_limited"] else ""
        L.append(f"| {r['num']} | **{r['name']}** | {r['category']} | {r['lattice']} | {r['potential'].split(' ')[0]} | {CLS[r['cpu_class']]} ({r['i9_p99_load']:.2f}{'' if r['cpu_source'] == 'i9 measured' else ' 估'}) | {r['lufs_i']}{pl} | {r['tail_s']:.1f} | {r['zh']} |")
    L.append("")
    L += ["### 详情 / Details", ""]
    for r in rs:
        L += [f"**{r['num']} {r['name']}** — {r['category']} · `{', '.join(r['tags'])}`  ", f"{r['zh']}  ", f"*{r['en']}*  ",
              "宏 Macros: " + " · ".join(f"{i+1} {m}" for i, m in enumerate(r["macros"])) + f"  ", f"来源 Origin: {r['origin']}", ""]
L += ["## 宏的极端位置 / Macro corners", "",
      "- 乐器 **Life** 宏的上端约为 Hopf 阈值的 1.35 倍：越过阈值后晶格自激，能量被能量门限制在 E_cap，声音持续（有界，不会失控），松开音符也不会自然结束——这是“活体”音色的设计，不是故障。",
      "- 效果器 **Freeze** 宏推到 1 时晶格无损（mu = eta = 0），声音无限保持；回到较低值即开始衰减。",
      "- 带螺旋波种子的预设（Spiral Drone、Turing Bells、Big Spiral、Spiral Lock 等）Life 设为 0：螺旋波在介质中永久存在，若再加 Life 会永久自持。", ""]
(ROOT / "presets/PRESET_LIST.md").write_text("\n".join(L), encoding="utf-8")
print(len(rows), "rows; fails:", sum(1 for r in rows if r["fails"]))
