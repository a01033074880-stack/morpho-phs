# Phase 4 进度日志（Asia/Shanghai）

| 时间 | 步骤 | 状态 | 证据 / 文件 |
|---|---|---|---|
| 20:36 | Phase 4 开始。注意：父任务所说的 i9 结果目录 experiments/results/phase3/windows/DESKTOP-114514_20261030_201314/ 在 box 上不存在；本阶段只使用父任务转述的 i9 关键数字 | 进行中 | — |
| 20:45 | test_phase4.cpp 编译通过（未运行，等待基线 bench 结束）；CLI bench 重写（--pin/--mmcss/--pool/--load/任意线程数/p99.9/PASS 判定/按块周期 paced）；RtOptions 接入 BuildSettings/EngineHost；D1 interp 默认 0；D4 edge_width/edge_crosstalk 参数 + 湿信号立体声矩阵 + 预设格式 v2（v1 兼容）+ 27 个预设迁移 | 完成（待编译） | python/scripts/phase4/p4_migrate_presets.py |
| 20:47 | 基线 bench 完成（linux_before/before_quiet + before_loaded3）。D2 两插件 CMake + 处理器/编辑器变体；D6 LICENSE（AGPLv3 全文，与 /usr/share/common-licenses/AGPL-3.0 一致）+ 79 个源文件 SPDX 头；预设规范 v2 文档 | 完成（待编译） | — |
| 20:49 | 测试全部通过：test_phase4（Q1–Q5，含 9 种池/绑核/MMCSS 变体与 1 线程逐位一致、零分配、i9 合成拓扑、超订阅不死锁）、test_mt、test_phase3、test_main | 完成 | experiments/results/phase4/tests/*.txt |
| 20:50 | D7：logcosh_fit.json 删除过时的 K=2 条目（注意：剩余 "3"/"4" 条目与实际使用的 K=3 常数不一致，实际常数见 logcosh_surrogate_eval.json / core_types.hpp，列为 Phase 5 待决）；docs/LICENSES_phase4.md | 完成 | — |
| 20:53 | Linux 构建成功：MORPHO-PHS（乐器）与 MORPHO-PHS FX（效果）各 VST3/Standalone/CLAP + morpho_cli；zig 交叉编译 test_phase4.cpp → Windows x64 (mingw) 成功，导入 AVRT.dll(AvSetMmThreadCharacteristicsW 等)、GetLogicalProcessorInformationEx、SetThreadGroupAffinity；build_windows.ps1 加 -VsPath、双插件；bench_windows.ps1 重写为 Phase 4 矩阵 | 完成 | build/wincheck/ |
| 20:56 | 发现：按宿主节奏（paced）运行时，工作线程在块间休眠导致中位数变慢约 40%（L32/块128/5 线程/P 核/专用池：不 paced 7.87 µs/样本 vs paced 10.85）。实现**自适应自旋**：调用方按块周期 EMA 设定工作线程空闲自旋时长 = clamp(1.25×周期, idle_spin, 10 ms)，专用池调用方等待也按忙时 EMA 自旋；Config.adaptive_spin / RtOptions.adaptive_spin / CLI --adaptive 0\|1。快测 4 线程 paced：8.62 µs（自适应）vs 11.14（关闭） | 完成 | esav_lf_mt.hpp |
| 20:58 | 自适应自旋后重新编译并重跑全部测试：test_phase4 / test_mt / test_phase3 / test_main 全部通过；Linux 双插件 + CLI 重新构建 BUILD_EXIT 0；README.md 与 README_WINDOWS.md 更新（双插件、-VsPath、环境变量、AGPL） | 完成 | experiments/results/phase4/tests/ |
| 21:00 | Linux “after” 基准开始（nohup，/workspace/p4/step_after.sh）：A 安静不 paced（与 before 同矩阵 + 5 线程）、C 安静 paced、B 3 个外部忙循环；变体 p3spin/baseline/pin/pset（Linux 上 MMCSS 为空操作，pin_mmcss≡pin） | 进行中 | experiments/results/phase4/linux_after/ |
| 21:06 | 发现：专用池 8 个工作线程（8 vCPU 上，加上调用方 = 超订阅）时 L32 达 168 µs/样本（正常约 7）。加入**超订阅保护**（专用池 T ≥ 逻辑 CPU 数时退回调用方计算）与“工作线程多于 P 核时不重复钉同一核（改软亲和）”；after 基准中专用池线程数限制为 ≤6（B 中 ≤5） | 完成 | linux_after/A_quiet_unpaced_pin.txt（8T 行）、D8_* |
| 21:16 | 用户追加：撰写专家咨询用 README.md（中文，全项目）并放入同步包；21:21 用户限定：Phase 4 + README 为终点，不开始 Phase 5 | 进行中 | — |
| 21:33 | Linux after 基准完成（A/C/B/D8），汇总 `experiments/results/phase4/linux_summary.md`（p4_summarize_linux.py）。发现：有 3 个外部忙循环时，凡“计算线程 + 调用方 + 负载 > CPU 数”，基于等待的屏障崩溃（baseline 6T 64 µs、pin 5T 171 µs/样本）；纯自旋中位数退化较少但有 0.4–0.6 s 尖峰 | 完成 | linux_after/B_* |
| 21:37 | 加保护后重编译，四套测试全部通过，双插件重建 BUILD_EXIT 0 | 完成 | tests/ |
| 21:40 | 修正：专用池时调用方（宿主线程）默认不再整块自旋（caller_pool_spin=false，只自旋 barrier_spin 后阻塞，每块一次唤醒），避免多占一个 CPU；CLI --caller-spin；验证基准 E 运行中 | 进行中 | linux_after/E_* |
| 21:43 | 验证基准 E 完成：安静时与原默认相近（pset 5T 7.87 µs / p99÷预算 0.603 / 4 超时）；有负载时 5T 从 171 降到 48.5 µs/样本但仍全部超时，3–4T 超时反而略多 → 宿主线程等待方式列为待决，bench_windows.ps1 -All 加 pin_mmcss_cspin、pin_mmcss_caller 对比 | 完成 | linux_after/E_*、linux_summary.md 表 E1/E2 |
| 21:44 | 最终代码重编测试全部通过（ALL_DONE），双插件 VST3/CLAP/Standalone 重建 BUILD_EXIT 0；pluginval strictness 10：MORPHO-PHS 与 MORPHO-PHS FX 均 SUCCESS（各 25 组，12 + 15 个预设） | 完成 | experiments/results/phase4/pluginval/ |
| 21:49 | Windows 检查：zig 交叉编译 test_phase4.exe（最终头文件）通过；两个 .ps1 经 PowerShell 7.4 解析 0 错误，改为纯 ASCII | 完成 | build/wincheck/（不入同步包） |
| 21:50 | 专家咨询版 README.md（中文，10 节 + 附录 A/B）完成，含 Phase 0–4 决策与理由、代码地图、box 实测汇总、i9 Phase 3 实测、问题与尝试、未决问题、专家问题；§6.4 留给用户补 Phase 4 i9 复测 | 完成 | README.md |
| 21:51 | Phase 4 结束：同步包（相对 phase4_baseline_hashes.json 的新增 64 + 修改 112 个文件 + 清单，Python zipfile）→ /workspace/phase4_sync/，split -b 15m -d；SHA256 见回报。按用户要求不开始 Phase 5 | 完成 | /workspace/phase4_sync/ |
| 10-30 21:52 | 用户 PC：Phase 4 代码用 MSVC 14.44 构建成功（两个插件 VST3/CLAP/Standalone + morpho_cli.exe，27 个预设 validate OK） | 完成 | logs/build_phase4_console.txt |
| 10-30 21:53 | i9 基准第一次运行（“平衡”电源计划），PC 关机中断，只完成 baseline、p3spin | 中断 | experiments/results/phase4/windows/DESKTOP-114514_20261030_215301/ |
| 10-31 11:17 | 重跑开始后用户把电源模式改为“最佳性能”，前后电源条件混在一起，该次运行中止并整组排除 | 排除 | experiments/results/phase4/windows/DESKTOP-114514_20261031_111728_mixed_power_aborted/ |
| 10-31 11:25 | **i9 复测完成**：`bench_windows.ps1 -All`，“最佳性能”，paced，4 s/配置，L16/L32 × 块 64/128/512 × 线程 3–6，8 个变体全部有数据。32×32 块 128：0/32 组 PASS，最好 p3spin 4–6 线程 7–12/1500 次超时（p99/预算 0.96–0.99，中位数 11–14 µs）；pin（无 MMCSS）尖峰最大 183 ms；16×16 pin_mmcss / pin_mmcss_caller 块 128、512 全部 PASS；pset 不比 pcores 好 | 完成 | experiments/results/phase4/windows/DESKTOP-114514_20261031_112539/、logs/bench_phase4_console_bestperf.txt |
| 10-31 11:50 | README.md §6.4 填入 i9 实测（判据表、L16、电源模式对比、结论与假设），并更新 §6.1–6.3、§7、§8、§9、附录 A；Phase4_阶段报告新增 §10 并重新生成 .html/.pdf；i9 增量同步包 /workspace/phase4_sync/morpho-phs-phase4-i9-sync.zip。按用户要求停止，不开始 Phase 5 | 完成 | README.md、reports/Phase4_阶段报告.* |
