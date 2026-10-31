# `.morpho` preset format — version 2

<!-- SPDX-License-Identifier: AGPL-3.0-only -->

**Version history.** Version 1 = Phase 3. Version 2 (Phase 4) adds `edge_width` and `edge_crosstalk` and changes the
`interp` default to `1 sample` (decision D1). The loader reads both versions and always writes version 2.
Backward compatibility of a version-1 file: a missing `interp` is read as `8 samples` (the v1 default, so the sound
is unchanged), and the missing `edge_width` / `edge_crosstalk` take 1 / 0, which is the identity (= v1 behaviour).
The 27 factory presets were migrated with `python/scripts/phase4/p4_migrate_presets.py`.

MORPHO-PHS presets are UTF-8 JSON files with the extension `.morpho`. The plugin (VST3, Standalone and CLAP) and
`morpho_cli` read them with the same loader (`plugin/Source/PresetIO.cpp`). Factory presets live in `presets/` and are
compiled into the plugin as binary data. `morpho_cli validate <dir|file>` checks presets against this spec.

## Top level

| key | type | required | meaning |
|---|---|---|---|
| `format` | string | yes | must be `"morpho-phs-preset"` |
| `version` | int | yes | `1` or `2` (written: `2`) |
| `name` | string | yes | display name (unique within a folder; `validate` flags duplicates) |
| `category` | string | no | menu grouping, e.g. `"FX Edge"`, `"Inst Living"` (default `"Uncategorised"`) |
| `author`, `description` | string | no | free text |
| `description_zh` | string | no | Simplified Chinese description (Phase 6b; required for factory presets) |
| `tags` | array of string | no | lower-case browser tags, e.g. `["bell", "pluck"]` (Phase 6b; factory: at least 2) |
| `cpu_class` | string | no | `"eco"`, `"standard"` or `"high"`: measured CPU class (Phase 6b; required for factory presets) |
| `params` | object | no | parameter id → value (see below). Missing ids take their default values. |
| `macros` | object | no | macro names and macro → parameter mappings |
| `lattice_state` | object | no | defects, initial morphogen stimuli and an optional spiral seed |

Unknown keys are ignored. An unknown parameter id gives a warning. A value outside its range is clamped and also
gives a warning. A wrong `format`/`version`, malformed JSON, a non-numeric value or an unknown choice label is an
error, and the preset is rejected.

## Phase 6b 补充：出厂库规则（`morpho_cli validate <dir> --factory 1`）

- 排序：文件名前缀 `NNN_` 决定库内顺序（乐器 001–032，效果器 101–125），不写入 JSON。`presets/` 只扫描一层：`presets/retired/` 里的旧预设不打包。
- `cpu_class`：按 `scripts/presets/cpu6b.py` 测得的 i9 实时预算占比（p99 估计 / 20.83 µs）：eco < 0.2，standard < 0.5，high ≥ 0.5。浏览器用 1–3 个六边形显示。
- 出厂预设必须：作者、英文与中文描述非空；至少 2 个标签；`cpu_class` 合法；写出全部非宏参数；8 个宏都有名字和至少一个目标；
  宏不得指向结构性选项（lattice、threads、mode、potential、exciter、interp）或增益（in_gain、out_gain）；lo ≠ hi；每个参数只属于一个宏
  （EngineHost 中同一参数的最后一个目标生效）；Threads = Auto；32x32 不得用 HQ exact（会按 HQ fast 运行并警告）。
- 用户预设不受这些规则约束；保存用户预设时会继承当前出厂预设的类别、标签与描述。

## `params`

Values use plain units, not normalised 0–1. A choice parameter takes either its 0-based index or its label
(case-insensitive).

