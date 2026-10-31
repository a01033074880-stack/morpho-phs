<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Phase 5 阶段报告：模板内核、纯自旋线程协议、基准规范化与专家建议核验（含 Phase 5b：i9 实测、专用池卡死修复；Phase 5c：最终数据与默认值）

日期：2026-10-31（Asia/Shanghai）。全部工作在 box（Linux KVM 虚拟机，8 vCPU，Intel Xeon）上完成，没有在用户 PC 上运行任何东西，也没有接触其它项目。i9-13900H 上的复测由用户在拿到同步包后执行（命令见 §9）。Phase 5b（同日 14:47 起）根据用户发回的 i9 数据修复专用池卡死、加固基准脚本、调整默认线程数，见 §11。Phase 5c（同日 15:48 起）根据 i9 三轮最终数据确定默认值并收尾，见 §12。

**目标**：i9-13900H 上 32×32、块 128 实时：0 次超时且 p99/预算 < 0.7（预算 2666.7 µs/块 = 20.83 µs/样本）。

**一句话结论**：新的二分格模板（stencil）SIMD 内核在 box 上把 32×32 单线程从 21.0 降到 **5.2 µs/样本（4.0 倍）**，结果与旧 CSR 内核**逐位一致**；box 上 **1 线程已 PASS**（p99/预算 0.39，0/2250 超时）。线程协议改为“调用方计算 + 2 个工作线程（干净 P 核 CPU Sets）+ 块内纯自旋屏障 + 关闭 EcoQoS”。i9 上能否达标要等用户复测；box 是虚拟机，多线程行的尾部受虚拟机抢占影响，不能代表 i9。

**Phase 5b 更新（i9 已实测，§11）**：i9 上 1T 从 26.6（CSR）降到 **4.67 µs/样本（5.7 倍）**，1 线程即 PASS（p99/预算 0.367）；插件默认协议 2/3/4 线程全部 PASS（0.30–0.31，bOSw 0）。专家的分诊（先修内核）在 i9 上得到证实。

**Phase 5c 最终结论（i9 三轮，§12）**：**Phase 5 完成，32×32 实时在 i9 上达成。** FX 四次势 2T 0/11250 超时、p99/预算最差 0.310；乐器 3T 0/11250、0.480。默认值：L32 Auto FX 2 / 乐器 3 / HQ-fast 3（标“High CPU”）；HQ-exact 只允许 L ≤ 16（L32 按 HQ-fast 运行并提示）；专用池保持非默认。PL1 / 热降频是尾部主因的假设不被数据支持。

---

## 1. 起点

Phase 4 i9 复测（2026-10-31，“最佳性能”）：32×32 块 128 没有任何配置 PASS；最好的是 Phase 3 式纯自旋 4–6 线程，7–12/1500 次超时；中位数 11–14 µs/样本。用户随后提供了外部专家报告（`docs/expert_review_phase4.md`），要求“仔细分析后自行决断”。本阶段把专家的数字当作假设，逐条实测核验（§8）。

## 2. 工作项与结果总表

| # | 工作项 | 结果 |
|---|---|---|
| 1 | 基准规范化 | `bench_windows.ps1` 重写：每个变体前冷却（默认 20 s）、按种子随机顺序、重复 3 次、每次运行记录电源方案 + 电源模式覆盖 + AC/电池 + WMI 有效频率；CLI 每行加进程内频率估计 `clkGHz`、只统计测量窗口的屏障 OS 等待 `bOSw`；新增 1T / 1T_csr 行和 `-Variants` |
| 2 | 线程协议 | 块内纯自旋屏障（`SpinBarrier`，只有单次等待 > 1 ms 时才 yield 自救，并计数）；干净 P 核列表（去掉物理核 0、每核一个逻辑 CPU）；CPU Sets 软绑定（`SetThreadSelectedCpuSets`）；关闭 EcoQoS；默认调用方计算、L32 共 3 线程；box 测试 `barrier_os_waits` = 0 |
| 3 | SoA/模板内核 | 二分蜂窝格交错布局 + 固定偏移（slot0 = 同格伙伴 `i^1`，slot1 = ±1，slot2 = ±(2Lx−1)）+ 每格点每槽系数，AVX2 4/8 格点并行 2×2 直解；与 CSR **逐位一致**（112 组配置 + 1 s 长跑）；CSR 保留为 `--kernel csr` / `MORPHO_PHS_KERNEL=csr` |
| 4 | 乐器模式开销 | gprof 定位：不是 MPE 激励，而是 ENPU-C 每个控制 tick 在调用方线程上串行执行 `energy()` 与 `reset_psi()`；向量化后逐位一致，box 1T 乐器模式 6.28 → 5.51 µs/样本 |
| 5 | E-RSAV | 实现为开关（默认关）；C++ 为单侧“钳位”版；SNHR 对比结果有正有负（±2 dB），远不到专家说的 +25～+32 dB；保持默认关 |
| 6 | k 步时空分块 | 不做：能量门控每个样本都要全局能量，存在逐样本全局耦合；重叠 halo 的重复计算估算 k=8 时 1.875 倍 |
| 7 | 延后事项 | float32 混合精度（插件已经是 float32）、多速率、SSB 外差注入、Padé HQ 近似：写入 README，说明原因 |

## 3. 模板内核（工作项 3）

**布局**：蜂窝格按“格 = A、B 两个格点”交错存储（下标 i 的 A/B 由 i 的最低位区分），每个格点恰好 3 个邻居：
- slot0：同一格内的伙伴，下标 `i ^ 1`（向量内 `swap_pairs` 置换）；
- slot1：水平邻居，A 取 i−1、B 取 i+1（两次非对齐加载 + `blend`）；
- slot2：竖直邻居，A 取 i−o2、B 取 i+o2，o2 = 2Lx−1。

缺失的边（边界、缺陷）用零系数 + 零填充 guard 区表示，加 ±0 不改变结果。每格点每槽 3 个系数（cxx, cxy, cyy）存为 `sc_[9]`。

**逐位一致的条件**：与 CSR 版本完全相同的运算顺序；禁止 FMA 收缩（GCC/Clang 加 `-ffp-contract=off`，MSVC `/fp:precise` 本来不收缩）。

