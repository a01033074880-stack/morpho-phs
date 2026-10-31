# Phase 3 进度日志（Asia/Shanghai）

用户决策（17:43）：ENPU 默认 C + 每 8 样本插值 g/μ；向量化 log-cosh 近似（含误差界，保持能量/无源证明）；并联带隙拓扑通道；去掉 1/f 说法，改为“有界、持续的不规则性”；暂停逆向设计，改做预设（.morpho JSON）与宏旋钮；Phase 3 主攻 JUCE 8 插件 MORPHO-PHS。用户 PC 当前不可达 ⇒ 全部在 box 上完成，并准备 Windows 构建/基准脚本。

| 时间 | 步骤 | 状态 | 产物 |
|---|---|---|---|
| 17:43–17:50 | 工具链：box 的 apt 经代理失败（500 / TLS EOF）⇒ 用 curl 从 deb.debian.org 下载 .deb：g++-14（与已装 gcc 14.2.0-19 同版本，dpkg -i 安装）；31 个 X11/ALSA/freetype/fontconfig 开发包解包到 /workspace/sysroot（不安装进系统），pkg-config 包装脚本 /workspace/tools/bin/pkg-config | 完成 | /workspace/debs, /workspace/sysroot |
| 17:50 | JUCE 8.0.9（git tag，f72bad6）与 clap-juce-extensions（7adee3a）克隆到 third_party/；JUCE 许可：AGPLv3 / 商业双许可 | 完成 | third_party/ |
| 18:05 | log-cosh 快速近似拟合：sqrt 和式 K=3（Σw=1, Σw/c=1；Σwc=ln2 与前两者在 w≥0 时不可行，已记录） | 完成 | experiments/results/phase3/logcosh_fit.json, logcosh_surrogate_eval.json, reports/figures/phase3/logcosh_surrogate_error.png |
| 18:25 | 引擎：Potential::QuarticLogcoshFast；SAV 系数拆成逐元素 pass（可向量化）；g/μ 核内阶梯 ramp（Config::interp_step） | 完成 | include/morpho_phs/{core_types,esav_lf,esav_lf_mt}.hpp；zig 构建下 spiral b64 与 Phase 2 输出逐位一致 |
| 18:30 | ramp HF 检查（spiral，64 块）：step=0 +8.33 dB，step=8 +2.42 dB，step=1 0.00 dB（与逐样本参考一致） | 完成 | experiments/results/phase3/ramp_hf.json |
| 18:25–18:30 | 引擎：HQ-fast SAV 系数改为每项单次除法（4 sqrt + 4 div/标量）；工作线程 FTZ/DAZ（Config::ftz_workers）；Phase 3 单元测试全部通过（P3 功率平衡 3.76e-15、P4 无源 0、P7 processBlock 0 次分配） | 完成 | include/morpho_phs/*.hpp, experiments/results/phase3/test_phase3.txt |
| 18:25–18:30 | 插件源码：engine/（MorphoEngine、EngineHost、ChebyBank 跟随 on-site 刚度）、PresetIO、Processor、Editor、LatticeView、cli/；输出归一化按模式×尺寸标定 | 完成 | plugin/Source/ |
| 18:28 | 工厂预设生成器 → 27 个预设（7 个类别，含宏映射）；`morpho_cli validate presets` → 27 OK / 0 failed | 完成 | presets/*.morpho, python/scripts/phase3/p3_make_presets.py |
| 18:29 | 插件构建（Ninja + g++14，Release，LTO）：修复 PluginEditor.h 缺少 juce_audio_utils include；VST3 / Standalone / CLAP / morpho_cli 全部构建成功（各约 6 MB） | 完成 | build/plugin/MorphoPHS_artefacts/Release/{VST3,Standalone,CLAP}, build/plugin_build.log |
| 18:32 | pluginval（Linux，最新 release，xvfb，--strictness-level 5，in-process）：VST3 **SUCCESS** | 完成 | experiments/results/phase3/pluginval/pluginval_vst3_s5.txt |
| 18:33 | 离线渲染 render-modes L8/L16（nohup）：全部有限、0 reset；FX 正常电平；instrument bow/pluck MPE 电平过低（峰值约 −54 dBFS）⇒ 待标定 | 进行中 | experiments/audio/phase3/L*/, experiments/results/phase3/render_log.txt |
| 18:35 | 乐器标定：新增 kVoiceGain = 300（MPE 声部激励），bow MPE 峰值 L8 −4.8 / L16 约 −9 / L32 −10.8 dBFS（之前约 −54）；life_spiral 不变。重新构建 + pluginval s5/s10 + 全部渲染（nohup，/workspace/p3_step_build_render.sh） | 进行中 | plugin/Source/engine/MorphoEngine.hpp |
| 18:37 | Windows 脚本：build_windows.ps1（自动克隆固定版本 JUCE 8.0.9 / clap-juce-extensions@7adee3a；VS 2022/2026 或 Ninja+vcvars64；产物收集到 dist\windows；预设校验）、bench_windows.ps1（sysinfo + 4 组基准 + 渲染冒烟测试 → JSON/TXT/SUMMARY）、README_WINDOWS.md。未在 Windows 上运行（PC 离线） | 完成 | scripts/windows/ |
| 18:37 | 重新构建（含 kVoiceGain）BUILD_OK；pluginval VST3 strictness 5 **SUCCESS**，strictness 10 **SUCCESS**（含 fuzz parameters，25 组测试） | 完成 | experiments/results/phase3/pluginval/pluginval_vst3_s{5,10}.txt |
| 18:38 | MSVC 可移植性：zig 以 x86_64-windows 目标做语法检查发现 M_PI 未定义 ⇒ 在 5 个头/源文件加 `#ifndef M_PI` 保护（数值不变）；复查无错误 | 完成 | include/morpho_phs/{core_types,excitable}.hpp, plugin/Source/engine/{ChebyBank,MorphoEngine}.hpp, cli/main.cpp |
| 18:38 | 许可文档 docs/LICENSES_phase3.md（JUCE AGPLv3/商业；CLAP 与 clap-juce-extensions 为 MIT；VST3 SDK 为 Steinberg 许可/GPLv3）；1/f 措辞检查：仅 Phase 2 报告中保留如实的阴性结果，新文档统一用“有界、持续的不规则性” | 完成 | docs/LICENSES_phase3.md |
| 18:42 | **问题**：FX Edge 渲染下游拾音（单向边缘态）严重削波（L16 右声道 27% 样本 ≥0.98；chir=0 时两声道均约 15%）。原因：之前的归一化按上游（冷）声道标定。两轮缩放渲染测得湿声 rms：下游/上游 ≈ 0.060/0.0013（L16），即约 33 dB 隔离 | 已定位 | /tmp 标定渲染 |
| 18:42 | 修复：kOutNorm[edge] 250 → 1.75，kSizeNorm[edge] = {1.0, 1, 1.4}；CLI 增加 `--edge-norm-scale`（仅用于标定） | 完成，待重新渲染 | plugin/Source/engine/MorphoEngine.hpp, cli/main.cpp |
| 18:44 | 重新渲染（nohup p3_step_render2.sh）；排队安静基准（p3_step_bench.sh：CLI bench fx 四次/HQ-fast/乐器/HQ-exact L32 + 引擎 L32 hq_fast 安静重跑），基准期间不做重负载 | 进行中 | experiments/results/phase3/bench_cli_* |
| 18:49 | 修复 CLI bench 的 std::bad_alloc（Phrase(1e9) 预生成了过多事件 ⇒ 改为 Phrase(secs+2)）；重新启动安静基准。引擎 L32 hq_fast 安静重跑已完成（18:46–18:48） | 进行中 | plugin/Source/cli/main.cpp, experiments/results/phase3/bench_engine_L32_hqfast_v2_quiet.txt |
| 18:50 | README 更新（Phase 3 段落 + Linux 复现命令） | 完成 | README.md |
| 18:51 | CLI 基准（安静，2 s/配置，48 kHz，块 64/128/512 × L 8/16/32 × 线程 1–8）完成：FX 四次势 L32 中位数 1T 23.0 / 4T 8.2 / 8T 5.4–8.1 µs/样本（预算 20.83）；HQ-fast L32 1T 27.0 / 3T 11.2 / 4T 9.1–11.4；HQ-exact L32(块128) 1T 54.3 / 4T 16.3 / 6T 12.5；L16 1T 5.8–7.5；L8 约 1.5–1.9 | 完成 | experiments/results/phase3/bench_cli_*.{json,txt} |
| 18:52 | 渲染电平审计（63 个 WAV，逐声道峰值/RMS/>0.9 比例）：模式渲染全部未触发限幅器；预设 5 个触发（10、14–17），3 个过轻（19–21）⇒ 生成器加入 TRIM（仅输出增益，音色不变），重新生成并校验 27 个预设；随后重新渲染预设 + 最终构建 + pluginval（nohup p3_step_final_build.sh） | 进行中 | python/scripts/phase3/p3_render_levels.py, experiments/results/phase3/render_levels.{json,txt}, presets/ |
| 19:00 | 预设重新渲染 + 审计：63 个 WAV 全部有限，**0 个**触发限幅器（>0.1% 样本 >0.9）；预设峰值 −1.5…−11 dBFS | 完成 | experiments/results/phase3/render_levels.txt |
| 19:02 | 插值成本实测：HQ-fast L32 4T 块128：1 样本 9.30 / 8 样本 9.49 / 关闭 12.3 µs/样本（差别在噪声内）；最终构建 BUILD_OK、pluginval s5/s10 SUCCESS、预设校验 27/0；报告初稿 | 完成 | experiments/results/phase3/bench_cli_interp*.{json,txt}, reports/Phase3_阶段报告.md |
| 19:03 | Standalone 编辑器 Xvfb 截图发现 UI 问题（参数读数位数过多、宏旋钮挤压、未加载预设时下拉框显示第一个预设名）；已修复并以 nohup 重建 + pluginval s10 | 进行中 | reports/figures/phase3/editor_standalone.png, /workspace/p3_step_ui_build.log |
| 19:05 | UI 修复后重建 BUILD_OK；pluginval s10 SUCCESS（pluginval_vst3_s10_ui.txt）；新截图加入报告 | 完成 | reports/figures/phase3/editor_standalone.png |
| 19:05 | Phase3 报告 PDF 生成（4 页，headless Chrome） | 完成 | reports/Phase3_阶段报告.{md,html,pdf} |
| 19:05 | 打包 /workspace/morpho-phs-phase3.zip（排除 build/、third_party/、.f32 等原始文件）和 Linux 插件 zip | 完成：phase3.zip 467 文件 156.8 MB（未压缩 206.4 MB，含 phase2/3 WAV）；插件 zip 5.1 MB | /workspace/p3_make_zips.py |
