<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Phase 5 进度日志（Asia/Shanghai）

## 2026-10-31

- 读取专家报告（`docs/expert_review_phase4.md`），列出可实测核验的说法；约束：只在 box 上工作，不碰用户 PC 与其它项目，文档用简体中文。
- **基准规范化**：重写 `scripts/windows/bench_windows.ps1`（冷却、种子随机顺序、重复、电源 / 频率记录、1T 行、`-Variants`）；CLI 加 `clkGHz`、`bOSw`/`iOSw`（只统计测量窗口）、`--kernel/--ersav/--inblock-os-wait/--ecoqos/--caller-cpuset/--caller-mmcss/--variant`。在 Linux 上用 pwsh 7 做语法检查（0 错误）与干跑（WMI/powercfg 打桩）。
- **线程协议**：`SpinBarrier` 纯自旋（单次等待 > 1 ms 才 yield，按次计数）；干净 P 核；CPU Sets；EcoQoS 关闭；调用方计算默认 3 线程。超订阅对比：Phase 4 混合屏障 203 ms/块（最大 592 ms）vs 纯自旋 + 自救 1.6–2.1 ms/块。
  - 问题：替换 rt_thread 时 `s.index('};')` 匹配到了 `count_{0};`，残留 4 行导致编译失败 → 删除。
  - 问题：自救计数原来每次 yield 计一次（上千）→ 改为每次等待计一次，CLI 只报测量窗口。
- **模板内核**：二分交错布局 + 3 个固定偏移邻居 + `sc_[9]` 系数；与 CSR 逐位一致（S1 112 组、S1b 1 s 长跑）。GCC/Clang 加 `-ffp-contract=off`。box 1T 21.05 → 5.21 µs/样本。
- **E-RSAV**：C++ 钳位版（默认关）；Python 钳位 / 预算两种；`p5_ersav_alias.py`：钳位 −1.1～+2.4 dB，预算中电平 +17.9 dB、高电平 −1.3 dB。默认保持关。
  - 问题：不一致度按元素归一化在 U≈0 时爆掉 → 改为与 C++ 相同的求和归一化；加 `passive_rise_max_rel`。
- **k 步分块**：门控逐样本全局耦合 → 不做，写入文档。
- **乐器模式开销**：gprof（`build/prof`，不进同步包）：`energy()` 20268 次 / `reset_psi()` 4894 次，MPE `render()` 0.01 s。向量化 `energy_stencil()`、`reset_psi_simd<POT>()`，逐位一致（S9 360/360）。box 1T 乐器 6.28 → 5.51 µs/样本。
- S4 在 box 上偶尔出现专用池 2 次 OS 等待（4 次运行中 1 次），是 VM 抢占（单次等待 > 1 ms）；S4 改为每种模式最多 2 次尝试，全部记录。
- 发现 Phase 5 的 CSR 比 Phase 4 慢约 20%：同一程序 contract=off 24.6 vs contract=fast 20.8 µs/样本，原因是关闭 FMA 收缩；MSVC 不受影响。
- 全部测试通过（test_main / test_mt / test_phase3 / test_phase4 / test_phase5）；插件构建 BUILD_EXIT 0；pluginval strictness 10 两个 VST3 SUCCESS；zig mingw 交叉编译 test_phase5.exe / test_phase4.exe 通过。
- box 基准：`experiments/results/phase5/box/`（1T 新旧对比、CSR 对照、多线程 paced / 不 paced × FX / 乐器）。
- 文档：README（状态、§3.6、§5.4、§8.3、§9.0、§10、附录）、`README_WINDOWS.md`、本报告；同步包。

## 2026-10-31（Phase 5b，14:47 起）

- 收到 i9 实测（`DESKTOP-114514_20261031_141513`，14:15–14:39）：1T 4.67 µs/样本 PASS（CSR 26.6，5.7 倍）；p5_caller_cpuset 2/3/4T PASS（p99/预算 0.30–0.31）；p4_default FAIL；p5_dedicated_cpuset 第 2 轮 3T 后卡死 16 分钟，脚本在第 11/27 次运行中止。数据解包到 `experiments/results/phase5/windows/`，写入 README §6.5。
- 再次确认只有本代理在编辑工作区（用时间戳文件比对新改动，全部是自己的）。
- **卡死根因**：专用池调用方 `while (done_.load()!=s) hybrid_wait(done_, done_.load(), …)` 读两次；t0 在两次读之间发布 → 等 “≠ s” 永远等不到，全员睡眠。自 Phase 4 就有，Phase 5 的 MMCSS 工作线程抢占调用方把窗口撑开（推测）。修复：只读一次并用读到的值做比较值；工作线程 `seen` 从 0 开始。审计调用方计算池：无此竞态。
  - 问题：普通压力测试复现不了（窗口纳秒级）→ 加 `MORPHO_PHS_RACE_HOOK` 插桩点（默认空），注入睡眠后旧代码 0 块 / 170 块卡死。
  - 问题：第一次用旧代码复现时 `-Iinclude` 排在 `-I/tmp/old2` 前面，实际编进了新头文件 → 调整包含顺序。
