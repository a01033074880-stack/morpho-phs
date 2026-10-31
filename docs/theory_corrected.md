# Morphohedra / NR-PHMS — 修正后的理论表述（Phase 0）

> 本文件给出 Phase 0 审计后**可以被严格证明/数值验证**的模型与离散格式。所有数值均可由 `python/scripts/*` 复现，结果文件见 `experiments/results/*.json`。未经证明的部分明确标注为“猜想/待证”。

## 1. 连续模型（端口哈密顿形式）

格点上 $N$ 个自由度，$q\in\mathbb R^N$ 位移，$M=\mathrm{diag}(m_i)$，速度 $v=\dot q$：

$$
M\ddot q = -Kq-\nabla U(q)+G\dot q - D(\dot q)\,\dot q + f,\qquad
U(q)=\sum_i \tfrac{\alpha_i}{4}q_i^4+\beta_i\ln\cosh q_i,
$$

$$
H(q,p)=\tfrac12 p^\top M^{-1}p+\tfrac12 q^\top Kq+U(q),\quad p=M\dot q,\qquad
D=\mathrm{diag}(d_i),\ d_i=-\mu_i+\eta_i v_i^2 .
$$

$K\succ0$（含接地弹簧 $k_0$），$G=-G^\top$。功率平衡：

$$\dot H = -\dot q^\top D\dot q + \dot q^\top f \;-\;\underbrace{\tfrac12\,\dot q^\top \dot M\,\dot q\;+\;\partial_t U}_{\text{参数调制所做的功}} .$$

**修正 1（被动性）**：原文“$\mu_i\le0\Rightarrow$ 严格无源”只在**参数冻结**时成立。若 $m_i(t),\alpha_i(t)$ 随形态场变化，$\partial_t H\neq0$ 构成一个额外的功率端口，可通过参数共振（Mathieu 型）注入能量。数值上（`e_stress_test.py`，pump_lossless 场景）任何精确积分器（RK4、隐式中点-dG）都观测到能量增长至初值的约 2.4–2.6 倍。正确表述：系统对 $(f,\dot q)$ 端口**在参数冻结时**无源；参数更新必须单独设计为不增能（见 §5.4 ENPU）。

## 2. 拓扑：什么情况下才有非零 Chern 数

一阶化：$\dot z=\mathcal A z$，$z=(q,p)$，$J=\begin{pmatrix}0&I\\-I&G\end{pmatrix}$，$H_m=\nabla^2H_{\rm lin}=\mathrm{blkdiag}(K,M^{-1})$，$\mathcal A=JH_m=\begin{pmatrix}0&M^{-1}\\-K&GM^{-1}\end{pmatrix}$。频率由厄米矩阵 $\mathcal L(k)=i\,H_m^{1/2}JH_m^{1/2}$ 给出（$H_m=\nabla^2H_{\rm lin}$），Chern 数用 Fukui–Hatsugai–Suzuki 格点法计算。

**命题 2.1（原文模型 Chern 数恒为 0）**：原文为每格点 1 个标量自由度、每原胞 1 个格点的正方晶格。Bloch 形式下 $\mathcal A(k)=\begin{pmatrix}0&1/m\\-\kappa(k)&\hat g(k)/m\end{pmatrix}$，$\kappa(k)\ge k_0>0$，$\hat g(k)=2i\sigma\sin\Phi(\sin k_x+\sin k_y)$ 纯虚。特征向量可写为 $(1,\,-i\omega(k) m)^\top$（$q$ 分量恒为 1，处处非零），因此正频带存在**全局光滑规范**，Berry 联络在整个布里渊区上光滑周期，$C=0$。$\sin\Phi$ 只产生 $\omega(k)\neq\omega(-k)$ 的非互易色散（$\omega=\tfrac{i\hat g}{2m}\pm\sqrt{\kappa/m-(\hat g/2m)^2}$ 的实部移动），**不打开带隙，不存在手性边缘态**。数值（FHS 法，所有 $\Phi$）亦为 $C=0$（`a_topology.json`）；16×16 开边界谱最大相对能级间距约 3%，无隙。