**测试**（`tests/test_phase5.cpp`）：
- S1：f32/f64 各 56 组配置（32×32、16×16、8×8、5×7、3×11；四次势 / HQ / HQ-fast / 无势；E-RSAV 开关；T=1/3；缺陷、ENPU-C、逐样本斜坡、门控），全部逐位一致；
- S1b：32×32、48000 样本长跑，stencil T=3 与 CSR T=1 逐位一致；
- S2：能量恒等式残差 3.8e-15 – 5.2e-15；S3：μ≤0 无源，能量最大上升 0；
- S6：音频循环 0 次堆分配。

**box 实测（32×32，块 128，不 paced，6 s；`experiments/results/phase5/box/`）**

| 配置 | FX 模式（mode 0）µs/样本 | p99/预算 | 超时 | 乐器模式（mode 2）µs/样本 | p99/预算 | 超时 |
|---|---|---|---|---|---|---|
| Phase 4 二进制 1T（CSR） | 21.05 | 1.665 | 1264/2250 | 21.50 | 1.592 | 1615/2250 |
| Phase 5 1T stencil | **5.21** | **0.387** | **0** | **5.51** | **0.413** | **0** |
| Phase 5 1T CSR（`--kernel csr`） | 25.54 | 1.415 | 2250 | 26.07 | 1.456 | 2250 |

- 单线程提速 21.05 / 5.21 = **4.0 倍**（专家估计 2.6–3.5 倍）。
- Phase 5 的 CSR 比 Phase 4 慢约 20%，原因已查明：GCC 构建加了 `-ffp-contract=off`。同一程序对比：contract=off CSR 24.6、contract=fast CSR 20.8；stencil 4.85 vs 4.51 µs/样本。Windows 的 MSVC `/fp:precise` 本来不收缩，**i9 上不受影响**。
- 多线程（box，调用方计算 + CPU Sets，FX / 乐器 × 不 paced / paced，共 12 行）：中位数 2T 3.79–4.59、3T 3.14–3.87、4T 2.87–3.65 µs/样本，p99/预算 0.17–0.41。只有乐器模式不 paced 的 2T、3T 为 0 超时 PASS（4T NEAR，1 次）；其余行每 2250 块有 1–9 次 3–21 ms 尖峰，几乎都伴随 `bOSw` > 0（单次屏障等待超过 1 ms），这是虚拟机 vCPU 被宿主机抢占的特征。**box 多线程尾部不能代表 i9。**

## 4. 线程协议（工作项 2）

- **SpinBarrier**（`rt_thread.hpp`）：块内每样本屏障只自旋（`_mm_pause`），没有 `WaitOnAddress`。保护：单次等待超过 `rescue_ns`（默认 1 ms）后改为 `std::this_thread::yield()`，每次这样的等待在 `blocked_waits()` 里计一次。超订阅对比（8 个忙线程 + 8 个计算线程 / 8 CPU）：Phase 4 混合屏障平均 203 ms/块、最大 592 ms；纯自旋 + 自救 1.6–2.1 ms/块、最大 41–62 ms（S8）。
- **块间空闲**：工作线程自旋 3.5 ms 后才进入 OS 等待（宿主块周期 2.67 ms 之内不睡）。
- **干净 P 核**：每个物理 P 核取一个逻辑 CPU，排除物理核 0。i9-13900H → {2,4,6,8,10}（S5，合成拓扑）。专家说 Phase 4 的 pset 包含 CPU 0 与 SMT 兄弟，这一点核实为真。
- **CPU Sets**（`pin=cpuset`，默认）：Windows 用 `SetThreadSelectedCpuSets`（软约束，调度器仍可在集合内迁移），Linux 用亲和掩码。
- **EcoQoS**：计算线程 `SetThreadInformation(ThreadPowerThrottling)`，执行速度节流关闭。
- **默认线程数**：L≤16 → 1；L=32 → min(P 核数, 3)，即宿主线程 + 2 个工作线程。

## 5. 乐器模式开销（工作项 4）

gprof（box，1T，mode 2 vs mode 0）：

| 项目 | mode 0 | mode 2 |
|---|---|---|
| `energy()` 调用次数 | 2 | 20268（0.39 s） |
| `U()` 时间 | — | 0.40 s |
| `reset_psi()` 调用次数 | — | 4894（0.09 s） |
| MPE `render()` | — | 0.01 s（可忽略） |

原因：乐器预设的形态场每个控制 tick 都改 α/m，选择性 ENPU-C（`enpu_impl` / `finish_enpu`）就在调用方线程上串行执行 `energy()` 与 `reset_psi()`；这在多线程时是纯串行段（约 114 µs/块）。

修复：`energy_stencil()` 与 `reset_psi_simd<POT>()` 做 AVX2 向量化，每格点运算顺序与标量相同，能量和仍按格点顺序在 double 里依次累加 ⇒ 逐位一致（S9：360/360 次检查，含势能切换）。32×32 f32：`energy()` 15.1 → 5.2 µs；`set_potential`（缩放 + reset_psi + 2 次 energy）45.4 → 18.4 µs。box 1T 乐器模式额外开销从 1.06 降到 0.30 µs/样本。

## 6. E-RSAV（工作项 5）

- C++（`MORPHO_PHS_ERSAV=1` / `--ersav 1`）：单侧钳位 ψ = copysign(min(|ψ̃|, √(2U(qⁿ⁺¹)+c₀)), ψ̃)，移走的能量计入耗散项 ⇒ 能量恒等式与无源性不变（S2、S3）。
- Python 另测“预算”变体：ψ 也可以上调，但上调量不超过该自由度本步阻尼耗散能量的 γ=0.95 倍（仍无源，只在 Python 里）。

SNHR（`python/scripts/phase5/p5_ersav_alias.py`，8×8，f0 = 3.1 kHz，三个激励电平 0.03 / 0.3 / 1.0）：

| 方案 | SNHR dB | 相对 ESAV | 备注 |
|---|---|---|---|
| 参考（RK4 ×8 过采样） | 104.5 / 104.8 / 2.07 | | |
| ESAV（当前） | 106.4 / 68.2 / 1.73 | 0 | 中电平比参考低 36.6 dB（即 Phase 2 的“低 37 dB”） |
| ESAV + 钳位（C++ 版） | 108.9 / 67.1 / 3.11 | +2.4 / −1.1 / +1.4 | 无源 |
| ESAV + 预算（仅 Python） | 108.9 / 86.1 / 0.42 | +2.5 / **+17.9** / −1.3 | 无源，但跟踪能量恒等式偏差 6.9e-4（因为把耗散能量还给了 ψ） |

