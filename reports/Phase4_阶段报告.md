<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Phase 4 阶段报告：实时线程、双插件、预设 v2 与 AGPLv3

日期：2026-10-30（Asia/Shanghai）。全部工作在 box（Linux KVM 虚拟机，8 vCPU，Intel Xeon）上完成，没有在用户 PC 上运行任何东西。2026-10-31 补充：Phase 4 代码已在用户的 i9-13900H 上用 MSVC 构建并跑完完整基准（`bench_windows.ps1 -All`，Windows“最佳性能”电源模式），结果见新增的 **§10**。结论：16×16 在绑核 + MMCSS 下达标；**32×32 块 128 仍没有任何配置 PASS**（最好约 0.5% 的块超时），是尾部延迟问题而不是吞吐问题。§1–§9 保留 2026-10-30 box 阶段的原文，只在相关处加了指向 §10 的说明。

## 1. 起点：Phase 3 在 i9-13900H 上的实测（用户转述）

48 kHz，预算 20.83 µs/样本。原始目录 `experiments/results/phase3/windows/DESKTOP-114514_20261030_201314/`（写报告时不在 box 上，2026-10-31 已补回仓库），以下为当时引用的数字：

| 项目 | i9 实测 |
|---|---|
| L16 四次势 | 4–5 µs/样本，0 次超时 |
| L32 四次势最佳 | 块 64、8 线程：中位数 11.0 µs/样本，p99/预算 0.82，超时 8/2250 |
| L32 其它 | 大量超时；线程扩展非单调（4 线程常比 3 线程差） |
| 8 线程块最大值 | 40–100 ms 尖峰 |
| HQ-fast | 只在 L8 稳定；HQ 精确 L32 ≥ 34 µs |

判断：瓶颈在调度和同步（E 核、纯自旋屏障被抢占、无优先级、SMT），不在算力。

## 2. 结论摘要

| 项目 | 结果 | 状态 |
|---|---|---|
| D1 逐样本斜坡 | `interp` 默认 0（1 样本） | ✅ |
| D2 两个插件 | MORPHO-PHS（乐器，IS_SYNTH，`Mpsi`，`lab.morpho.phs.instrument`）与 MORPHO-PHS FX（效果，`Mpsx`，`lab.morpho.phs.fx`），各有 VST3 / CLAP / Standalone，一个 CMake 工程 | ✅ Linux 构建 |
| D3 宏覆盖 Tone | 保留覆盖语义，写入规范与代码注释 | ✅ |
| D4 edge_width / edge_crosstalk | 可自动化参数 + 湿声 2×2 矩阵（默认值时跳过，逐位不变）；预设格式 v2（v1 可读）；27 个预设迁移，`validate` 27 OK | ✅ |
| D5 MMCSS | 工作线程 `AvSetMmThreadCharacteristicsW("Pro Audio")`，其它系统为空操作 | ✅ 编译检查（zig mingw） |
| D6 AGPLv3 | `LICENSE` 全文、79 个源文件 SPDX 头、README、`docs/LICENSES_phase4.md` | ✅ |
| D7 | 删除 `logcosh_fit.json` 中过时 K=2 条目 | ✅（另见 §8 第 4 条） |
| 实时线程 | 拓扑感知绑核、混合屏障、专用常驻池、MMCSS、自动线程数、自适应自旋、超订阅保护、调用方阻塞等待 | ✅ Linux；✅ i9 MSVC 构建 |
| i9 复测（2026-10-31，§10） | 16×16：pin_mmcss / pin_mmcss_caller 块 128、512 全部 PASS；32×32 块 128：0/32 组 PASS，最好 p3spin 4–6 线程 7–12/1500 次超时 | ⚠️ 32×32 未达标 |
| 测试 | test_phase4（Q1–Q5）、test_mt、test_phase3、test_main 全部通过 | ✅ |
| pluginval strictness 10 | 两个 VST3 | 见 §6 |
| 32×32 块 128 判据（box） | 在这台 VM 上没有一个变体稳定 PASS（VM 抖动）；最好的几组接近 | ⚠️ 见 §5 |

## 3. 改了什么