> 注：$\sigma_{ij}\sin\Phi$ 也不是 Peierls 规范磁通（它是速度耦合的实数振幅，而非跃迁的复相位）。

**可行替代 A（标量蜂窝 + Haldane 型次近邻回旋耦合）**：A/B 两子格，NNN 回旋耦合在 A、B 上取**相反符号**（$\pm\sigma_2$）。数值：$\sigma_2=0.15$ 时 $C=(+1,-1)$，全带隙 0.566（单位 $\sqrt{k_c/m}$）；同号时无隙、$C=0$。带隙随 $|\sin\Phi|$ 增长并在 0.566 饱和，$\mathrm{sign}\,C=\mathrm{sign}\sin\Phi$。

**可行替代 B（2 自由度陀螺蜂窝，Nash 2015 / Wang–Lu–Bertoldi 2015 型）**：每格点 $(x,y)$ 两个面内位移 + 格点内陀螺项 $\Omega\,\hat z\times\dot u$。$\Omega=0.6$：$C=[1,0,0,-1]$，隙宽 $[0.416,0.476,0.255]$。时域有限片中观察到绕过刚性缺陷的单向边缘传输，方向随 $\Omega$ 符号翻转（图 `a3`, `a4`）。**优点**：$G$ 为格点内 $2\times2$ 块对角，离散格式中可逐点精确求解（§5.3），无需迭代。

## 3. Rayleigh 子流的精确解（稳定形式）

子流 $\dot p=(\mu-\eta p^2/m^2)\,p/m$。令 $w=p^{-2}$：

$$\dot w = -\frac{2\mu}{m}w+\frac{2\eta}{m^3}\quad\Longrightarrow\quad
w(t)=e^{-z}w_0+\frac{2\eta t}{m^3}\,\varphi_1(-z),\qquad z=\frac{2\mu t}{m},\ \ \varphi_1(x)=\frac{e^x-1}{x}.$$

$p(t)=\mathrm{sign}(p_0)\,w(t)^{-1/2}$，$p_0=0\Rightarrow p\equiv0$。该式对 $\mu<0,\ \mu=0,\ \mu>0$ 统一成立（$\mu\to0$：$p=p_0m^{3/2}/\sqrt{m^3+2\eta p_0^2t}$），无相消、无上溢（$e^{-z}$ 在 $\mu>0$ 时有界；$\mu<0$ 时用 $w$ 而非 $p$ 递推）。sympy 残差为 0；$10^6$ 组随机参数单调有界检验全部通过，最大相对误差 $1.6\times10^{-16}$。原文印刷公式在 $\mu=0$ 处 0/0（NaN），$\mu\approx0$ 相对误差可达 $2.8\times10^{-3}$，大步长溢出为 NaN，随机检验 9172/10⁶ 非有限。

**修正 2**：子流不动点 $|p|^*=m\sqrt{\mu/\eta}$ **不是**整个振子的极限环半径。对耦合振子 $\ddot q+\omega^2q=(\mu-\eta\dot q^2)\dot q$（单位质量），一阶平均给出速度幅值 $\sqrt{4\mu/(3\eta)}$，数值吻合到 $10^{-5}$ 量级（`b_bernoulli.json`）。

## 4. Cayley 步：H-正交而非欧氏正交

对 $\mathcal A=JH_m$（$J^\top=-J$, $H_m\succ0$），$Q=(I-\tfrac h2\mathcal A)^{-1}(I+\tfrac h2\mathcal A)$ 满足
$$Q^\top H_m Q=H_m,$$
即保持二次能量 $z^\top H_mz$，**但 $Q^\top Q\neq I$**。8×8 实测：$\|Qz\|_2/\|z\|_2\in[3.1\times10^3,6.3\times10^3]$（因 $q,p$ 量纲不同），$\|Q^\top H_mQ-H_m\|/\|H_m\|=1.4\times10^{-14}$。$Q$ 是稠密的（填充率 61%）：

| L | 稠密 $Q$ flop/样本 | 稀疏 LU flop/样本 | ESAV 模板 flop/样本 |
|---|---|---|---|
| 8 | 65 536 | 15 716 | ≈12 900 |
| 16 | 1 048 576 | 106 724 | ≈51 700 |
| 32 | 16 777 216 | 676 396 | ≈206 800 |