C++ S7（16×16 强驱动）：平均 SAV 不一致度 1.98（关）vs 2.93（开），钳位版在该场景反而更不一致。

**决定**：默认关。钳位版效果有正有负（±2 dB），不到专家说的 +25～+32 dB；预算版只在中电平明显改善，高电平变差，并且改变了能量恒等式的形式，需要新的证明后才考虑移植。

## 7. k 步时空分块（工作项 6）——不做

专家的前提是“门控每 64 样本一次”。实际代码里能量门控（定理 4）**每个样本**都用上一样本的全局能量决定 μ 的上限（`gate_open`），这是逐样本的全局归约；k 步分块要么破坏逐位一致与定理 4，要么每样本仍需一次全局同步，屏障省不掉。重叠 halo 的重复计算：k=8 约 1.875 倍、k=4 约 1.375 倍（3 线程按行分条）。单线程已经达标，所以不做。

## 8. 专家建议核验

| 专家说法 | 核验结果 |
|---|---|
| “32×32 单线程在 i9 上 23 µs/样本” | **不对**：23.0 是 box 上 Phase 3 插件的数字，i9 从未测过 1T。本阶段的基准加入了 1T 行 |
| CSR 改模板 + SIMD 单线程提速 2.6–3.5 倍 | **成立，而且更好**：box 4.0 倍（21.05 → 5.21），逐位一致 |
| “内核全程 double” | **不对**：插件引擎一直是 float32（`MorphoEngine.hpp: using Real = float`） |
| 块内 `WaitOnAddress` 唤醒造成超时 | 与 Phase 4 数据一致（混合屏障 219–12,395 次 OS 等待）；已改为纯自旋，box `bOSw` = 0；i9 待测 ——**Phase 5c：i9 证实**（纯自旋调用方计算 2/3T bOSw 0、0/11250 超时） |
| Phase 4 P 核集合包含 CPU 0 与 SMT 兄弟 | **成立**；已改为干净 P 核 |
| MMCSS 变慢是 PL1 功耗墙 / 热降频（4.8/3.1 = 1.55） | **未验证**：只是假设。新基准每行记录频率并随机顺序、冷却，i9 复测可以检验 ——**Phase 5c：不被数据支持**（§12.3） |
| 硬绑核“饥饿陷阱” | 合理但未验证；默认改为 CPU Sets（软） |
| “门控每 64 样本一次，k=8 分块可逐位一致” | **不对**：门控逐样本全局耦合（§7） |
| “乐器模式开销来自 MPE 声部遍历” | **不对**：MPE `render()` 只占 0.01 s；真正原因是 ENPU-C 串行 `energy()`/`reset_psi()`（§5） |
| “E-RSAV 一行代码 SNHR +25～+32 dB” | **不对**：钳位版 −1.1～+2.4 dB；预算版中电平 +17.9 dB，高电平 −1.3 dB（§6） |
| float32 混合精度再省 45–50% | 前提不成立（已是 float32）；延后 |
| 多速率、SSB 外差注入、Padé HQ | 延后（改变物理或需要新证明 / 听感评估），见 README §8.3 |

## 9. 验证与交付

