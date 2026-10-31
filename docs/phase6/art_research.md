<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Phase 6a 美术调研：另类 / 实验性开源插件的界面语言

日期：2026-10-31（Asia/Shanghai）。调研在 box 上联网完成（用户 PC 当前离线，不影响本步骤）。

**用途与边界**
- 只研究这些插件的**设计决策**（信息层级、颜色纪律、字体、动效、密度），不复制它们的任何素材。
- 下表中多数项目是 GPL-3.0，它们的位图、SVG 和皮肤**一律不复制、不描摹**。MORPHO-PHS 的全部图形都由本项目代码绘制（见 `art_direction.md`）。
- 参考截图放在 `research/phase6_refs/`，仅供本地研究，**不随源码或同步包分发**（同步脚本排除该目录）。各截图版权归原作者所有。

## 1. 逐个插件的观察

| 插件 | 许可证（截图/素材） | 值得学的 | 显得廉价 / 要避免的 |
|---|---|---|---|
| **Vital**（mtytel/vital） | GPL-3.0；素材不可复制 | 波表和滤波器的**实时可视化就是主角**，旋钮反而退居二线。颜色纪律严格：深灰底，只用 1 个紫色强调色和 1 个辅色。数值读数用等宽小字，悬停时才完整出现。 | 面板全部同权重，模块一多就变成"灰色格子墙"。 |
| **Surge XT**（surge-synthesizer/surge） | GPL-3.0；皮肤可换，但默认皮肤素材属于项目 | 可缩放的皮肤系统，菜单很丰富，每个控件都有右键菜单。调制路由用颜色编码。 | 默认皮肤的信息密度极高，新手找不到主旋钮。浅色主题的强调色太多，主次不分。 |
| **Cardinal / VCV**（DISTRHO/Cardinal, VCVRack） | GPL-3.0（Cardinal）；模块面板版权归各模块作者 | 模块面板是一种"物理隐喻"，接线可见，信号流就一目了然。 | 拟物螺丝、伪金属渐变；在插件窗口里显得拥挤又过时。 |
| **Sapphire**（cosinekitty/sapphire） | GPL-3.0 | **物理模型插件的范本**。Elastika 等模块把"弹性网格""张力"直接画在面板上，用单一色相加明度表示能量。面板窄而竖，层级清楚：标题、主参数、IO。 | 面板配色过饱和（亮蓝底），离开 Rack 环境后显得玩具化。 |
| **Audible Instruments**（Mutable Instruments 移植） | 代码 GPL-3.0；面板设计属于 Émilie Gillet / VCV，不可复制 | Rings / Clouds 的面板几乎**只有字体和留白**：小号无衬线大写标签，一个主色。克制是质感的来源。 | 无 |
| **ChowMatrix**（Chowdhury-DSP/ChowMatrix） | BSD-3-Clause | **图本身就是界面**：延迟树里节点的位置就是参数（距离 = 延迟，角度 = 声像）。背景是深蓝渐变，节点用双色区分左右声道。顶部是动态的"能量雾"。 | 下方的参数表格像表格软件，与上方图形语言断裂。 |
| **ChowMultiTool**（Chowdhury-DSP/ChowMultiTool） | GPL-3.0 | 大面积曲线编辑区，彩色节点直接代表频段。品牌启动页简洁：大号字标加圆形标志。 | 底部频段按钮色块饱和度过高；标题页的大字有点"模板感"。 |
| **plugdata**（plugdata-team/plugdata） | GPL-3.0 | 把老旧的 Pd 画布重新设计成干净的现代 UI：圆角、柔和单色背景、一个强调色。图标是自绘的几何体。 | 通用"现代 App"风格，缺少声音个性。 |
| **Bespoke**（BespokeSynth） | GPL-3.0 | 极端的"可见状态"：每个模块都实时画出自己的信号，连线带动画。另类，但非常诚实。 | 无层级，字体太小，深色底上各色线混杂。 |
| **Zest / Zyn-Fusion**（mruby-zest, LGPL-2.1+） | LGPL-2.1+ | NanoVG 矢量绘制，可流式缩放；深蓝灰配青色强调；控件很密，但对齐网格严格；有自动截图脚本，**为文档生成截图**这一点值得照搬思路（不是照搬代码）。 | 面板非常多，新手迷路。 |
| **Geonkick**（quamplex/geonkick） | 代码 GPL-3.0+；截图和图像资源为 CC0 | 包络图占据上方大面积；下方是规整的旋钮阵列。图像资源全部 CC0，是少见的开放态度。 | 旋钮是小号灰色拟物件，读数很难辨认；整体发灰、对比度不足。 |
| **Dexed**（asb2m10/dexed） | GPL-3.0 | 忠实再现 DX7 的算法图，"算法拓扑"可视化很直观。 | 仿硬件的绿色 LCD 加灰色面板，复古但显旧；控件密度极高。 |
| **Odin 2**（TheWaveWarden/odin2） | 代码 GPL-3.0，图形素材另有许可（不可复用） | 拟物金属质感做得精细，模块分区靠底色区分。 | 全拟物位图很重（大量 PNG），缩放受限；视觉噪声大。 |
| **Dragonfly Reverb**（michaelwillis/dragonfly-reverb） | GPL-3.0 | 每个变体有自己的名字和统一的"蜻蜓"标志，四个产品**同一家族、不同气质**。 | 渐变灰面板加位图旋钮，显得廉价；标题字体与界面不搭。 |
| **sfizz**（sfztools/sfizz） | BSD-2-Clause | UI 极简：工具性强，信息就是文件路径和几项设置。 | 缺乏视觉身份。 |
| **Helm**（mtytel/helm） | GPL-3.0 | 早期就用实时示波器和调制可视化作为主角，扁平深色风格是 Vital 的前身。 | 小字号 + 大量同尺寸旋钮。 |
| **Wolf Shaper**（wolf-plugins/wolf-shaper） | GPL-3.0 | 整个窗口就是一条可编辑曲线，**单一隐喻**加极少的旋钮。网格线很淡，曲线亮，层级清晰。 | 底部旋钮条与上方风格不统一。 |
| **Ninjas2**（clearly-broken-software/ninjas2） | GPL-3.0 | 波形区占据主视觉，切片颜色是唯一的强调。 | 红黑配色像硬件采样器模板。 |
| **DISTRHO / DPF 系列** | ISC（DPF）；各插件单独许可 | 轻量 NanoVG / Cairo 绘制，体积小、启动快，**性能即体验**。 | 很多 DPF 插件直接用默认控件，没有身份。 |
| **Gate-12**（tiagolr/gate12） | GPL-3.0 | 黑底、单一橙色强调、包络曲线下方填充半透明能量；小字号标签全大写，间距宽。 | 无 |
| **ADLplug**（jpcima/ADLplug） | BSL-1.0 | 复刻芯片 FM 合成器的操作员布局，有清晰的分区编号。 | 蓝红双主色加拟物位图，显得陈旧。 |