“FFT 块对角化 $O(N)$”只在**空间均匀参数 + 周期边界**时成立（且 FFT 本身是 $O(N\log N)$）；一旦 $m_i,\alpha_i$ 由形态场逐点调制，平移不变性被破坏。$m$ 每变一次就必须重新分解（750 Hz 控制率下每秒 750 次；SuperLU 16×16 一次约 5 ms（Python 调用），即 >100% 单核）。

## 5. 修正格式：ESAV-LF（显式标量辅助变量蛙跳 + 中心化回旋/阻尼）

思路与 Bilbao–Ducceschi–Zama (JCP 2023) 的显式精确能量守恒格式、Ducceschi–Bilbao 的能量二次化（IEQ/SAV）一致；**本节不是新方法**，而是将其适配到本模型（回旋项、Rayleigh 有源项、参数调制）。

### 5.1 格式
记 $w^n=q^{n+1}-q^{n-1}$，$g^n=\nabla U(q^n)/\sqrt{2U(q^n)+c_0}$（逐元素），辅助变量 $\psi\approx\sqrt{2U+c_0}$：

$$
M\frac{q^{n+1}-2q^n+q^{n-1}}{h^2}=-Kq^n-g^n\odot\frac{\psi^{n+1}+\psi^{n-1}}2+G\frac{w^n}{2h}-D^n\frac{w^n}{2h}+f^n,
\qquad \psi^{n+1}=\psi^{n-1}+g^n\odot w^n,
$$
$D^n=\mathrm{diag}(-\mu_i+\eta_i (v^{n-1/2}_i)^2)$，$v^{n-1/2}=(q^n-q^{n-1})/h$（滞后，因此 $D^n$ 显式已知）。

消去 $\psi^{n+1}$ 得对 $w^n$ 的线性方程
$$\Big(\tfrac{M}{h^2}+\tfrac12\mathrm{diag}(g^n)^2+\tfrac{D^n}{2h}-\tfrac{G}{2h}\Big)w^n=\tfrac{2M}{h^2}(q^n-q^{n-1})-Kq^n-g^n\odot\psi^{n-1}+f^n .$$
左端 = 对角 $-\ G/(2h)$。$G=0$ 时完全显式；$G$ 为格点内 $2\times2$ 块（替代 B）时逐点 $2\times2$ 精确求解；$G$ 跨格点（原文/替代 A）时用 Jacobi 迭代，收缩因子约 $\rho\approx h\|G\|_\infty/(2m)$，本参数下 $\rho\approx0.16$，15 次扫描到 $10^{-13}$。

### 5.2 离散能量与功率平衡（定理，可直接验证）
$$E^{n+1/2}=\tfrac12(\delta_{t+}q^n)^\top M\,\delta_{t+}q^n+\tfrac12 (q^{n+1})^\top Kq^n+\tfrac14\big(\|\psi^{n+1}\|^2+\|\psi^n\|^2\big)-\tfrac{Nc_0}2,$$
$$\frac{E^{n+1/2}-E^{n-1/2}}h=-(\delta_{t\cdot}q^n)^\top D^n\,\delta_{t\cdot}q^n+(\delta_{t\cdot}q^n)^\top f^n,\qquad \delta_{t\cdot}q^n=\frac{w^n}{2h}.$$
（$G$ 项因反对称精确消失；Jacobi 截断时消失到迭代容差。）$E^{n+1/2}\ge -Nc_0/2$ 的下界要求 $M-\tfrac{h^2}4K\succ0$，即 **CFL 只依赖线性部分和 $m_{\min}$**：$h\,\omega_{\max}^{\rm lin}<2$，与振幅无关。因此 $\mu_i\le0$ 且 $f=0$ 时 $E$ 单调不增、轨道有界——**无条件对非线性振幅稳定**。

对照：原文“$q$ 冻结的离散梯度”退化为显式 kick，其稳定性要求 $h\,\omega_{\rm loc}(q)<2$，$\omega_{\rm loc}^2=(k_0+4k_c+3\alpha q^2+\beta\,\mathrm{sech}^2q)/m$，本参数下 $|q|\gtrsim10.6$ 即失稳（数值：Verlet 在 $|q|=10$ 处首次爆炸，图 `cd_cayley_and_kick_cfl.png`）。