- 测试（box，`experiments/results/phase5/tests/`）：test_main、test_mt、test_phase3、test_phase4、test_phase5 全部通过，0 失败。
- pluginval strictness 10（Linux、Xvfb、in-process）：MORPHO-PHS 与 MORPHO-PHS FX 都 SUCCESS（`experiments/results/phase5/pluginval/`）。
- Windows 交叉编译检查（zig mingw，`-mcpu=x86_64_v3`）：test_phase5.exe、test_phase4.exe 编译通过，导入了 SetThreadSelectedCpuSets、GetSystemCpuSetInformation、SetThreadInformation、AvSetMm*、WaitOnAddress 等（`experiments/results/phase5/wincheck/`）。test_mt.cpp 只在 Linux 上用（`std::aligned_alloc`，自 Phase 3 起就是这样）。
- i9 复测命令（PowerShell，在项目根目录）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\windows\build_windows.ps1 -VsPath C:\SDKs\AetherBuildTools -Generator Ninja
powershell -ExecutionPolicy Bypass -File scripts\windows\bench_windows.ps1        # Phase 5 原版约 17–20 分钟（已于 14:15 执行，见 §11；Phase 5b 补测命令见 §11.6）
```

结果在 `experiments\results\phase5\windows\<主机>_<时间>\SUMMARY.txt`。判据行：p5_caller_cpuset 2/3/4 线程、1T、inst_p5；p5 变体的 `bOSw` 应为 0。

## 10. 风险与未决

- 每个插件实例的 3 个计算线程在宿主持续调用时一直忙等（块间空闲自旋 3.5 ms）；多实例时会占满多个核。超订阅保护保留（专用池线程数 ≥ CPU 数时退回调用方计算；纯自旋有 1 ms 自救）。i9 上 1T 已达标，Phase 5b 已把 L32 默认降为 2 线程（§11.4）。
- box 是虚拟机：多线程尾部与 `bOSw` 受 vCPU 抢占影响；S4 每种模式允许重试一次（每次都记录）。i9 的 `bOSw` 才是权威数字。
- E-RSAV 预算版、k 步分块、多速率等改动物理或证明的事项都没有进插件。

---

## 11. Phase 5b：i9 实测、专用池卡死修复、基准脚本加固与默认线程数

### 11.1 i9 实测结果
数据：`experiments/results/phase5/windows/DESKTOP-114514_20261031_141513/`（`runlog.csv`、`<变体>_r<k>.json/.txt`、`sysinfo.json`；控制台日志 `experiments/results/phase5/windows/logs/`）。条件：Windows 11 家庭版，电源方案“平衡” + 电源模式“最佳性能”，AC，32×32，块 128，四次势（quartic），每配置 10 s（3750 块），按主机节奏（paced）。原计划 9 个变体 × 3 次重复 = 27 次运行，实际完成 10 次：第 1 轮全部 9 个变体 + 第 2 轮 inst_p5；第 2 轮的 p5_dedicated_cpuset 在 3 线程行 PASS 后卡死 16 分钟（14:22:47–14:38:46），脚本因非零退出码中止（原因与修复见 §11.2）。

| 变体（第 1 轮） | 线程 | µs/样本 | p99/预算 | 最大块 µs | 超时块 | 结论 | clkGHz | bOSw |
|---|---|---|---|---|---|---|---|---|
| 1T（模板内核） | 1 | 4.670 | 0.367 | 1506 | 0/3750 | PASS | 4.61 | 0 |
| 1T_csr（Phase 2–4 内核） | 1 | 26.567 | 1.692 | 9259 | 3750/3750 | FAIL | 4.74 | 0 |
| p5_caller_cpuset（插件默认协议） | 2 / 3 / 4 | 3.494 / 2.962 / 2.859 | 0.301 / 0.305 / 0.308 | 1428 / 1237 / 1339 | 0 / 0 / 0 | PASS ×3 | 4.71 / 4.32 / 3.88 | 0 / 0 / 0 |
| p5_caller_cpuset_host（宿主线程也进 CPU Set + MMCSS） | 2 / 3 / 4 | 3.413 / 2.839 / 2.614 | 0.232 / 0.205 / 0.206 | 850 / 886 / 15319 | 0 / 0 / 1 | PASS / PASS / NEAR | 4.76 / 4.20 / 3.82 | 0 / 0 / 42 |
| p5_caller_pin（硬绑 P 核） | 2 / 3 / 4 | 3.498 / 3.018 / 3.317 | 0.329 / 0.334 / 0.360 | 1775 / 1249 / 1358 | 0 / 0 / 0 | PASS ×3 | 4.62 / 4.52 / 3.76 | 0 / 0 / 0 |
| p5_dedicated_cpuset（专用池） | 3 / 4 | 3.013 / 2.953 | 0.303 / 0.252 | 2441 / 2016 | 0 / 0 | PASS ×2 | 4.85 / 3.69 | 25 / 4 |
| inst_p5（乐器模式，第 1 / 2 轮） | 3 | 3.342 / 3.333 | 0.312 / 0.293 | 1286 / 1480 | 0 / 0 | PASS ×2 | 4.40 / 4.85 | 0 / 2 |
| p3spin（Phase 3 式协议） | 4 / 6 | 5.457 / 4.651 | 0.435 / 0.375 | 1702 / 1420 | 0 / 0 | PASS ×2 | 4.82 / 3.83 | 0 / 0 |
| p4_default（Phase 4 插件默认） | 4 / 5 | 16.441 / 15.662 | 1.230 / 1.116 | 4389 / 3926 | 247 / 114 | FAIL ×2 | 4.66 / 1.84 | 1900 / 1643 |

**结论**
1. **专家分诊在 i9 上得到证实**：瓶颈首先是内核，不是线程。模板 SIMD 内核把 i9 单线程从 26.6 降到 4.67 µs/样本（**5.7 倍**，box 上是 4.0 倍），1 线程即 PASS（p99/预算 0.367，0 超时）。Phase 4 i9 复测时 32×32 没有任何配置 PASS，现在除 p4_default 外全部 PASS。
2. **Phase 4 协议本身就是问题**：同一新内核下，p4_default（专用池 + 混合屏障 + CSR）仍 FAIL，bOSw 1643–1900；纯自旋协议 bOSw = 0。
3. **2 线程就够**：插件默认协议 2T / 3T / 4T 的 p99/预算几乎相同（0.301 / 0.305 / 0.308），多出的线程只降中位数、不降尾部，频率估计随线程数下降（4.71 → 3.88 GHz，睿频随活动核数下调）。因此 **Phase 5b 把 L32 Auto 从 3 改为 2**（宿主线程 + 1 个工作线程，CPU Sets 模式）；1 线程仍可选（i9 上已 PASS）。L16 Auto 保持 1。HQ 势的 i9 数据出来前不再加线程（box 参考见 §11.5）。
4. 宿主线程也有 MMCSS 时（真实 DAW 的音频线程通常如此），p99/预算再降到 0.21–0.23；4T 出现 1 次 15 ms 尖峰（bOSw 42），说明 4 线程开始与系统争核。
5. 本次只有 1 轮完整数据，重复性要等补测（§11.6 的 `-Resume` 命令）。

### 11.2 专用池卡死：根因与修复
- 位置：`EsavLFMT::process_block` 的专用池分支，调用方线程等待工作线程 t0 发布 `done_ = s`。旧代码：`while (done_.load() != s) hybrid_wait(done_, done_.load(), ...)`——**读了两次** `done_`。
- 竞态：t0 恰好在两次读之间写入 `s`，第二次读到的就是 `s`，于是调用方调用 `hybrid_wait(done_, s)`，意思是“等到 `done_` 不再等于 `s`”。而 `done_` 下次改变要等下一块，下一块又要等调用方返回，于是所有线程都睡在 `WaitOnAddress`/futex 上：**0 CPU 的死锁**，与 i9 上 16 分钟无输出的现象一致。`std::atomic::wait(old)` 的语义（Windows `WaitOnAddress` 与 Linux futex 一样）是“值 ≠ old 才返回、可伪唤醒”，所以比较值必须是调用方刚刚看到的那个值。
- 为什么 Phase 4 没暴露：这段代码自 Phase 4 就存在（`include_phase4` 第 196 行），但窗口只有几纳秒。Phase 5 给工作线程加了 MMCSS，`notify_all` 唤醒的高优先级工作线程可以在 `notify` 返回后立刻抢占普通优先级的调用方，正好落在两次读之间；块越短、唤醒越频繁越容易触发（第 1 轮 3T/4T 正常，第 2 轮 3T 后卡死）。
- 修复：只读一次，用读到的值作比较值：`for (d = done_.load(acquire); d != s; d = done_.load(acquire)) hybrid_wait(done_, d, ...)`。工作线程的 `seen` 从 0 开始（不再在启动时读 `seq_`，避免启动与第一块之间的同类窗口）。`stop_workers` 的顺序（先 `quit_` seq_cst，再 `seq_++`，再 `notify_all`）与睡眠计数器的 Dekker 配对经核查正确。
- 调用方计算池（插件默认）审计：它不等待 `done_`，只走块内纯自旋屏障与空闲等待；空闲等待用 `seen` 作比较值、屏障先读代号再 `fetch_add`，均无此竞态。唯一风险点是工作线程 `seen` 的初始化，已一并修正。
- 复现：在两次读之间注入 300 µs 睡眠（旧代码），压力测试 0 块即卡死；按新的插桩位置用旧等待逻辑，170 块后卡死；修复后同样的注入下不再卡死。
- 压力测试 `tests/test_phase5_stress.cpp`（Windows 上为 `morpho_stress.exe`）：P1 专用池极小块（1–8 样本）、P2 调用方计算池极小块、P3 反复 `prepare()` 重配（1–6 线程、caller/dedicated/auto、无绑定/CPU Sets、空闲自旋 0–50 µs 或 3.5 ms，且要求 ≥ 1000 次启停）、P4 类插件 32×32 块 128 切换线程数；看门狗 10 s 无进展即打印状态并以退出码 2 结束。`-DSTRESS_HOOKS` 版本在 6 个竞态点随机插入睡眠 / yield。box 结果（3 个种子，普通版 30 s/部分、插桩版 20 s/部分）全部通过，见 §11.3。

### 11.3 压力测试结果（box）
**线程池压力测试**（`tests/test_phase5_stress.cpp`，结果 `experiments/results/phase5/tests/test_phase5_stress*_s{1,2,3}.txt`，3 个种子合计）：

| 版本 | P1 专用池极小块 | P2 调用方计算池极小块 | P3 重配启停 | P4 类插件切换 | 结果 |
|---|---|---|---|---|---|
| 普通（30 s/部分 × 3） | 6,106,233 块 | 5,767,036 块 | 73,061 次（1–6 线程） | 499 次 | 全部 PASS，无卡死 |
| 插桩（20 s/部分 × 3，6 个竞态点随机睡眠 / yield，共注入 121,154 次睡眠） | 1,813,001 块 | 2,710,530 块 | 45,492 次 | 312 次 | 全部 PASS，无卡死 |

旧等待逻辑在同样的注入下 0 块（300 µs 注入）或 170 块后卡死（`experiments/results/phase5/tests/hang_repro.md`）。

### 11.4 默认值
- L32 Auto = **2 线程**（宿主线程计算一份 + 1 个工作线程，CPU Sets 模式，MMCSS，块内纯自旋，块间空闲自旋 3.5 ms）。理由：i9 上 2T / 3T / 4T 的 p99/预算 0.301 / 0.305 / 0.308，第三个线程不改善尾部，却多占一个 P 核、拉低睿频（4.71 → 4.32 GHz），多实例时代价更大。只有 HQ 数据要求时才升降。
- 1 线程保持可选（i9 1T PASS，p99/预算 0.367）。L16 Auto = 1 不变。
- 代码：`rt::default_threads()`（`include/morpho_phs/rt_thread.hpp`）；测试期望（test_phase4 Q4b、test_phase5 S5）同步改为 2。

### 11.5 HQ 势与 L16（box 参考）
**HQ 势与 L16 参考**（box，块 128，5 s/配置，调用方计算 + CPU Sets，单次运行，`experiments/results/phase5/box_hq/`；box 是共享虚拟机，只作方向参考，结论以 i9 为准）：

| 配置 | 1T µs/样本（p99/预算） | 2T | 3T |
|---|---|---|---|
| L32 四次势（quartic） | 5.42（0.36）PASS | 3.77（0.26）NEAR（1 次 19 ms 尖峰） | 3.33（0.27）PASS |
| L32 HQ-fast（quartic + 快速 logcosh） | 9.52（0.58）FAIL（2 超时） | 5.93（0.45）NEAR | 5.20（0.58）FAIL（12 超时，VM 抢占） |
| L32 HQ-exact（quartic + 精确 logcosh） | 37.6（2.56）FAIL | 21.4（1.34）FAIL | 15.4（1.03）FAIL |
| L16 四次势 | 1.48（0.12）PASS | 1.51（0.13）PASS | — |
| L16 HQ-exact | 9.54（0.59）PASS | 6.38（0.39）PASS | — |

要点：HQ-exact 在 L32 上 3 线程以内不可能实时（单线程比四次势慢约 7 倍）；HQ-fast 的 2T 尚有余量。L16 单线程足够（四次势与 HQ-exact 都 PASS），L16 Auto = 1 保持不变。是否为 HQ 势单独提高 Auto 线程数，等 i9 的 hqf_L32 / hq_L32 行再定。

### 11.6 基准脚本加固与 i9 补测命令
- 每次调用 morpho_cli 限时：3 × 预期时长（预期 = 行数 × (每配置秒数 + 3 s) + 5 s），至少 60 s（`-TimeoutFactor`、`-MinTimeout`）。超时后杀进程，runlog 记 `exit=TIMEOUT`，JSON 里追加一行 `verdict=TIMEOUT`，然后继续下一个变体。
- 非零退出码不再 `throw`：记 `FAILED` 行，继续。所有失败 / 超时汇总在 SUMMARY.txt 的 “Failures / timeouts”。
- morpho_cli 每完成一行就重写 JSON / TXT（`complete` 字段在最后一行后为 true），所以超时被杀时已完成的行不丢。
- `-Resume <结果文件夹>`：读取该文件夹的 runlog.csv，`exit=0` 的（变体, 重复）跳过，其余（缺失、TIMEOUT、失败）重跑；与 `-Variants` 可组合。
- 新变体：`hqf_L32`、`hq_L32`（1/2/3T，`--potential 1/2`）、`L16`、`L16_hq`（1/2T，`--sizes 16`）；`inst_p5` 改为 2/3T。
- 验证（Linux pwsh 7，WMI/powercfg 打桩）：语法检查 0 错误；用一个故意卡死（L16_hq）和故意崩溃（1T_csr，退出码 3）的假 morpho_cli 干跑：卡死的被 13 s 超时杀掉并记 TIMEOUT，崩溃的记 FAILED，其余变体照常完成；随后 `-Resume` 只重跑了这 3 个；真实 morpho_cli 在第 2 行被超时杀掉时，第 1 行保留在 JSON 中。
- `build_windows.ps1` 额外构建 `morpho_stress.exe`（失败只警告，不影响插件）。

i9 补测命令（PowerShell，在项目根目录；先解压同步包覆盖）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\windows\build_windows.ps1 -VsPath C:\SDKs\AetherBuildTools -Generator Ninja
dist\windows\morpho_stress.exe 15 1
powershell -ExecutionPolicy Bypass -File scripts\windows\bench_windows.ps1 -Resume experiments\results\phase5\windows\DESKTOP-114514_20261031_141513 -Repeats 3
```

