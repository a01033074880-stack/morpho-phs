<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Phase 6b 旧预设审计（27 个 → 57 个新库）

证据：`morpho_cli qa presets/retired /workspace/p6b/old27 --play 8 --tail 8` + `scripts/presets/qa_analyze.py`（与新库同一套 QA 门限：乐器 −18 LUFS-I、效果器当时按 −18 测，现行策略为 −20 LUFS-I ±2；真峰值 ≤ −1 dBTP；限幅器 0 次；无失控；余音 ≤ 12 s）。
旧 27 个中 **24 个未通过**；`validate --factory 1` 下 27/27 不满足出厂元数据规则（缺 `description_zh` / `tags` / `cpu_class`，宏未全部命名或分配）。旧文件保存在 `presets/retired/`（不被 BinaryData 打包，CLI 也不递归扫描）。

## 发现的系统性问题

1. **边缘色散效果器（01–08、13、25、27）整体偏轻约 6–8 LU**（−25.6 ~ −27.7 LUFS）。原因是物理性的：Edge 模式的湿声只有能隙内的窄带（tone 1500 时约 0.8 / 1.9 / 2.6 kHz），尾音约 0.1 s，峰值由干声鼓点决定。新库改为“湿声在前”的混合（wet 0.8 / dry ≈0.43），并按 −20 LUFS-I 拟合输出增益。
2. **超临界 Life 预设（22、23、24、26）永不结束**：Life 超过 Hopf 阈值 life* = −mu0/(120−mu0)（mu0 = −(2 + 600·(1−decay)³)）后晶格自激，能量门把它锁在 E_cap，松手后仍持续。螺旋波种子与部分形态素碰撞在 Barkley 介质里永久存在，若再加 Life 就会永久自持。新规则：有螺旋波种子 ⇒ Life = 0（生成器 `bank6b.py` 断言）；其它 Life 预设取阈值的 0.5–0.75 倍，Life 宏的上端才越过阈值。
3. **冻结器（09、10、12）默认 freeze 太高**，余音不结束。新库默认部分冻结（freeze 0.5–0.6）、有限余音；Freeze 宏推到 1 才无限保持。
4. **限幅器被触发**（15、16、17、19、21、24）：峰值过高，0.9 以上的软限幅器参与，真峰值接近 0 dBTP。新库全部 0 次限幅，真峰值 ≤ −1.1 dBTP。
5. **15 Glass Harmonica 能量失控**（energy_end > 1.1·E_cap，且末 2 s 仍增长）。新版默认 Life = 0（Life 宏可推入自持区），改 HQ fast，QA 通过。
6. 宏：旧预设多数只分配了 3–5 个宏。新库 8 个宏全部命名、全部有目标，且每个参数只属于一个宏（`factory_checks` 报错规则）。

## 逐个决定