### 5.3 注意事项
- SAV 的 $\psi$ 与真实 $\sqrt{2U+c_0}$ 会漂移（已知性质）；可用周期性的“能量不增重投影”修正（与 §5.4 同法）。
- 用 float32 + 快速超越函数近似时，$\ln\cosh$ 近似的**绝对**误差乘以 $\beta$ 可使 $2U+c_0<0$ 而在 sqrt 中产生 NaN（C 基准中实际发生，已用小 $|q|$ 级数分支修复）。实现必须保证 $U_{\rm approx}\ge0$。
- $\mu>0$ 时对角项为 $m/h^2-\mu/(2h)+\dots$，需 $\mu<2m/h$（本参数下远满足）。

### 5.4 能量不增参数更新（ENPU）
在控制节拍处 $\theta\to\theta'$：$\alpha,\beta$ 改变不改变 $E$（$\psi$ 为独立状态）。$m\to m'$ 时保持 $\bar q=(q^n+q^{n-1})/2$，令目标速度 $\tilde v=v\sqrt{m/m'}$，再以 $c=\min\{1,\sqrt{(E_0-b)/a}\}$ 缩放，其中 $a=\tfrac12\sum m'\tilde v^2-\tfrac{h^2}8\tilde v^\top K\tilde v$，$b=\tfrac12\bar q^\top K\bar q+\psi$ 部分。于是 $E_{\rm new}\le E_0$（数值断言 $<10^{-12}$）。这把“参数调制端口”的功强制为 $\le0$——是**工程设计选择**（牺牲绝热不变量的物理精确性换取绝对无爆音），需在论文中如实说明。

## 6. 慢场（反应–扩散）修正
- Gray–Scott 均匀态 $(1,0)$ 线性稳定（特征值 $-F,-(F+k)$），且 $v\equiv0$ 是 $v$ 方程的不变集：**仅作用于 $u$ 的声学源永远不能生成图案**（数值验证：$u$ 源开启 6000 步后 $v_{\max}=0$）。需要有限幅度 $v$ 种子（16×16、3×3 种子阈值约 0.18–0.32）。
- 文中常用参数集的均匀非平凡态均**不是** Turing 不稳定（$k=0$ 已不稳定或全稳定），图案是有限幅度/可激发机制，强依赖种子与网格尺寸。
- 在 Pearson 扩散系数（格点单位 $D_u=0.21,D_v=0.105$）下，16×16 周期网格上 5 组参数 × 8 个随机种子**全部消亡**；将 $D$ 缩小 4 倍后 3/5 组存活，波长约 7–8 格（16×16 仅容纳约 2 个波长），0.1–2.7 s 内**冻结为静态**（后期相对变化 ≈0），与“缓慢有机演化”的设想相反。
- 建议：源项作用于 $v$ 或调制 $F$（feed）；如需螺旋波，改用 Barkley/FitzHugh–Nagumo 可激发介质；参数在音频率上做逐样本线性插值或单极点平滑，以避免 750 Hz 阶跃产生的拉链噪声。
- 闭环（`f4_coupled_loop.py`，16×16，8 s）：按原文映射（$m=m_0(1+0.5u)$，$\alpha=\alpha_0e^{2v}$，$\mu=2000(v-0.15)$）是**正反馈**：有种子时 <1 s 内 100% 格点 $\mu>0$，$v$ 被推到均匀值（0.28；κ×5 时 0.54），图案被抹平；无种子时 $v\equiv0$，系统永不自激。全部情形有界（ESAV-LF + $\eta$ 饱和）。建议改为负反馈或带滞回/增益上限的映射。

## 7. 实时成本（C 实测摘要，详见主报告 §3.7）
Xeon 2.4 GHz 单核、空闲、5 次中位数、float32 + 快速超越函数近似：ESAV-LF 8×8 0.67–2.05%，16×16 2.4–6.1%，32×32 8.6–22.5%（下限 = 回旋项无需迭代，上限 = 15 次 Jacobi）；原文稠密 Cayley 路线 16×16 为 128%。