第三行跳过已完成的 10 次运行，补跑原 9 个变体缺的 17 次（第 2 轮 8 次含卡死的专用池，第 3 轮 9 次），再加 4 个新变体 × 3 轮 = 12 次，共 29 次运行、66 个配置；估算约 23 分钟（每配置 10 s + 约 1 s 开销，每次运行前冷却 20 s）。

### 11.7 验证
- 测试：test_main、test_mt、test_phase3、test_phase4、test_phase5 全部通过；test_phase5 第一次运行 S4 失败 1 项（调用方计算模式 2–4 次 1 ms 自救），原因是同时在跑基准脚本干跑（box 上 CPU 争用），机器空闲后单独重跑 12/12 PASS（`experiments/results/phase5/tests/test_phase5.txt`）。
- 压力测试：见 §11.3。
- pluginval strictness 10（Linux、Xvfb、in-process）：MORPHO-PHS 与 MORPHO-PHS FX 都 SUCCESS（`experiments/results/phase5/pluginval/`）。
- zig mingw 交叉编译（`-target x86_64-windows-gnu -mcpu=x86_64_v3 -lavrt`）：test_phase5.exe、test_phase4.exe、test_phase5_stress.exe、test_phase5_stress_hooks.exe 全部通过（`experiments/results/phase5/wincheck/zig_windows_check.txt`）。MSVC 版 `morpho_stress` 没在 box 上编译过（box 没有 MSVC），所以构建脚本对它失败只警告。