| 旧 # | 名称 | 旧 QA（LUFS-I / 真峰值 / 问题） | 决定 | 新预设 | 理由 |
|---|---|---|---|---|---|
| 01 | Edge Shimmer | −26.0 / −5.7 / 偏轻 | 重做（合并 02） | 101 Edge Shimmer | 湿声在前；Chirality 宏覆盖“反向”版本 |
| 02 | Reverse Edge | −26.0 / −5.7 / 偏轻 | **退役** | — | 与 01 的指标完全一致，仅手性符号相反 = 101 的 Chirality 宏 |
| 03 | Low Edge Rumble | −27.7 / −9.2 / 偏轻 | 重做 | 102 Low Edge Rumble | 重新拟合增益与混合 |
| 04 | Glass Corridor | −26.6 / −7.0 / 偏轻 | 保留（重配增益） | 103 Glass Corridor | 声音成立，仅响度与宏 |
| 05 | Breathing Edge | −26.3 / −8.2 / 偏轻 | 重做 | 122 Breathing Edge | 移到 Morph Evolutions |
| 06 | Grit Edge | −25.6 / −7.0 / 偏轻 | 重做 | 104 Grit Edge | — |
| 07 | Wide Pickups | −27.4 / −8.4 / 偏轻 | **退役** | — | 与 01 只差 spread；Width 宏已覆盖 |
| 08 | Pinhole Edge | −26.2 / −7.8 / 偏轻 | 重做 | 105 Pinhole Edge | — |
| 09 | Freeze Halo | −18.2 / −5.4 / 余音不结束 | 重做 | 108 Freeze Halo | 默认有限余音，Freeze 宏到 1 才保持 |
| 10 | Crystal Cage | −18.5 / −6.3 / 余音不结束 | 重做 | 109 Crystal Cage | freeze 0.5、decay 0.6 |
| 11 | Soft Hold | −16.1 / −3.0 / 通过 | 保留（重配增益） | 110 Soft Hold | 改为 −20 LUFS-I 策略 |
| 12 | Shattered Ice | −17.0 / −5.3 / 余音不结束 | 重做 | 111 Shattered Ice | HQ fast，部分冻结 |
| 13 | Bulk Smear | −26.2 / −7.8 / 偏轻 | 重做 | 114 Bulk Smear | 移到 Lattice Spaces |
| 14 | Chiral Cello | −18.7 / −5.2 / 通过 | 保留（重配增益） | 014 Chiral Cello | — |
| 15 | Glass Harmonica | −11.8 / 0.0 / 失控 + 限幅 27 | 重做 | 017 Glass Harmonica | 默认 Life 0，HQ fast |
| 16 | Twisted Choir | −14.1 / −0.9 / 限幅 | 重做 | 028 Halo Choir | Life 取阈值 0.5 倍（原 0.65 在 5.6 s 后出现宽带噪声底） |
| 17 | Rusty Bow | −15.5 / −0.1 / 限幅 64 | 重做 | 015 Rusty Bow | — |
| 18 | Honeycomb Harp | −21.5 / −7.1 / 偏轻 | 重做 | 006 Honeycomb Harp | — |
| 19 | Defect Kalimba | −19.4 / −0.0 / 限幅 244 | 重做 | 010 Defect Kalimba | decay 0.7；峰值受限类 |
| 20 | Metal Lattice | −19.8 / −3.6 / 通过 | 重做 | 001 Gyro Bell | 强陀螺耦合的钟声更有辨识度 |
| 21 | Low Mallets | −23.8 / 0.0 / 限幅 186 | 重做 | 011 Low Mallets | decay 0.64；峰值受限类 |
| 22 | Spiral Drone | −17.8 / −4.0 / 不结束 | 重做 | 026 Spiral Drone | 螺旋波 ⇒ Life 0 |
| 23 | Ember Field | −18.4 / −3.8 / 不结束 | 重做 | 022 Ember Field | morph 0.45，单个形态素源 |
| 24 | Restless Swarm | −13.5 / −0.0 / 不结束 + 限幅 | 重做 | 025 Nerve Net | 亚临界 Life，HQ fast |
| 25 | Big Edge 32 | −24.8 / −6.3 / 偏轻 | 重做 | 107 Long Edge 32 | 32x32 只用 Quartic，Auto 线程 |
| 26 | Big Spiral 32 | −17.9 / −7.3 / 不结束 | 重做 | 029 Big Spiral | 亚临界 Life（=0），HQ fast；6b2 起改为 16×16（i9 实测 32×32 版 p99/预算 1.226） |
| 27 | Tiny Edge 8 | −25.6 / −6.2 / 偏轻 | 重做 | 106 Tiny Edge 8 | — |

统计：保留 3（04、11、14）、重做 22、退役 2（02、07）；其余 32 个为 Phase 6b 新设计（共 57 个）。

## 新库的设计规则（由生成器断言 / factory_checks 检查）

- 8 个宏全部命名并分配；每个参数只被一个宏控制；宏位置由预设自身的设计值反推（宏目标会覆盖参数）。
- 宏不得指向结构性参数（lattice、threads、mode、potential、exciter、interp）或增益参数。
- 32x32 只用 Quartic 或 HQ fast，线程 Auto（HQ exact 在 32x32 会退回 HQ fast 并警告）。
- 螺旋波种子 ⇒ Life = 0。
- 必须带 `description_zh`、`tags`、测得的 `cpu_class`；文件名前缀 `NNN_` 决定库内顺序（乐器 001–032，效果器 101–125）。