### 3.1 实时线程（`include/morpho_phs/rt_thread.hpp`、`esav_lf_mt.hpp`）
- **拓扑**：Windows 用 `GetLogicalProcessorInformationEx(RelationProcessorCore)` 的 EfficiencyClass 区分 P/E 核；Linux 用 sysfs 拓扑 + `/sys/devices/cpu_core`、`cpu_atom`。每个物理 P 核只取一个逻辑线程（避开 SMT 兄弟）。合成的 i9 拓扑测试：P 核 CPU = {0,2,4,6,8,10}，L32 默认 5 线程（Q4b）。
- **绑核模式** `pin`：none / pcores（每个工作线程钉一个物理 P 核）/ pset（软亲和到整个 P 核集合）。Windows `SetThreadGroupAffinity`，Linux `pthread_setaffinity_np`。工作线程多于 P 核时不再把两个线程钉在同一核上（多出的改用 P 核集合）。
- **MMCSS**：`MmcssScope`（`AvSetMmThreadCharacteristicsW` + `AvSetMmThreadPriority(HIGH)`），线程退出时 `AvRevertMmThreadCharacteristics`。
- **混合屏障 HybridBarrier**：自旋（默认 50 µs）→ yield（到 200 µs）→ `std::atomic::wait`（Linux futex / Windows WaitOnAddress）。替代 Phase 3 的纯自旋。
- **专用常驻池**：可选让全部计算线程都是可绑核的池线程，宿主线程只发布任务并等待 `done_`；或“调用方计算”（宿主线程是 0 号计算线程）。
- **自动线程数**：L≤16 → 1；否则 clamp(P 核数 − 1, 2, 6)。
- **自适应自旋**：box 上 paced 运行时，工作线程在块间睡眠再唤醒使中位数慢约 40%（L32/块 128/5 线程/pcores/专用池：不 paced 7.87 vs paced 10.85 µs/样本；`--idle-spin-us 3000` 时 7.55）。现在调用方跟踪块周期 EMA，工作线程的块间自旋 = clamp(1.25 × 周期, idle_spin, 10 ms)。快测（4 线程 paced）：开 8.62，关 11.14 µs/样本。
- **超订阅保护**：专用池 T ≥ 逻辑 CPU 数时退回调用方计算。依据：box 上 8 个专用工作线程（加调用方共 9 个线程 / 8 vCPU）时 L32 为 168 µs/样本（`A_quiet_unpaced_pin.txt` 8T 行，`D8_oversub_*`）。
- **调用方阻塞等待**（`caller_pool_spin = false`，默认）：专用池时宿主线程只自旋 barrier_spin，然后阻塞，每块只多一次唤醒。依据：有 3 个外部忙循环时，调用方整块自旋会多占一个 CPU，“计算线程 + 调用方 + 负载 > CPU 数”时屏障崩溃（pin 5 线程 171 → 48.5 µs/样本，但仍全部超时；而 3–4 线程时阻塞等待反而更差：超时 7 → 20、35 → 60，见 §5.3）。这一默认值的收益不明确，列为待决（§8 第 9 条），`bench_windows.ps1 -All` 中的 `pin_mmcss_cspin` 变体用于在 i9 上比较。
- 填充的 chunk 部分和（每线程从新的缓存行开始），避免伪共享。
- 处理循环仍然零分配、无锁（Q3）；任意线程数、任意池/绑核/MMCSS 变体与 1 线程逐位一致（Q1/Q2）。

### 3.2 插件（`plugin/`）
- 一个 CMake 工程两个插件（`morpho_add_plugin()`）；`MORPHO_VARIANT_FX=0/1` 编译开关。乐器版没有模式下拉、只有输出总线、接收 MIDI；FX 版模式为 “FX Edge Disperser | FX Defect Freezer”，工厂预设按变体过滤。
- 旧插件代码 `Mphs` / `lab.morpho.phs` 退役：**用旧版本保存的 DAW 工程会找不到插件**。
- 编辑器：新增 “Edge Width”“X-talk” 旋钮，网格 6 列 × 3 行；标题显示插件名。
- 实时选项可用环境变量覆盖：`MORPHO_PHS_PIN`（none|pcores|pset，默认 pset）、`MORPHO_PHS_MMCSS`（默认 1）、`MORPHO_PHS_POOL`（dedicated|caller，默认 dedicated）。
- 版本 0.4.0。

### 3.3 预设 v2（`docs/morpho_preset_spec.md`、`presets/`）
- 新参数 `edge_width`（0–2，默认 1）、`edge_crosstalk`（0–1，默认 0）；读取 v1 和 v2，写 v2；v1 中缺少 `interp` 时按旧默认 “8 samples” 读取，以保持旧预设听感。
- 迁移脚本 `python/scripts/phase4/p4_migrate_presets.py`（幂等）：全部改为 interp “1 sample”；`07_wide_pickups` edge_width 1.5；`13_bulk_smear` crosstalk 0.35；其余 1 / 0。