- 新增 `tests/test_phase5_stress.cpp`（P1–P4 + 看门狗，`-DSTRESS_HOOKS` 插桩版），3 个种子全部通过；加入 CMake 目标 `morpho_stress` 与 `build_windows.ps1`。
- `bench_windows.ps1`：单次超时（3 倍预期）、TIMEOUT 行、失败不中止、`-Resume`、新变体 hqf_L32 / hq_L32 / L16 / L16_hq；CLI 每行重写 JSON。pwsh 干跑用故意卡死 / 崩溃的假 CLI 验证。
  - 问题：`-Resume` 的相对路径在 `Set-Location $Root` 之后解析失败 → 先按调用目录、再按项目根解析。
  - 问题：zig 交叉编译需要 `-mcpu=x86_64_v3`（不是 `-march`）和 `-lavrt`（MSVC 靠 `#pragma comment(lib)`）。
- 默认值：L32 Auto 3 → 2；测试期望同步修改。
- box HQ / L16 参考（`experiments/results/phase5/box_hq/`）：HQ-exact L32 在 3T 以内 FAIL；HQ-fast 2T NEAR；L16 1T 四次势与 HQ-exact 都 PASS。
- 测试全部通过（test_phase5 第一次因与干跑同时运行出现 S4 自救 2–4 次，空闲后重跑 PASS）；插件重建 BUILD_EXIT 0；pluginval strictness 10 两个 VST3 SUCCESS；zig 交叉编译 4 个 exe 通过。
- 文档：README（状态、§3.6、§5.5、§6.5、§8.1、§9.0、§10、附录）、`README_WINDOWS.md`、本报告 §11；同步包 `morpho-phs-phase5b-sync.zip`。

## 2026-10-31（Phase 5c，15:48 起）

- 收到 i9 最终数据 `p5_i9_final.zip`（`-Resume` 补测 15:18–15:43 + 第 1 轮）。zip 内路径用反斜杠，手动映射到 `experiments/results/phase5/windows/`；顶层与 `windows\logs\` 下的日志重复，合并到 `windows/logs/`。开工时建立时间戳文件，确认只有本代理在编辑工作区。
- 分析（`experiments/results/phase5/windows/i9_final_analysis.md`）：FX 2T 0/11250（0.310）、3T 0/11250（0.408）、4T 2 次；乐器 3T 0/11250（0.480），2T 只有 1 轮（0.626）；HQ-fast 3T 1 次（0.757），2T 31 次；HQ-exact L32 全部 FAIL（24–58 µs/样本）；L16 HQ-exact 2T 12 次；专用池 22–60 次、无卡死；Phase 4 协议 621–1830 次。i9 压力测试 P1/P2/P4 PASS，P3 256 次启停（约 58.6 ms/次）无卡死但按旧判据 FAIL。
- PL1 检验：clkGHz 随线程数下降（1T 4.65 → 4T 3.88 GHz）从第一分钟就存在；会话内无下降趋势（1–2T Spearman +0.1）；同配置内频率偏差与 p99 / 超时 ρ ≈ −0.1；超时最多的行在全速。结论：不支持；无功耗遥测，不能完全排除。
- 默认值：`default_threads(L, topo, AutoLoad)`（L32 FX 2 / 乐器 3 / HQ 3；L16 HQ-exact 2）；HQ-exact 限 L ≤ 16（有效势回退 + 界面 / 预设提示 + CLI `--hq-exact-any-L`）；专用池不变。
  - 问题：bench 的 hq_L32 变体改动后会悄悄测成 HQ-fast → 加 `--hq-exact-any-L 1`（旧 CLI 会忽略未知选项）。
  - 决定：不禁用 HQ-exact 选项，而是回退，避免已保存工程 / 自动化在切换晶格尺寸后出现无效值。
- 压力测试 P3：第一版判据“≥ 秒数 / 中位数的一半”放弃（启停成本按线程数多峰，中位数会误判 i9）→ 改为“≥ min(100, 10 × 秒数) + 单次 < 2 s”，并输出循环与 init 成本；看门狗不变。
- S5c 新测试（Auto 线程数与 HQ-exact 限制）；插件重建 BUILD_EXIT 0；box CLI 冒烟：L32 Auto FX 2T、乐器 3T、HQ-fast 3T（5.0 µs/样本）、HQ-exact → 有效势 1、3T；`--hq-exact-any-L 1` 时 15.4 µs/样本。
- 验证：全部测试通过（含 S5c）；压力测试 3 个种子普通 + 插桩全部 PASS（P3 普通 68,981 次、插桩 48,603 次，循环中位数约 0.6 ms）；pluginval strictness 10 两个 VST3 SUCCESS；zig 交叉编译 4 个目标 EXIT 0；27 个预设通过。
- 文档：README（状态、§1、§3.6 专家核验最终状态与 Phase 5c 段、§5.5、§6.5 重写、§8.0 新增、§8.1/§8.2、§9.0 Phase 6 候选、§10、附录）、`README_WINDOWS.md`、本报告 §12；同步包 `morpho-phs-phase5c-sync.zip`。