### 11.8 风险与未决
- i9 只有 1 轮完整数据；补测后才能确认重复性和 2T 默认值。
- 卡死的根因由代码分析与插桩复现确定；i9 上的触发机制（MMCSS 工作线程抢占调用方）是推测，与现象一致，未在 i9 上直接观测。
- HQ-exact 在 L32 上 3 线程以内不可能实时（box）；如果 i9 也如此，界面上应提示“HQ-exact + 32×32 需要更大缓冲或离线渲染”。

## 12. Phase 5c：i9 最终数据、默认值与收尾

数据：用户 15:18–15:43 用 `-Resume` 补测后的 `experiments/results/phase5/windows/DESKTOP-114514_20261031_141513/`（3 轮齐全）与 `windows/logs/`。完整分析：`experiments/results/phase5/windows/i9_final_analysis.md`。所有工作仍只在 box 上完成。

### 12.1 i9 汇总（3 次重复）

| 变体 | L | 线程 | µs/样本（3 轮范围） | p99/预算 最差 | 超时 / 11250 | 最大块 µs | 3 轮结论 | bOSw 合计 |
|---|---|---|---|---|---|---|---|---|
| p5_caller_cpuset（插件默认协议，FX 四次势） | 32 | 2 | 3.48–3.56 | **0.310** | **0** | 1737 | PASS/PASS/PASS | 0 |
| 同上 | 32 | 3 | 2.96–3.26 | 0.408 | 0 | 1555 | PASS/PASS/PASS | 0 |
| 同上 | 32 | 4 | 2.81–3.44 | 0.491 | 2 | 5310 | PASS/PASS/NEAR | 8 |
| p5_caller_cpuset_host（宿主线程也进 CPU Set + MMCSS） | 32 | 2 / 3 / 4 | 3.41–3.77 / 2.57–3.19 / 2.61–3.23 | 0.309 / 0.318 / 0.574 | 1 / 9 / 10 | 3160 / 17352 / 15700 | 2T PASS/NEAR/PASS；3T PASS/FAIL/NEAR；4T NEAR/PASS/FAIL | 3 / 103 / 172 |
| p5_caller_pin（硬绑 P 核） | 32 | 2 / 3 / 4 | 3.50–3.90 / 3.02–3.71 / 3.32–5.42 | 0.450 / 0.488 / 0.621 | 2 / 0 / 9 | 3393 / 2283 / 3842 | 3T PASS×3，其余有 NEAR/FAIL | 0 / 2 / 32 |
| p5_dedicated_cpuset（专用池，非默认） | 32 | 3 / 4 | 3.01–3.23 / 2.88–3.16 | 0.513 / 1.465 | 22 / 60 | 4130 / 10039 | 不再卡死；PASS/FAIL/FAIL、PASS/FAIL/NEAR | 191 / 485 |
| 1T（模板内核） | 32 | 1 | 4.67–5.14 | 0.501 | 1 | 3752 | PASS/NEAR/PASS | 0 |
| 1T_csr（Phase 2–4 内核） | 32 | 1 | 26.22–26.96 | 1.833 | 11250 | 11290 | FAIL×3 | 0 |
| inst_p5（乐器模式） | 32 | 3 | 3.33–3.67 | **0.480** | **0** | 1810 | PASS/PASS/PASS | 2 |
| 同上 | 32 | 2 | 4.23（只测到 1 轮） | 0.626 | 0 / 3750 | 2282 | PASS（1 轮） | 1 |
| hqf_L32（HQ-fast） | 32 | 1 | 9.50–11.98 | 1.560 | 436 | 7470 | FAIL×3 | 0 |
| 同上 | 32 | 2 | 5.62–7.37 | 0.887 | 31 | 4458 | NEAR/FAIL/FAIL | 1 |
| 同上 | 32 | 3 | 4.60–4.89 | 0.757 | 1 | 2917 | FAIL/FAIL/PASS（第 1、2 轮 p99/预算 0.757、0.726 ≥ 0.7） | 2 |
| hq_L32（HQ-exact） | 32 | 1 / 2 / 3 | 54.0–57.8 / 31.7–34.5 / 24.1–26.6 | 5.009 / 2.901 / 2.300 | 全部超时 | 20007 / 11358 / 8998 | FAIL×9 | 0 / 3 / 6 |
| L16（四次势） | 16 | 1 / 2 | 1.61–1.66 / 1.35–1.36 | 0.145 / 0.177 | 0 / 0 | 1544 / 1940 | PASS×6 | 0 |
| L16_hq（HQ-exact） | 16 | 1 | 13.27–15.20 | 1.133 | 141 | 4726 | FAIL×3 | 0 |
| 同上 | 16 | 2 | 7.78–8.67 | 0.875 | 12 | 5053 | FAIL/FAIL/PASS | 0 |
| p3spin（Phase 3 式协议） | 32 | 4 / 6 | 4.90–5.46 / 4.65–5.16 | 0.468 / 0.478 | 0 / 1 | 2542 / 2667 | PASS×3；PASS/NEAR/PASS | 0 / 9 |
| p4_default（Phase 4 插件默认） | 32 | 4 / 5 | 16.4–18.8 / 15.7–16.8 | 1.305 / 1.200 | 1830 / 621 | 4705 / 4172 | FAIL×6 | 4226 / 4341 |