### 3.4 基准 CLI 与 Windows 脚本
- `morpho_cli bench`：`--pin --mmcss --pool --threads N（0=Auto） --paced 0|1 --adaptive 0|1 --caller-spin 0|1 --load N --barrier-spin-us --idle-spin-us --tag`；输出中位数、p99、p99.9、max、超时数、PASS/NEAR/FAIL 判定；默认 paced（按块周期调用，最后 1.5 ms 自旋对齐）。
- `build_windows.ps1`：`-VsPath`（直接用该安装的 vcvars64），构建两个插件。
- `bench_windows.ps1`：变体 p3spin / baseline / pin / pin_mmcss（`-All` 加 pset_mmcss、pin_mmcss_cspin、pin_mmcss_caller），L16/L32 × 块 64/128/512 × 线程 3/4/5/6，`SUMMARY.txt` 给判据表。
- Windows 代码路径用 zig 0.17（x86_64-windows-gnu）以最终头文件交叉编译 `test_phase4.cpp` 通过（21:49 CST 重编，`zig c++ -target x86_64-windows-gnu -std=c++20 -O2 -mavx2 -mfma -Iinclude tests/test_phase4.cpp -lavrt`；只编译，box 上无法运行 .exe），导入 AVRT.dll（AvSetMmThreadCharacteristicsW 等）、GetLogicalProcessorInformationEx、SetThreadGroupAffinity。`build_windows.ps1` 与 `bench_windows.ps1` 用 PowerShell 7.4 解析器检查为 0 个语法错误（只解析，未执行），并已改为纯 ASCII，避免 Windows PowerShell 5.1 按 ANSI 读取时出错。**JUCE 插件本身没有在 Windows 上编译过 Phase 4 版本**（`windows.h` 与 JUCE 头的冲突风险未验证，已加 NOMINMAX / WIN32_LEAN_AND_MEAN）。

## 4. 测试（`experiments/results/phase4/tests/`）

| 测试 | 结果 |
|---|---|
| Q1 | f32 输出与状态，9 种池/绑核/MMCSS 变体（1–8 线程），32×32，四次势与 HQ-fast：与 1 线程逐位一致 |
| Q2 | f64 HQ 精确，专用绑核池 4 线程 + MMCSS：逐位一致 |
| Q3 | 192 000 样本、4 线程，专用池与调用方计算：0 次堆分配 |
| Q4 / Q4b | box 拓扑 8/8/8，L8/16/32 默认 1/1/6；合成 i9：P 核 {0,2,4,6,8,10}，L32 默认 5 |
| Q5 | 超订阅（8 个忙线程 + 8 个工作线程 / 8 CPU）：不死锁，200 块全部完成（每块平均 0.1–0.2 s，说明超订阅时的退化程度） |
| test_mt / test_phase3 / test_main | 全部通过 |

## 5. Linux 前后对比（box）

完整表：`experiments/results/phase4/linux_summary.md`（由 `p4_summarize_linux.py` 从 JSON 生成）。before = Phase 3 CLI（纯自旋、调用方计算、不 paced，5 s/配置）；after 每配置 4 s。变体：p3spin（新 CLI 模拟 Phase 3：自旋 1 s 才等待）、baseline（混合屏障、调用方计算、不绑核）、pin（专用池、pcores）、pset（专用池、P 核集合）。Linux 上 MMCSS 是空操作，所以 pin_mmcss = pin，没有单独跑。

**重要提醒**：box 是 KVM 虚拟机，vCPU 不是独占物理核，块最大值里有数 ms 到数十 ms 的 VM 抖动，同一配置两次运行的超时数可以相差数倍（例如 pin 4 线程块 128：不 paced 8.02 µs、0 次超时；paced 11.20 µs、6 次）。这些数字只能看趋势，不能代表 i9。

### 5.1 判据行：32×32、块 128（单元格 = µs/样本中位数 / p99÷预算 / 超时块数）

| 线程 | before（安静、不 paced） | after p3spin（安静、不 paced） | after baseline（安静、不 paced） | after pin（安静、不 paced） | after baseline（安静、paced） | after pin，调用方阻塞（安静、paced） | after pset，调用方阻塞（安静、paced） |
|---|---|---|---|---|---|---|---|
| 3 | 9.62 / 0.702 / 3 | 9.46 / 0.698 / 2 | 9.86 / 0.744 / 3 | 10.87 / 1.034 / 20 | 10.59 / 0.940 / 11 | 10.57 / 0.954 / 11 | 10.52 / 0.814 / 2 |
| 4 | 7.96 / 0.630 / 2 | 7.65 / 0.556 / 1 | 8.28 / 0.691 / 1 | **8.02 / 0.585 / 0（PASS）** | **8.85 / 0.759 / 0** | 8.87 / 0.738 / 9 | 11.13 / 3.842 / 59 |
| 5 | — | 7.12 / 0.524 / 0（PASS） | 7.07 / 0.510 / 1 | 8.53 / 0.857 / 12 | 10.10 / 0.649 / 4 | 9.71 / 0.645 / 3 | 7.87 / 0.603 / 4 |
| 6 | 6.81 / 0.559 / 2 | 6.34 / 0.470 / 1 | 5.74 / 0.415 / 5 | 6.66 / 0.596 / 3 | 6.68 / 0.494 / 1 | 7.64 / 0.796 / 7 | 7.29 / 0.762 / 8 |
| 8 | 6.60 / 2.209 / 271 | 5.27 / 0.575 / 5 | 5.34 / 2.990 / 34 | 168.3 / 11.5 / 全部（超订阅，已加保护） | 6.48 / 3.935 / 90 | — | — |

不 paced 每配置 1500 块（before 1875 块）；paced 1500 块。

