# Phase 2 进度日志（Asia/Shanghai）

用户决策（2026-10-30 16:37）：四次势默认、log cosh 为高质量选项；32×32 必须多线程实时；τ 作为用户旋钮；JUCE 8（后续阶段，与 AETHER 隔离）；不在用户 PC 上运行任何东西（i9 数字仅为估算）；ENPU 变体做 A/B 后推荐。

| 时间 | 步骤 | 状态 | 产物 |
|---|---|---|---|
| 16:37 | Phase 2 启动，盘点环境 | 完成 | — |
| 16:42 | C++ 多线程内核 esav_lf_mt.hpp + τ 旋钮 + 势函数切换 + ENPU 变体 B；tests/test_mt.cpp 9/9 通过 | 完成 | experiments/cpp_test_mt_output.txt |
| 17:01 | 融合 HQ 系数 sav_coeff_hq（一次 expm1 + 一次 log1p）；ENPU 变体 C（selective）；v_at / sav_inconsistency；两套 C++ 测试全部通过 | 完成 | experiments/cpp_test_mt_output.txt, cpp_test_output.txt |
| 17:00–17:14 | MT 基准（盒子原生 AVX-512 与 AVX2 构建，L=32 与 L=16；L=48 因耗时中止，未测） | 完成 | experiments/cpp_bench_mt_box.txt, cpp_bench_mt_box_avx2.txt |
| 17:03 | 修复 venv 的 pip（用系统 pip wheel 强制重装），安装 torch 2.15.0+cpu | 完成 | — |
| 17:14–17:20 | exp1 drift / alias；exp2 impulse / burst；exp3 全部 14 次运行 + 校准 | 完成 | experiments/results/phase2/exp1_*.json, exp2_*.json/npz, exp3_coupling.json |
| 17:20 | 发现并修复 ENPU-C 缺陷：质量改变也会移动 (q, q⁻)，因此必须同时重投影 ψ（之前 C 在质量滑音场景 SAV 不一致度 7.5）；新增 g/μ/η 快速路径（能量逐位不变，无需求能量） | 完成 | include/morpho_phs/esav_lf_mt.hpp |
| 17:19 | exp2 分析：边缘演示 ±6 dB 的诊断（鼓信号仅 1.1% 能量落在带隙内；无吸收边界时手性波绕周长回到另一拾音点），滤波器选型，修复后演示 08–12 | 完成 | exp2_analysis.json, edge_v2/edge_v2_metrics.json, experiments/audio/phase2/08–12 |
| 17:20 | ENPU A/B/C（3 场景 × 3 变体 × {块 64, 逐样本}）+ WAV | 完成 | experiments/results/phase2/enpu/enpu_ab.json, experiments/audio/phase2/enpu_* |
| 17:21– | exp4（torch 可微 ESAV-LF，3 个目标，各 200 次迭代）；exp1 stress 余下套件与 rtf | 运行中 | — |
| 17:27 | 会话中断后核查：exp4 三个进程仍在运行（ESAV 第 120/200 次迭代），未重启；exp1 stress A/B 全部套件已保存（exp1_stress.json、exp1_stress_b.json），rtf 已完成（exp1_rtf.json）。exp4 脚本新增每 10 次迭代的增量检查点（只对之后的运行生效）。核对报告草稿中的数字：修正 RK4 漂移、MSE、HQ 系数精度（新测 tools/check_hq_coeff.cpp → hq_coeff_accuracy.json：f64 1.9e-12，f32 9.9e-4），并发现 L48 基准其实已写入 cpp_bench_mt_box.txt | 完成 | experiments/results/phase2/* |
| 17:32 | exp4 三个目标完成（ESAV 200 + 模态 200 次迭代）：模态基线在 3/3 目标上更优，隐藏参数未找回 | 完成 | experiments/results/phase2/exp4/exp4_*.json, experiments/audio/phase2/exp4_* |
| 17:33 | 重新生成全部 Phase 2 图（含 exp4、完整 stress）与 p2_tables.json；exp3 试听 WAV 4 个 | 完成 | reports/figures/phase2/, paper/figures/p2_* |
| 17:35 | Phase 2 报告（中文，18 页 PDF）、README 更新、压缩包 | 完成 | reports/Phase2_阶段报告.{md,html,pdf}, README.md, /workspace/morpho-phs-phase2.zip |