### 12.2 结论与默认值
1. **32×32 实时在 i9 上达成**：插件默认协议（调用方计算 + 干净 P 核 CPU Sets + 块内纯自旋）FX 四次势 2T 三轮 0/11250 超时、p99/预算最差 0.310；3T 0 超时、0.408。乐器模式 3T 0/11250、0.480。
2. **线程数**：2T 的尾部最好；4T 开始出现超时（2 次，bOSw 8）。宿主线程自身也在 CPU Set + MMCSS 时 3–4T 出现 15–17 ms 尖峰（bOSw 103–172），说明线程越多越容易与系统 / 宿主争核。→ FX L32 Auto = 2。
3. **乐器模式**：3T 稳定；2T 只测到 1 轮（0.626，离 0.7 只有 10% 余量）。→ 乐器 L32 Auto = 3。
4. **HQ-fast**：3T 是唯一接近达标的设置（3 轮共 1 次超时，p99/预算 0.543–0.757，两轮超过 0.7）；2T 31 次超时。→ HQ-fast L32 Auto = 3，并在界面标“High CPU”。余量不足，属于“可用但不保证”。
5. **HQ-exact**：L32 在 1/2/3T 全部失败（24–58 µs/样本，超过预算），不可能靠加线程解决。L16 1T 失败（13–15 µs/样本），2T 接近（12 次超时，最差 0.875）。→ HQ-exact 只允许 L ≤ 16；L32 时按 HQ-fast 运行并在界面提示；L16 HQ-exact Auto = 2，同样标“High CPU”。
6. **专用池**：卡死已修复（3 轮都跑完），但 22–60 次超时、bOSw 191–485（工作线程完成后调用方要被唤醒）→ 保持非默认。
7. **Phase 4 默认协议**仍然 FAIL（621–1830 次超时，bOSw 4000+），确认 Phase 5 协议改动是必要的。
8. 内核加速稳定：1T_csr / 1T = 26.6 / 4.8 ≈ 5.5 倍（三轮）。

实现（Phase 5c）：
- `rt::default_threads(L, topo, AutoLoad{instrument, hq, hq_exact})`：L ≤ 8 → 1；L ≤ 16 → 1（HQ-exact 为 2）；L32 → 2，乐器或任一 HQ 势 → 3；以干净 P 核数为上限。旧的两参数版本（FX）保留。
- `MorphoEngine.hpp`：`kHqExactMaxL = 16`、`effective_potential_choice()`（L > 16 时 HQ-exact → HQ-fast，基准可用 `allow_hq_exact_any_L` 覆盖）、`high_cpu_setting()`、`auto_load()`；`threads_from_choice(c, L, mode, potential)`。引擎构造与 `control_tick` 的实时势切换都用有效势。
- `EngineHost`：Auto 线程数随模式 / 势变化时按原方式在消息线程重建（256 样本交叉淡化）。
- 编辑器：POTENTIAL 标题 “HQ FAST @32” / “HIGH CPU”（暖色）、提示文字说明 CPU 代价；THREADS 标题显示 Auto 取值；状态栏提示。选择 HQ-exact 的参数值不变，所以预设、自动化与宿主状态不受影响。不禁用该选项的理由：选项禁用后，在 16×16 保存的 HQ-exact 工程切换到 32×32 时会变成无效值；回退更干净，且用户能看到提示。
- `PresetIO`：32×32 + HQ-exact 的预设给出警告（不是错误）；27 个工厂预设全部通过校验，没有一个使用 HQ-exact。
- CLI：`--hq-exact-any-L 0|1`；Auto（`--threads 0`）按模式 / 势选；表头打印各模式的 Auto 线程数与有效势。`bench_windows.ps1` 的 hq_L32 变体加了 `--hq-exact-any-L 1`，否则 Phase 5c 之后会悄悄测成 HQ-fast。

### 12.3 PL1 / 热降频假设
可用数据：每行开始前 20 ms 的进程内频率估计 `clkGHz`（调用方线程上的依赖浮点加法链；它测的是**调用方线程实际得到的吞吐**，同核 SMT 兄弟或时间片竞争也会让它变低，并不是纯频率）；`runlog.csv` 每次运行前后的 WMI `CurrentClockSpeed`（粗粒度）。**没有**封装功耗、PL1/PL2 状态与温度记录。

| 检验 | 结果 |
|---|---|
| clkGHz 与线程数 | 中位数 1T 4.65、2T 4.62、3T 4.11、4T 3.88、5T 1.84（p4_default，工作线程在自旋）、6T 2.53 GHz。第 1 轮最开始几行就已如此（14:15 p3spin 6T 3.83，14:17 p5 4T 3.88），不是累积发热 → 符合“睿频按活动核数下调 / 调用方与自旋线程争核”，不是 PL1 时间窗耗尽 |
| clkGHz 随会话时间的趋势（Spearman ρ） | 1T +0.13（n=18）、2T +0.11（n=22）、3T −0.40（n=21）。1–2T 无下降趋势；3T 有中等负相关，但幅度主要来自个别行（p5_caller_pin 3T 第 3 轮 2.91） |
| WMI 频率 | 前 13 次运行前中位数 3094 MHz，最后 13 次 3068 MHz；无趋势 |
| 同配置内 clkGHz 偏差与尾部偏差（84 行，Spearman ρ） | 与 p99/预算 −0.10，与超时数 −0.14，与 µs/样本 −0.26。频率低时中位数略慢（预期之内），但与尾部、超时几乎无关 |
| 重负载之后 | hq_L32（约 70 s 满载）之后的下一个变体有时偏低（第 2 轮 L16 3.72 vs 平常 4.7 GHz），符合短时功耗预算（PL2→PL1）被用掉；但第 1 轮紧跟其后的 L16_hq 为 4.56 GHz（正常），第 3 轮紧跟其后的 p4_default 4T 只比另两轮低约 0.4 GHz（4.31 vs 4.66/4.71）；第 2 轮那次 L16 在 3.72 GHz 下照样 0 超时 PASS |
| 超时最多的行 | hqf_L32 1T 第 2 轮 429 次超时时 clkGHz = 4.86（全速）；HQ-exact 在全速时也全部超时 → 超时由计算量 / 调度决定，不是频率 |