### 5.2 读法
- 安静时，中位数在各变体间差别不大（4 线程约 7.7–8.9 µs/样本，6 线程约 5.7–7.6），都在预算 20.83 的一半以下；**决定成败的是尾部**（p99、max、超时数），而尾部在这台 VM 上主要由 VM 抖动决定。
- 8 线程（= vCPU 数）时：Phase 3 纯自旋 271 次超时；新混合屏障 34–90 次；专用池 8 线程完全崩溃（现已有保护）。**规律：计算线程 + 宿主线程 + 其它负载 ≥ CPU 数时，每样本一次的屏障会崩溃**，混合屏障崩得比纯自旋更“干脆”（中位数变差），纯自旋则表现为数百 ms 的尖峰。
- box 不是混合架构，也不是 Windows，绑核与 MMCSS 的主要收益（避开 E 核、避开被抢占）在这里体现不出来，所以 pin/pset 在 box 上没有比 baseline 更好。这一点只能在 i9 上验证。

### 5.3 有负载（3 个外部忙循环，块 128，32×32）

| 线程 | before（不 paced） | p3spin（不 paced） | baseline（不 paced） | baseline（paced） | pin 调用方自旋（paced） | pin 调用方阻塞（paced） |
|---|---|---|---|---|---|---|
| 3 | 9.87 / 0.712 / 0 | 9.97 / 0.819 / 0 | 13.64 / 0.817 / 2 | 10.24 / 0.798 / 1 | 10.20 / 0.903 / 7 | 10.70 / 1.138 / 20 |
| 4 | 7.92 / 0.762 / 8 | 8.57 / 0.797 / 4 | 10.91 / 0.737 / 5 | 9.20 / 0.765 / 6 | 10.40 / 1.289 / 35 | 10.10 / 1.988 / 60 |
| 5 | — | 7.23 / 0.665 / 0 | 10.37 / 3.455 / 69 | 7.67 / 3.931 / 47 | 170.5 / 18.2 / 1486 | 48.5 / 12.2 / 1351 |
| 6 | 8.66 / 37.8 / 344 | 6.71 / 3.40 / 296 | 64.45 / 6.75 / 1449 | 114.6 / 7.90 / 1426 | — | — |
| 8 | 37.14 / 223 / 1000 | 7.09 / 214 / 597 | 124.8 / 11.7 / 1500 | 130.1 / 12.5 / 1500 | — | — |

结论：只要线程数 + 负载 ≤ CPU 数（3–4 线程 + 3 个负载），各变体 p99/预算在 0.7–3.0 之间，超时数个到数十个；一旦超过，基于等待的屏障整体崩溃。这说明在用户机器上**线程数绝不能超过真正空闲的 P 核数**，Auto=5（i9 6 个 P 核）也只留 1 个 P 核给宿主和其它插件，需要在 i9 上实测 4 与 5 的差别。

## 6. pluginval（strictness 10，Linux，Xvfb，in-process）
在本轮最终代码（含超订阅保护与 `caller_pool_spin`）重新构建后运行（21:44 CST），命令：`xvfb-run -a pluginval --strictness-level 10 --validate-in-process --timeout-ms 1200000 <插件>.vst3`。

| 插件 | 结果 | 测试组 | 总线 | 预设（programs） | 延迟 / 尾长 | 输出 |
|---|---|---|---|---|---|---|
| MORPHO-PHS（乐器，VST3） | **SUCCESS**，退出码 0 | 25 组全部完成 | 0 入 / 2 出 | 12 | 0 / 8 s | `experiments/results/phase4/pluginval/pluginval_s10_MORPHO-PHS.txt` |
| MORPHO-PHS FX（效果器，VST3） | **SUCCESS**，退出码 0 | 25 组全部完成 | 2 入 / 2 出 | 15 | 0 / 8 s | `experiments/results/phase4/pluginval/pluginval_s10_MORPHO-PHS_FX.txt` |

12 + 15 = 27 个预设（v2 格式），与迁移数量一致。同一次构建也产出了两个插件的 CLAP 与 Standalone（Linux），但 CLAP 未用 clap-validator 检查，Standalone 未在音频设备上运行（box 没有声卡）。Windows 版的 pluginval 需要在 i9 上运行（命令见 §9）。

## 7. 还需要在 i9 上测的东西
> 2026-10-31：第 1、2、3 条已完成，结果见 §10（第 3 条：pin_mmcss 消除了大尖峰，块 128 最大 4.3 ms；4–6 线程比 3 线程好，扩展单调；但 32×32 仍不达标）。第 4、5 条尚未做。