| id | range / choices | default | physical mapping (per control tick, 64 samples) |
|---|---|---|---|
| `mode` | `FX Edge Disperser` \| `FX Defect Freezer` \| `Instrument` | FX Edge | port layout + absorber + input routing |
| `lattice` | `8x8` \| `16x16` \| `32x32` | 16x16 | honeycomb L×L (N = 2L² sites); rebuilt **off the audio thread** and cross-faded |
| `threads` | `Auto` \| 1 \| 2 \| 3 \| 4 \| 6 \| 8 | Auto | lattice threads (Auto, Phase 4: 1 for 8/16; for 32: P-cores − 1, clamped to 2…6, e.g. 5 on an i9-13900H). Workers form a dedicated pool pinned to the P-cores (MMCSS "Pro Audio" on Windows); the host audio thread only dispatches |
| `potential` | `Quartic` \| `HQ (fast log-cosh)` \| `HQ exact` | Quartic | U(s) = α s⁴/4 (+ β·logcosh or its K=3 sqrt-sum surrogate) |
| `interp` | `1 sample` \| `8 samples` \| `Off (64)` | 1 sample (v1: 8 samples) | in-kernel staircase ramp of g and μ inside each 64-sample block |
| `tone` | 150–3000 Hz (log) | 1500 | r = tone/1500; m = 1/r² (all lattice frequencies scale with r; clamped so the CFL number γ ≤ 0.8) |
| `chirality` | −1…1 | 0.6 | g = chirality·0.6·ω₀·√m (sign = edge direction) |
| `nonlinear` | 0…1 | 0.25 | α = nonlinear·0.2·ω₀² |
| `hq` | 0…1 | 0.3 | β = hq·0.2·ω₀² (only with an HQ potential) |
| `decay` | 0…1 | 0.6 | μ₀ = −(2 + 600(1−decay)³)  (μ < 0 is damping) |
| `life` | 0…1 | 0 | μ = lerp(μ₀, +120, life)·m and η = 2·10⁻⁴·life·m: gated negative damping (self-oscillation; bounded by the energy gate) |
| `morph` | 0…1 | 0.3 | morphogen coupling: g_i·(1−2·morph·u_i), μ_i + 60·morph·m·u_i, α_i·(1+morph·u_i) |
| `tau` | 0.01–2 s (log) | 0.0833 | Barkley medium time scale |
| `freeze` | 0…1 | 0 | Freezer mode: μ, η → (1−freeze)·μ, η and the input scales by (1−freeze). At 1 the lattice is lossless. |
| `wet` | 0…1 | 0.7 | lattice (pickup) level |
| `dry` | 0…1 | 0.5 | dry / full-band path level (FX); the instrument presets use 0 |
| `in_gain`, `out_gain` | −24…24 dB | 0 | input / output gain |
| `spread` | 0.05–0.45 | 0.25 | pickup spacing (edge mode: ± spread·L along the edge) |
| `exciter` | `Bow (saw)` \| `Pluck` | Bow | instrument voice exciter (PolyBLEP saw / raised-cosine strike) |
| `defect_depth` | 0…1 | 0.6 | defect sites: m × (1 + 15·depth), g = 0 |
| `macro1` … `macro8` | 0…1 | 0.5 | macro knob positions |
| `edge_width` | 0…2 | 1 | (v2) stereo width of the lattice (wet) signal: side·w in M/S. 0 = mono, 1 = unchanged, 2 = extra wide. Dry path unaffected |
| `edge_crosstalk` | 0…1 | 0 | (v2) mixes the two edge pickups before the width stage: L' = (1−c/2)·L + (c/2)·R (and mirrored). 1 = both pickups summed |

Here ω₀ = 2π·1500 rad/s. The bond stiffness is k_c = ω₀² and the on-site stiffness is k₀ = 0.1·ω₀².

**FX Edge Disperser** has a parallel topological channel:

```
input ──► dry full-band path ───────────────────────────────────────┐
      └─► 3 × Cheby-II(6, 80 dB) in-gap band-pass ─► edge source port ─► chiral edge ─► pickups L/R ─┴─► out
```

The in-gap bands follow the lattice. At tone ratio r and HQ amount β/m, a band edge f moves to
√((r·f)² + β/m/(2π)²). The nominal gaps at r = 1 and β = 0 are 448–1104, 1500–2229 and 2400–2794 Hz.

## `macros`

```json
"macros": {
  "names":   ["Tone", "Chirality", "Edge Mix", "", "", "", "", ""],
  "targets": [ {"macro": 1, "param": "tone", "lo": 600, "hi": 2600},
               {"macro": 3, "param": "wet",  "lo": 0.2, "hi": 1.0},
               {"macro": 3, "param": "dry",  "lo": 0.8, "hi": 0.2} ]
}
```

* `macro` is 1–8 and `param` is any non-macro parameter id. A preset can have up to 32 targets, and one macro may
  drive several parameters. `hi < lo` inverts the direction.
* **Override semantics:** a mapped parameter is *replaced* by `lo + (hi − lo)·x`, where x is the macro position.
  Log parameters (`tone`, `tau`) use `lo·(hi/lo)^x` instead. Choice parameters are rounded. The parameter's own knob
  has no effect while it is mapped. This keeps automation unambiguous. **This includes `tone`** (decision D3, Phase 4):
  a macro mapped to `tone` overrides the Tone knob, exactly like any other parameter. `edge_width` and
  `edge_crosstalk` are engine parameters and can be macro targets too.
* The factory generator sets each stored `macroN` so that the macro's *first* target equals the preset's own value
  on load.
* A macro with no targets is shown dimmed in the editor.

## `lattice_state`

Coordinates are normalised to the flake's bounding box: x points right and y points up, both in [0, 1].

| key | type | meaning |
|---|---|---|
| `defects` | `[[x, y], …]` | chisel points; each marks the nearest site and its neighbours as defects |
| `morphogen` | `[[x, y, amount], …]` | initial Barkley stimuli (u += amount at the site and its neighbours) |
| `seed_spiral` | `[x, y]` | seeds a spiral wave (excited bar + refractory tail) |

These are applied through the same lock-free event queue the editor brushes use. They are re-applied automatically
after a lattice-size change.

## Example

See `presets/01_edge_shimmer.morpho`. The generator is `python/scripts/phase3/p3_make_presets.py` (writes the 27
factory presets as version 1); `python/scripts/phase4/p4_migrate_presets.py` then migrates them to version 2.

**Plugin split (Phase 4, D2).** `MORPHO-PHS` (instrument) lists only `Instrument` presets and forces `mode` =
`Instrument`; `MORPHO-PHS FX` lists only the two FX modes and maps a preset with `mode` = `Instrument` to
`FX Edge Disperser`. The file format is shared.