**结论**：数据**不支持**“PL1 / 热降频是尾部超时主因”。频率随线程数下降从第一分钟就存在，会话内无累积下降，频率偏差与超时基本无关。能看到的只有重负载后的单次短时降频，影响中位数而不是成败。由于没有功耗与温度遥测，不能完全排除 PL1 对长时间满载（多实例、HQ-exact）的影响；若要定论，需要用 HWiNFO 或 Intel PCM 同时记录封装功耗、PL1/PL2 限制标志与温度（列为 Phase 6 测量项）。“MMCSS 变慢”本身在 Phase 5 数据中也不成立：所有 Phase 5 变体都开 MMCSS，且都 PASS。

### 12.4 压力测试 P3 判据
| 部分 | 结果 |
|---|---|
| P1 专用池极小块 | 457,575 块 / 23 种配置，27,788 次 OS 等待，无卡死，PASS |
| P2 调用方计算池极小块 | 435,525 块 / 22 种配置，14,799 次 OS 等待，无卡死，PASS |
| P3 重配启停 | 256 次启停 / 15 s，1435 块，**无卡死**；按旧判据（≥ 1000 次）判 FAIL |
| P4 类插件 32×32 切换 | 20 次切换、1000 块，无卡死，PASS |

P3 平均每次启停循环约 **58.6 ms**（15 s / 256，含线程创建 + MMCSS 注册 + 少量块与随机间隔；box Linux 上约 1.2 ms）。这是 Windows 建线程与 `AvSetMmThreadCharacteristicsW` 的成本，不是卡死。Phase 5c 改判据：无卡死（看门狗不变）+ 至少 min(100, 10 × 每部分秒数) 次循环 + 单次循环 < 2 s，并输出每次循环与 `prepare()`（停 + 重启）的中位数 / p95 / 最大值；按新判据 i9 的 256 次（需要 100 次）通过。插件里一次重建（改晶格尺寸 / 线程数）在消息线程上，约 60 ms 的成本不影响音频线程。

判据选择：曾考虑“循环数 ≥ 每部分秒数 / 中位数的一半”，但每次循环的成本随线程数与池类型呈多峰分布（box 上中位数 0.6 ms、p95 4.4 ms），中位数会把 i9 误判，所以改为“覆盖下限 + 单次循环 2 s 上限”。看门狗（10 s 无进展 → 退出码 2）不变，卡死检测没有放宽。

### 12.5 验证（box）
- 测试（box，Phase 5c 代码）：test_main、test_mt、test_phase3、test_phase4、test_phase5 全部通过（test_phase5 含新的 S5c：i9 拓扑下 L32 FX 2 / 乐器 3 / HQ-fast 3 / HQ-exact→fast 3、L16 1、L16 HQ-exact 2、L8 HQ-exact 1、单 P 核机器 1，HQ-exact 限制与基准覆盖；S4 纯自旋 bOSw 0）。结果在 `experiments/results/phase5/tests/`。
- 压力测试（box）：普通版 3 个种子 × 30 s/部分：P1 6,004,958 块、P2 5,689,413 块、P3 68,981 次启停、P4 453 次切换；插桩版 3 × 20 s/部分（注入 122,824 次睡眠）：P1 1,832,046、P2 2,740,259、P3 48,603、P4 308；全部 PASS，无卡死。每次启停循环中位数 0.59–0.64 ms、p95 4.3–4.7 ms、最大 33–109 ms；`prepare()`（停 + 重启）中位数 0.15–0.16 ms、p95 0.51–0.54 ms、最大 3.9–19.5 ms（box 是 VM，最大值含抢占）。
- 预设：`morpho_cli validate presets` 27/27 通过；临时 32×32 + HQ-exact 预设给出警告、不报错。
- 插件重建 BUILD_EXIT 0；pluginval strictness 10（Linux、Xvfb、in-process）：MORPHO-PHS 与 MORPHO-PHS FX 都 SUCCESS（`experiments/results/phase5/pluginval/`）。
- zig mingw 交叉编译（`-target x86_64-windows-gnu -mcpu=x86_64_v3 -lavrt`）：test_phase5、test_phase4、test_phase5_stress、test_phase5_stress_hooks 全部 EXIT 0（`experiments/results/phase5/wincheck/zig_windows_check.txt`）。
- `bench_windows.ps1` 仍为纯 ASCII，pwsh 解析 0 错误。MSVC 构建只能在 i9 上做。

### 12.6 风险与未决
- HQ-exact 32×32：限制而非解决；需要更便宜的凸近似（Padé / 有理近似）或 float32 + 多速率（Phase 6）。
- HQ-fast 32×32 3T：p99/预算 0.757（两轮 ≥ 0.7），标“High CPU”，可用但不保证；乐器 2T 只测到 1 轮（0.626），所以乐器取 3T。
- L16 HQ-exact 2T：12/11250 次超时，接近实时。
- 多实例 CPU 占用、真实 DAW、Windows 上 pluginval / clap-validator 尚未做；功耗 / 温度遥测缺失。
- Phase 5c 的默认值改动（Auto 线程数、HQ-exact 回退、界面）只在 box 上验证，用户需要在 i9 上重新构建；建议重新运行 `morpho_stress.exe 15 1` 确认新 P3 判据。
- Phase 6 候选：float32 更激进路径、多速率晶格、SSB 外差带隙注入、Padé 近似 HQ 势、E-RSAV 预算版、多实例 CPU 占用、功耗遥测、DAW 实机测试（README §9.0）。