1. `build_windows.ps1 -VsPath C:\SDKs\AetherBuildTools -Generator Ninja`：Phase 4 版本（两个插件）能否用 MSVC 14.44 编译（`windows.h` 与 JUCE 冲突风险）。
2. `bench_windows.ps1`（建议加 `-All`）：p3spin / baseline / pin / pin_mmcss / pset_mmcss / pin_mmcss_cspin / pin_mmcss_caller，L16/L32 × 块 64/128/512 × 线程 3/4/5/6；判据 32×32、块 128：0 超时且 p99/预算 < 0.7。
3. 重点看：pin_mmcss 是否消除 40–100 ms 尖峰；4 与 5 线程哪个更稳；线程扩展是否恢复单调。
4. 在 DAW 中实测两个插件（乐器版是否出现在乐器列表、多实例、不同缓冲区大小）。
5. 可选：Windows 上的 pluginval（命令见 `scripts/windows/README_WINDOWS.md`）。

## 8. Phase 5 待决事项（本阶段不开始 Phase 5）
1. **自适应自旋 vs CPU 占用**：默认开（宿主持续调用时工作核保持忙等，任务管理器里看起来满核）。是否保留？10 ms 上限是否合适？
2. **默认绑核**：插件默认 pset（软亲和，多实例安全）；pcores（硬）在单实例时可能更稳。i9 数据（§10）：pset_mmcss 在 32×32、4–6 线程为 95–188 次超时，pin_mmcss 为 27–60 次；16×16 上 pcores + MMCSS 全部达标而 pset 只有 6/12。倾向改为 pcores + MMCSS，但要先验证多实例。
3. **授权**：AGPL-3.0-only 还是 or-later；是否购买 JUCE 商业许可以闭源发布。
4. **logcosh_fit.json**：删除 K=2 后，剩余 K=3/K=4 条目与代码实际常数（`core_types.hpp`、`logcosh_surrogate_eval.json`）不一致：重新生成还是删除？
5. **旧插件 ID `Mphs` 退役**：是否需要兼容（旧工程仍可打开）？
6. **线程选项列表**是 Auto|1|2|3|4|6|8，没有 5（Auto 在 i9 上选 5）：是否改为 1–8 全选？
7. **每样本一次屏障的结构性限制**：i9 上 pin_mmcss 确实仍不达标（§10），是否投入“每 k 样本一次同步”的时空分块或降采样子晶格（会影响能量证明的形式，需要重新论证）？
8. HQ-fast 在 i9 上只有 L8 稳定：是否把 HQ 限制在 ≤ 16×16？
9. **专用池时宿主线程等待方式**：阻塞（当前默认，少占一个 CPU，超订阅时好得多）还是自旋（box 上有负载、3–4 线程时超时更少）？以 i9 上 `pin_mmcss` 与 `pin_mmcss_cspin` 的对比为准。i9 结果：差别很小（5/6 线程 27/28 vs 23/20 次超时，3–4 线程时自旋略差），倾向保留阻塞。
10. **新增（i9 复测后）**：补测“绑核 + MMCSS + 纯自旋屏障”和排除 CPU 0 的 P 核列表；基准脚本记录电源模式覆盖与实际频率；32×32 是否降级为“高负载 / 大缓冲”选项。

## 9. 文件清单（相对上次同步的新增/修改）
见同步包中的 `PHASE4_SYNC_MANIFEST.txt`。主要新增：`LICENSE`、`include/morpho_phs/rt_thread.hpp`、`tests/test_phase4.cpp`、`docs/LICENSES_phase4.md`、`python/scripts/phase4/*`、`experiments/results/phase4/*`、本报告。2026-10-31 的 i9 增量同步只包含 `README.md`、本报告（.md/.html/.pdf）与 `reports/phase4_progress.md`；i9 原始数据与日志已在用户 PC 上。主要修改：`esav_lf_mt.hpp`、`plugin/CMakeLists.txt`、`plugin/Source/**`、`presets/*.morpho`、`docs/morpho_preset_spec.md`、`scripts/windows/*`、`README.md`、`experiments/results/phase3/logcosh_fit.json`，以及 79 个源文件的 SPDX 头。

## 10. i9-13900H 复测结果（2026-10-31，Asia/Shanghai）

**运行条件**

- 权威数据：`experiments/results/phase4/windows/DESKTOP-114514_20261031_112539/`（`SUMMARY.txt`、各变体 `*.txt` / `*.json`、`sysinfo.json`），控制台日志 `logs/bench_phase4_console_bestperf.txt`。
- 构建：Phase 4 代码，用户机器 MSVC 14.44（`build_windows.ps1 -VsPath C:\SDKs\AetherBuildTools -Generator Ninja`），2026-10-30 21:52 构建成功（两个插件 VST3/CLAP/Standalone + `morpho_cli.exe`，日志 `logs/build_phase4_console.txt`）。
- 电源：Windows 11 电源模式“最佳性能”（覆盖方案 GUID ded574b5…），底层电源计划仍是“平衡”（381b4222…）。`sysinfo.json` 只记录了底层计划，**没有记录电源模式覆盖**；“最佳性能”来自用户确认，以及运行前在 PC 上读取到的电源模式覆盖 GUID（ded574b5…）；这一信息不在结果文件里。`max_clock_mhz=2600` 是 WMI 报告的标称频率，不是实际睿频。
- 矩阵：`bench_windows.ps1 -All`，paced（按宿主块周期调用），每配置 4 s，L16/L32 × 块 64/128/512 × 线程 3/4/5/6；变体 baseline、p3spin、pin、pin_mmcss、pin_mmcss_caller、pin_mmcss_cspin、pset_mmcss，以及乐器模式 inst_L32_pin_mmcss（L32、块 128）。共 32 组判据行 + 全矩阵，全部有数据。
- 排除的运行：`DESKTOP-114514_20261030_215301/`（“平衡”模式，PC 关机中断，只有 baseline、p3spin）仅用于电源模式对比；`DESKTOP-114514_20261031_111728_mixed_power_aborted/` 运行中途切换了电源模式，**整组排除，不引用**。
- 判据：32×32、块 128（预算 2666.7 µs/块 = 20.83 µs/样本）：0 次超时且 p99/预算 < 0.7 为 PASS；超时 ≤ 0.1% 且 p99/预算 < 0.7 为 NEAR。