**参考链接**
- Vital https://github.com/mtytel/vital
- Surge XT https://github.com/surge-synthesizer/surge
- Cardinal https://github.com/DISTRHO/Cardinal
- Sapphire https://github.com/cosinekitty/sapphire
- Audible Instruments https://github.com/VCVRack/AudibleInstruments
- ChowMatrix https://github.com/Chowdhury-DSP/ChowMatrix
- ChowMultiTool https://github.com/Chowdhury-DSP/ChowMultiTool
- plugdata https://github.com/plugdata-team/plugdata
- Bespoke https://github.com/BespokeSynth/BespokeSynth
- mruby-zest https://github.com/mruby-zest/mruby-zest（以及 LAC 2018 论文 http://lac.linuxaudio.org/2018/pdf/38-paper.pdf）
- Geonkick https://github.com/quamplex/geonkick
- Dexed https://github.com/asb2m10/dexed
- Odin 2 https://github.com/TheWaveWarden/odin2
- Dragonfly https://github.com/michaelwillis/dragonfly-reverb
- sfizz https://github.com/sfztools/sfizz
- Helm https://github.com/mtytel/helm
- Wolf Shaper https://github.com/wolf-plugins/wolf-shaper
- Ninjas2 https://github.com/clearly-broken-software/ninjas2
- DPF https://github.com/DISTRHO/DPF
- Gate-12 https://github.com/tiagolr/gate12
- ADLplug https://github.com/jpcima/ADLplug

许可证信息取自 2026-10-31 GitHub API 和各 README 的徽章。ChowMultiTool 的 README 徽章标注 GPLv3，ChowMatrix 标注 BSD-3。

本地保存的截图共 16 张：adlplug, bespoke, cardinal_fundamental, chowmatrix, chowmultitool_eq/title, dragonfly, gate12, geonkick, ninjas2, odin2, plugdata（图标）, sapphire_chaops/elastika, vital, wolfshaper。Surge、Dexed、Helm、Audible Instruments 和 sfizz 的截图链接已失效（404），这几个依据项目页面描述和以往的使用经验评述，没有本地截图。

## 2. 共性结论（直接影响 Phase 6a 的决策）

1. **主角必须是"隐藏状态"的可视化**，而不是旋钮。做得最好的几个（Vital、ChowMatrix、Sapphire、Wolf Shaper）都把插件内部真正发生的事画成最大的元素。MORPHO-PHS 的隐藏状态是格点的位移、哈密顿能量、Barkley 介质和拓扑边缘通道，这就是主视觉。
2. **颜色纪律 = 质感。** 单一强调色加一条有物理意义的明度/热度色阶（Gate-12、Audible Instruments、Vital）显得昂贵。多色相并列（Surge 浅色主题、ADLplug、Dexed）显得廉价。
3. **拟物位图是负资产。** Odin2 和 Dragonfly 的位图旋钮放大后发糊，体积大，而且一眼看出是素材。矢量绘制（Zest、DPF/NanoVG、Vital）缩放干净，也更省内存。
4. **字体：小号、全大写、加字距的标签，配等宽数字读数**是专业感的最低成本来源（Mutable、Gate-12、Vital）。至多两个字族。
5. **密度要分层，而不是删参数。** 参数多的插件（Surge、Dexed、Zyn）失败在"同权重"。办法是：宏旋钮最大，物理参数中号，引擎和高级参数收进可折叠抽屉。
6. **家族化。** Dragonfly 用同一标志、不同名称，形成家族感。MORPHO-PHS 乐器版和 FX 版应同源、但一眼能区分。
7. **动效要诚实且克制。** Bespoke 的动画全部来自真实信号，这是对的；但不能让动画成为 CPU 负担。要有低动效 / 省电选项，窗口隐藏时停止绘制。
8. **反面清单（避免"预设感"）：** 霓虹外发光、玻璃拟态、伪金属渐变、螺丝钉、通用"赛博朋克"青紫配色、现成 UI 套件的圆角卡片、照搬 Inter + 圆角卡片的 Web 风。

## 3. 由此得出的 MORPHO-PHS 原则（详见 art_direction.md）

- **"能量即光"**：界面上除了强调色标记的交互状态以外，**只有物理能量会发光**。静态结构全部是哑光石墨色。
- 图形母题全部来自本项目的物理：蜂窝格的 Y 形三键（A/B 两个子格）、哈密顿能量热度、Barkley 可激介质的影线、手性边缘通道的单向流动，以及多面体的等距投影（蜂窝格单元恰好就是立方体的等距投影）。