**判据表：32×32、块 128（1500 块）**

| 变体 | 线程 | 中位数 µs/样本 | p99 µs | max µs | p99/预算 | 超时块 | 判定 | 线程配置（rt_report 摘要） |
|---|---|---|---|---|---|---|---|---|
| p3spin | 3 | 11.39 | 2,768 | 3,293 | 1.038 | 31 | FAIL | 不绑核，无 MMCSS，调用方 + 2，纯自旋屏障 |
| p3spin | 4 | 11.25 | 2,555 | 3,287 | **0.958** | **8** | FAIL | 调用方 + 3 |
| p3spin | 5 | 14.14 | 2,646 | 3,385 | 0.992 | 12 | FAIL | 调用方 + 4 |
| p3spin | 6 | 13.47 | 2,594 | 3,311 | 0.973 | **7** | FAIL | 调用方 + 5 |
| baseline | 3 | 15.00 | 4,383 | 6,866 | 1.644 | 426 | FAIL | 不绑核，无 MMCSS，混合屏障，调用方 + 2 |
| baseline | 4 | 17.61 | 4,099 | 6,429 | 1.537 | 308 | FAIL | 调用方 + 3 |
| baseline | 5 | 21.70 | 11,723 | 46,867 | 4.396 | 832 | FAIL | 调用方 + 4 |
| baseline | 6 | 14.07 | 3,762 | 4,922 | 1.411 | 92 | FAIL | 调用方 + 5 |
| pin | 3 | 12.94 | 2,451 | 3,439 | 0.919 | 9 | FAIL | 专用池，pcores [10,8,6]，无 MMCSS |
| pin | 4 | 11.23 | 3,425 | 88,858 | 1.284 | 43 | FAIL | [10,8,6,4] |
| pin | 5 | 10.03 | 3,802 | 6,457 | 1.426 | 73 | FAIL | [10,8,6,4,2] |
| pin | 6 | 13.17 | 6,547 | 163,259 | 2.455 | 376 | FAIL | [10,8,6,4,2,0]（含 CPU 0） |
| pin_mmcss | 3 | 20.25 | 3,305 | 4,317 | 1.239 | 581 | FAIL | 专用池，pcores [10,8,6]，MMCSS 3/3 成功 |
| pin_mmcss | 4 | 17.03 | 2,970 | 4,215 | 1.114 | 60 | FAIL | [10,8,6,4] |
| pin_mmcss | 5 | 15.37 | 2,873 | 3,870 | 1.077 | 27 | FAIL | [10,8,6,4,2] |
| pin_mmcss | 6 | 13.91 | 2,930 | 4,341 | 1.099 | 28 | FAIL | [10,8,6,4,2,0]（含 CPU 0） |
| pin_mmcss_caller | 3 | 19.28 | 3,372 | 4,193 | 1.264 | 363 | FAIL | 宿主线程参与计算，其余钉 [10,8] |
| pin_mmcss_caller | 4 | 16.68 | 3,016 | 3,522 | 1.131 | 87 | FAIL | [10,8,6] |
| pin_mmcss_caller | 5 | 14.57 | 2,916 | 3,818 | 1.094 | 34 | FAIL | [10,8,6,4] |
| pin_mmcss_caller | 6 | 13.46 | 2,816 | 3,901 | 1.056 | 26 | FAIL | [10,8,6,4,2] |
| pin_mmcss_cspin | 3 | 20.39 | 3,437 | 4,210 | 1.289 | 582 | FAIL | pin_mmcss + 宿主线程为池自旋 |
| pin_mmcss_cspin | 4 | 17.27 | 3,042 | 4,226 | 1.141 | 81 | FAIL | [10,8,6,4] |
| pin_mmcss_cspin | 5 | 15.17 | 2,820 | 3,867 | 1.057 | 23 | FAIL | [10,8,6,4,2] |
| pin_mmcss_cspin | 6 | 13.87 | 2,747 | 3,566 | 1.030 | 20 | FAIL | [10,8,6,4,2,0]（含 CPU 0） |
| pset_mmcss（插件默认） | 3 | 13.10 | 3,277 | 3,930 | 1.229 | 208 | FAIL | 软亲和到 12 个 P 核逻辑 CPU，MMCSS |
| pset_mmcss | 4 | 17.59 | 3,422 | 4,513 | 1.283 | 141 | FAIL | |
| pset_mmcss | 5 | 16.57 | 3,930 | 5,472 | 1.474 | 188 | FAIL | |
| pset_mmcss | 6 | 15.11 | 3,853 | 5,518 | 1.445 | 95 | FAIL | |
| inst_L32_pin_mmcss（乐器） | 3 | 25.72 | 4,245 | 4,573 | 1.592 | 1112 | FAIL | 同 pin_mmcss |
| inst_L32_pin_mmcss | 4 | 20.21 | 3,606 | 6,844 | 1.352 | 589 | FAIL | |
| inst_L32_pin_mmcss | 5 | 18.19 | 3,256 | 4,038 | 1.221 | 205 | FAIL | |
| inst_L32_pin_mmcss | 6 | 16.50 | 3,179 | 4,136 | 1.192 | 84 | FAIL | |

注：P 核逻辑 CPU 由 `GetLogicalProcessorInformationEx` 得到（6 个 P 核 / 14 个物理核，每个 P 核取一个逻辑线程：10,8,6,4,2,0）。

**32×32 其它块大小（摘要）**

- 块 64（1333 µs 预算）：所有变体都 FAIL；最好的是 p3spin（45–64/3000 超时，p99/预算 1.04–1.10）。
- 块 512（10.67 ms 预算）：pin_mmcss 5/6 线程、pin_mmcss_caller 4/5/6 线程、p3spin 4/5/6 线程、pset_mmcss 4 线程为 **0 超时**，但 p99/预算 0.79–0.98，超过 0.7 的余量要求，所以仍记为 FAIL。最好：p3spin 6 线程 0/375、p99/预算 0.787；pin_mmcss_caller 6 线程 0/375、0.844。

**16×16 结果（中位数 µs/样本 / p99÷预算 / 超时块）**

| 变体 | 块 64（3000 块） | 块 128（1500 块） | 块 512（375 块） | PASS 数 / 12 |
|---|---|---|---|---|
| p3spin | 3T–6T 全 PASS（3.1–3.8 / 0.40–0.45 / 0） | 3/5/6T PASS，4T NEAR（1 次） | 全 PASS（0.25–0.28） | 11 |
| pin_mmcss | 3/4T PASS，5/6T NEAR（3、2 次） | **3T–6T 全 PASS**（5.8–6.4 / 0.51–0.68 / 0） | **全 PASS**（3.5–4.4 / 0.30–0.32 / 0） | 10 |
| pin_mmcss_caller | 3T FAIL（22 次），4–6T PASS | **全 PASS**（5.0–5.8 / 0.44–0.53 / 0） | **全 PASS**（0.26–0.31） | 11 |
| pin_mmcss_cspin | 4T NEAR、6T NEAR，3/5T FAIL | 4/5T PASS，3T NEAR，6T FAIL（2 次） | 3/4/6T PASS，5T FAIL（1 次） | 5 |
| pset_mmcss | 3T PASS，4–6T NEAR | 5T PASS，其余 FAIL（p99/预算 0.70–0.74 或 2 次超时） | 全 PASS | 6 |
| baseline | 3/4T PASS，5T NEAR，6T FAIL | 3T PASS，4T NEAR，5/6T FAIL | 3/5/6T PASS，4T FAIL（1 次，11 ms 尖峰） | 6 |
| pin（无 MMCSS） | 全 FAIL（39–329 次，尖峰 13–54 ms） | 全 FAIL（4–149 次，尖峰最大 55 ms） | 3T PASS，其余 FAIL（尖峰 12–56 ms） | 1 |

**电源模式的影响（只比较两次都有的 baseline / p3spin，32×32、块 128）**

| 变体 | 线程 | “平衡”（20261030_215301） | “最佳性能”（20261031_112539） |
|---|---|---|---|
| p3spin | 3 / 4 / 5 / 6 | 20.3 / 19.7 / 16.7 / 16.1 µs；664 / 585 / 288 / 140 次超时 | 11.4 / 11.2 / 14.1 / 13.5 µs；31 / 8 / 12 / 7 次 |
| baseline | 3 / 4 / 5 / 6 | 20.8 / 18.2 / 18.2 / 17.0 µs；744 / 423 / 328 / 101 次 | 15.0 / 17.6 / 21.7 / 14.1 µs；426 / 308 / 832 / 92 次 |

“最佳性能”使纯自旋的中位数下降约 15–45%、超时减少 20–70 倍；混合屏障收益小得多，5 线程甚至更差（单次测量，46.9 ms 尖峰）。Phase 3 的 i9 数据（“平衡”、不 paced、每配置 3 s、FX 四次势、块 128）最好是 2 线程 14/1125 次超时，其余 93–484 次；条件不同，只能看量级。

**关键结论**

1. **32×32 仍不能可靠实时**：块 128 没有任何一组 PASS（也没有 NEAR）。最好的是 Phase 3 式纯自旋 p3spin 4–6 线程：7–12/1500 次超时（0.5–0.8%），p99/预算 0.958–0.992，max 3.3–3.4 ms。中位数 11–14 µs/样本，低于 20.83 µs 预算，所以这是**尾部延迟问题，不是吞吐问题**：p99 块耗时约为中位数块耗时的 1.5–1.9 倍（例如 p3spin 4T：中位数 1440 µs，p99 2555 µs）。
2. **只绑核、不开 MMCSS（pin）最差**：L32 出现 89 ms（4T）、163 ms（6T 块 128）、183 ms（6T 块 64）、178 ms（6T 块 512）尖峰；L16 也有 13–56 ms 尖峰，12 组只有 1 组 PASS。6 线程时 CPU 集合包含逻辑 CPU 0（[10,8,6,4,2,0]），6T 最差；但 **3–5 线程不含 CPU 0 也有 13–89 ms 尖峰**，所以 CPU 0 不能单独解释。硬绑核后线程不能迁移，一旦所在核被同级或更高优先级的线程（或 DPC/ISR）占用，就只能等待。
3. **绑核 + MMCSS 让 16×16 完全达标**：pin_mmcss 与 pin_mmcss_caller 在块 128 和 512 上 3–6 线程全部 0 超时，p99/预算 ≤ 0.68，max ≤ 2.5 ms（块 128）。MMCSS 把 pin 的 50–180 ms 尖峰压到 L32 最多 13 ms（块 512）、块 128 最多 4.3 ms。
4. **但在 32×32 上，绑核 + MMCSS 不如不绑核的纯自旋**：pin_mmcss 最好 27–28 次（5/6T）、pin_mmcss_cspin 最好 20 次（6T）、pin_mmcss_caller 最好 26 次（6T）；3 线程时中位数 19–20 µs、几百次超时。这一族变体都是线程越多越好：3 线程时每个线程的计算量太大，块耗时中位数已接近预算。
5. **pset（软亲和到 P 核集合，当前插件默认）不比 pcores 好**：32×32 块 128、4–6 线程为 95–188 次超时，比 pin_mmcss 的 27–60 次差（只有 3 线程时 pset 的 208 次好于 pin_mmcss 的 581 次）；16×16 也只有 6/12 PASS。插件默认值需要重新决定（§8 第 2 条）。
6. **乐器模式更重**：inst_L32_pin_mmcss 中位数 16.5–25.7 µs，84–1112 次超时，比 FX 模式的 pin_mmcss 差一截。
7. **宿主线程等待方式**（§8 第 9 条）：pin_mmcss_cspin（宿主自旋）与 pin_mmcss（宿主阻塞）在 i9 上差别很小（5T 23 vs 27、6T 20 vs 28 次），3–4 线程时自旋略差。阻塞可以保留为默认。
8. **一个混淆因素（新发现）**：p3spin 是唯一使用纯自旋屏障的变体（`barrier_spin_us=1e6`）；其余 Phase 4 变体都是混合屏障（自旋 50 µs 后转 `WaitOnAddress`）。`*.json` 的 `barrier_os_waits` 显示，32×32 块 128 时混合屏障每次测量有 219–12,395 次 OS 等待（p3spin 为 0），每次唤醒都有调度延迟。所以“绑核 + MMCSS 不如 p3spin”可能有一部分来自屏障类型，而不是绑核本身。**“绑核 + MMCSS + 纯自旋屏障”这个组合还没测过**，是最直接的下一个实验。

**原因假设（尚未逐一验证）**

- **CPU 0 / 中断负载**：Windows 把大量 DPC/ISR 放在 CPU 0；6 线程的 pcores 集合包含 CPU 0。可用 xperf/WPA 或 LatencyMon 查看各核的 DPC/ISR 时间来验证；或把 CPU 0 从 P 核列表中排除再测。
- **每样本一次屏障**：块 128 = 每块 128 次全线程同步；每个样本的墙钟时间约 11–14 µs，屏障之间每个线程只有几 µs 计算（i9 单线程约 28 µs/样本，按线程数平分）。任一线程晚到几十 µs，整块就被拖延；超时是“偶发长尖峰叠加”的结果。
- **Windows 对硬绑核线程的调度**：硬亲和让线程无法迁移，所在核被占用时只能等；MMCSS 提高优先级后好很多，但 MMCSS 的调度器会在“Pro Audio”任务上做占空比限制（可能造成周期性停顿），对此尚无数据。
- **混合架构的频率与功耗状态**：“平衡”与“最佳性能”的巨大差异说明频率、C 状态或 Thread Director 有很大影响；E 核负载、P 核睿频降档、核间频率不一致都可能把最慢的线程变得更慢。本次没有记录实际频率和温度。
