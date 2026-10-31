<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# MORPHO-PHS 出厂预设列表 / Factory preset list (Phase 6b2)

由 `scripts/presets/docs6b.py` 根据 `presets/*.morpho` 与 Phase 6b2 QA/CPU 测量自动生成。Generated from the preset files and the Phase 6b2 measurements.

- CPU 等级 / CPU class：i9-13900HX（MSVC 构建）实时预算（48 kHz、128 样本块，20.83 µs/样本）的 p99 占比：eco < 20 %，standard 20–50 %，high ≥ 50 %。CPU 列括号内为 p99/预算：来自 2026-10-31 的 i9 实测（`experiments/results/phase6b2/i9/qa.json`）；标“估”的是 6b2 改动后尚未在 i9 复测的预设，用保守估计（`scripts/presets/cpu6b.py`）。
- 响度 / Loudness：乐器 −18 LUFS-I，效果器 −20 LUFS-I（= 演示干声输入，旁通对比时响度中性），真峰值 ≤ −1 dBTP；6b2 起样本峰值 ≤ 0.80（限幅器拐点 0.9 以下 1 dB），FX 另按 25 个噪声种子的最坏峰值留余量。标 “峰值受限” 的打击类预设按峰值归一（EBU 单发声音做法），积分响度低于目标。
- 试听 / Demos：`experiments/results/phase6b/demos/<文件名>.{wav,ogg,mp3}`（演示乐句 8 s + 余音）；6b2 改动的预设见 `experiments/results/phase6b2/demos/`。

## 乐器 / Instrument (MORPHO-PHS) — 32 个预设

| # | 名称 Name | 类别 Category | 晶格 | 势能 | CPU | LUFS-I | 余音 s | 说明 |
|---|---|---|---|---|---|---|---|---|
| 001 | **Gyro Bell** | Gyro Bells | 16x16 | HQ | standard ●●○ (0.21) | -18.1 | 0.6 | 强陀螺耦合的拨奏晶格：简并模态被劈裂，像敲击的钟一样缓慢拍频。 |
| 002 | **Counter-Spin Bell** | Gyro Bells | 16x16 | HQ | standard ●●○ (0.23) | -18.1 | 0.8 | 手性反向：同一口钟，泛音沿雪花反方向拍动。 |
| 003 | **Glass Gong** | Gyro Bells | 16x16 | HQ | standard ●●○ (0.22) | -18.0 | 2.3 | 宽的在位能隙拉伸泛音列；长而闪烁的锣尾。 |
| 004 | **Toy Carillon** | Gyro Bells | 8x8 | Quartic | eco ●○○ (0.07) | -18.0 | 0.0 | 8x8 小雪花：模态少，清晰的非谐和钟声；库里最省 CPU 的钟。 |
| 005 | **Cathedral Gyro 32** | Gyro Bells | 32x32 | HQ | standard ●●○ (0.41) | -18.1 | 4.7 | 32x32 大雪花：稠密的模态云，劈裂泛音形成缓慢的合唱。 |
| 006 | **Honeycomb Harp** | Plucks | 16x16 | Quartic | eco ●○○ (0.18) | -18.1 | 0.0 | 蜂窝上的柔和拨奏；轻微手性让余音略带旋动。 |
| 007 | **Snowflake Koto** | Plucks | 16x16 | Quartic | eco ●○○ (0.18) | -20.5 (峰值受限) | 0.0 | 拾音点贴近中心：鼻音般、类似古筝的起音，随后快速绽开。 |
| 008 | **Felt Lattice** | Plucks | 16x16 | Quartic | eco ●○○ (0.14) | -21.2 (峰值受限) | 0.0 | 低音调、短衰减：像包了毛毡的闷音拨奏。 |
| 009 | **Bent Tine** | Plucks | 16x16 | HQ | standard ●●○ (0.28) | -20.0 | 0.0 | HQ 能隙加非线性：像音叉齿的拨奏，振幅衰减时音高随之弯曲。 |
| 010 | **Defect Kalimba** | Percussive Lattice | 16x16 | Quartic | eco ●○○ (0.14) | -21.2 (峰值受限) | 0.2 | 一排重缺陷把模态困在小腔里：短促、有音高、像拇指琴的敲击。 |
| 011 | **Low Mallets** | Percussive Lattice | 16x16 | Quartic | eco ●○○ (0.14) | -24.4 (峰值受限) | 0.0 | 低音调、散布缺陷：带木质感的马林巴式槌击。 |
| 012 | **Shard Clave** | Percussive Lattice | 16x16 | Quartic | eco ●○○ (0.14) | -26.8 (峰值受限) | 0.0 | 密集随机缺陷与快速阻尼：干脆、玻璃质、带音高的咔哒声。 |
| 013 | **Ring Cavity Tom** | Percussive Lattice | 16x16 | Quartic | eco ●○○ (0.13) | -21.4 (峰值受限) | 0.0 | 缺陷围成的圆环像鼓皮腔体；腔内的声部低沉轰鸣。 |
| 014 | **Chiral Cello** | Chiral Strings | 16x16 | Quartic | eco ●○○ (0.15) | -18.0 | 1.0 | 弓擦锯齿波穿过低调手性雪花：木质、略带扭转感的大提琴腔体。 |
| 015 | **Rusty Bow** | Chiral Strings | 16x16 | Quartic | eco ●○○ (0.16) | -20.1 (峰值受限) | 0.6 | 高四次非线性：弓在刮擦，泛音拥挤并发出低吼。 |
| 016 | **Viola Gyre** | Chiral Strings | 16x16 | Quartic | eco ●○○ (0.15) | -18.0 | 1.2 | 中音区；强手性把弓擦音拉成旋转的合唱感。 |
| 017 | **Glass Harmonica** | Chiral Strings | 16x16 | HQ | standard ●●○ (0.24) | -18.0 | 2.0 | HQ 能隙加高音调：湿指摩擦玻璃般纯净，略带不稳定。 |
| 018 | **Sul Ponticello** | Chiral Strings | 16x16 | Quartic | eco ●○○ (0.15) | -18.0 | 0.3 | 拾音点外移至边缘：贴近琴码的纤细玻璃质弓音。 |
| 019 | **Rim Rider** | Edge / Topological | 16x16 | Quartic | eco ●○○ (0.17) | -18.0 | 4.1 | 最大手性：弓擦能量沿雪花边缘传播，边缘拾音点听到它绕圈。 |
| 020 | **Mirror Rim** | Edge / Topological | 16x16 | Quartic | eco ●○○ (0.16) | -18.0 | 4.0 | 同一条边缘反向旋转，串扰把两个拾音点折叠在一起。 |
| 021 | **Edge Chime** | Edge / Topological | 16x16 | Quartic | eco ●○○ (0.14) | -20.0 | 3.4 | 满手性拨奏：钟声泛音在衰减时仍沿边缘行进。 |
| 022 | **Ember Field** | Excitable Textures | 16x16 | Quartic | eco ●○○ (0.14) | -18.0 | 2.8 | 拨奏激发形态场波，像余烬被短暂扇旺，随后冷却。 |
| 023 | **Static Bloom** | Excitable Textures | 16x16 | Quartic | eco ●○○ (0.14) | -18.0 | 0.8 | 快速 Barkley 介质（短 tau）：每个音符后都绽开噼啪作响的颗粒。 |
| 024 | **Slow Tide** | Excitable Textures | 16x16 | Quartic | eco ●○○ (0.14) | -18.0 | 6.1 | 慢速介质（长 tau）配合弓擦：音色像潮汐一样涨落。 |
| 025 | **Nerve Net** | Excitable Textures | 16x16 | HQ | standard ●●○ (0.22) | -18.0 | 3.1 | HQ 势加敏感介质：拨奏在雪花上激发神经脉冲般的连发。 |
| 026 | **Spiral Drone** | Drones | 16x16 | Quartic | eco ●○○ (0.14) | -18.0 | 2.8 | 介质中植入的螺旋波缓慢旋转，扫动弓擦长音的音色。 |
| 027 | **Low Orbit** | Drones | 16x16 | Quartic | eco ●○○ (0.15) | -18.0 | 5.7 | 极低音调、接近临界的 Life：释放后仍久久不散的厚重低频长音。 |
| 028 | **Halo Choir** | Drones | 16x16 | HQ | standard ●●○ (0.25) | -18.0 | 3.4 | HQ 能隙、宽拾音、慢形态场：空灵、带元音感的合唱铺底。 |
| 029 | **Big Spiral** | Drones | 16x16 | HQ | standard ●●○ (0.42 估) | -18.0 | 1.8 | 雪花加螺旋种子：缓慢旋转的稠密长音。 |
| 030 | **Morphogen Tapestry** | Morph Evolutions | 16x16 | Quartic | standard ●●○ (0.34) | -18.0 | 2.4 | 两个形态场源相撞；弓擦音慢慢重新编织泛音。 |
| 031 | **Turing Bells** | Morph Evolutions | 16x16 | Quartic | standard ●●○ (0.26) | -18.0 | 1.8 | 在植入螺旋的介质上拨奏的钟：每一次敲击落在不同的纹理上。 |
| 032 | **Breathing Glass** | Morph Evolutions | 16x16 | Quartic | eco ●○○ (0.15) | -18.1 | 1.5 | 弓擦高音玻璃，其明亮度随介质慢波呼吸。 |

### 详情 / Details

**001 Gyro Bell** — Gyro Bells · `bell, bright, chiral, pluck`  
强陀螺耦合的拨奏晶格：简并模态被劈裂，像敲击的钟一样缓慢拍频。  
*Plucked lattice with strong gyroscopic coupling: split modes beat slowly like a struck bell.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Gap  
来源 Origin: 20 Metal Lattice (rework)

**002 Counter-Spin Bell** — Gyro Bells · `bell, chiral, dark, pluck`  
手性反向：同一口钟，泛音沿雪花反方向拍动。  
*Chirality reversed: the same bell, but the partials beat the other way around the flake.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Gap  
来源 Origin: new (Phase 6b)

**003 Glass Gong** — Gyro Bells · `bell, chiral, hq, long, pluck`  
宽的在位能隙拉伸泛音列；长而闪烁的锣尾。  
*Wide on-site gap stretches the partials; long, shimmering gong tail.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Gap  
来源 Origin: new (Phase 6b)

**004 Toy Carillon** — Gyro Bells · `bell, chiral, eco, pluck, small`  
8x8 小雪花：模态少，清晰的非谐和钟声；库里最省 CPU 的钟。  
*8x8 flake: few modes, clear inharmonic chime; the cheapest bell in the bank.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Defects  
来源 Origin: new (Phase 6b)

**005 Cathedral Gyro 32** — Gyro Bells · `bell, chiral, large, pluck, showcase`  
32x32 大雪花：稠密的模态云，劈裂泛音形成缓慢的合唱。  
*32x32 flake: dense mode cloud, slow chorus of split partials.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Gap  
来源 Origin: new (Phase 6b)

**006 Honeycomb Harp** — Plucks · `pluck, warm`  
蜂窝上的柔和拨奏；轻微手性让余音略带旋动。  
*Soft plucks on the honeycomb; mild chirality keeps the decay slightly swirling.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Crosstalk  
来源 Origin: 18 Honeycomb Harp (rework)

**007 Snowflake Koto** — Plucks · `bright, narrow, pluck`  
拾音点贴近中心：鼻音般、类似古筝的起音，随后快速绽开。  
*Narrow pickups near the centre: nasal, koto-like attack with a quick bloom.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Crosstalk  
来源 Origin: new (Phase 6b)

**008 Felt Lattice** — Plucks · `dark, pluck, soft`  
低音调、短衰减：像包了毛毡的闷音拨奏。  
*Low tone and short decay: muted, felt-covered plucks.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Crosstalk  
来源 Origin: new (Phase 6b)

**009 Bent Tine** — Plucks · `hq, metallic, pluck`  
HQ 能隙加非线性：像音叉齿的拨奏，振幅衰减时音高随之弯曲。  
*HQ gap plus nonlinearity: tine-like pluck whose pitch bends as it rings out.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Crosstalk  
来源 Origin: new (Phase 6b)

**010 Defect Kalimba** — Percussive Lattice · `defects, percussion, tuned`  
一排重缺陷把模态困在小腔里：短促、有音高、像拇指琴的敲击。  
*A row of heavy defects traps modes in pockets: short, tuned, kalimba-like hits.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Defects  
来源 Origin: 19 Defect Kalimba (rework)

**011 Low Mallets** — Percussive Lattice · `defects, low, percussion, wood`  
低音调、散布缺陷：带木质感的马林巴式槌击。  
*Low tone, scattered defects: woody marimba-ish mallets.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Defects  
来源 Origin: 21 Low Mallets (rework)

**012 Shard Clave** — Percussive Lattice · `bright, defects, percussion, short`  
密集随机缺陷与快速阻尼：干脆、玻璃质、带音高的咔哒声。  
*Dense random defects and fast damping: dry, glassy clicks with pitch.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Defects  
来源 Origin: new (Phase 6b)

**013 Ring Cavity Tom** — Percussive Lattice · `defects, drum, low, percussion`  
缺陷围成的圆环像鼓皮腔体；腔内的声部低沉轰鸣。  
*A ring of defects forms a drum-head cavity; the voice inside booms.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Defects  
来源 Origin: new (Phase 6b)

**014 Chiral Cello** — Chiral Strings · `bowed, low, warm`  
弓擦锯齿波穿过低调手性雪花：木质、略带扭转感的大提琴腔体。  
*A bowed saw through a low-tuned chiral flake: woody, slightly twisted cello body.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Crosstalk  
来源 Origin: 14 Chiral Cello (keep, re-gain)

**015 Rusty Bow** — Chiral Strings · `bowed, dark, grit`  
高四次非线性：弓在刮擦，泛音拥挤并发出低吼。  
*High quartic nonlinearity: the bow scrapes, partials crowd and growl.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Crosstalk  
来源 Origin: 17 Rusty Bow (rework)

**016 Viola Gyre** — Chiral Strings · `bowed, chiral, mid`  
中音区；强手性把弓擦音拉成旋转的合唱感。  
*Mid register; strong chirality pulls the bowed tone into a rotating chorus.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Crosstalk  
来源 Origin: new (Phase 6b)

**017 Glass Harmonica** — Chiral Strings · `bowed, bright, glass, hq`  
HQ 能隙加高音调：湿指摩擦玻璃般纯净，略带不稳定。  
*HQ gap and a high tone: wet-finger glass, pure and slightly unstable.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Gap  
来源 Origin: 15 Glass Harmonica (rework)

**018 Sul Ponticello** — Chiral Strings · `bowed, bright, thin`  
拾音点外移至边缘：贴近琴码的纤细玻璃质弓音。  
*Pickups spread to the rim: thin, glassy bowing near the bridge.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Crosstalk  
来源 Origin: new (Phase 6b)

**019 Rim Rider** — Edge / Topological · `bowed, chiral, edge, wide`  
最大手性：弓擦能量沿雪花边缘传播，边缘拾音点听到它绕圈。  
*Maximum chirality: the bowed energy rides the flake's rim, pickups at the edge hear it circle.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Crosstalk  
来源 Origin: new (Phase 6b)

**020 Mirror Rim** — Edge / Topological · `bowed, chiral, edge`  
同一条边缘反向旋转，串扰把两个拾音点折叠在一起。  
*The same edge, counter-rotating, with crosstalk folding the two pickups together.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Crosstalk  
来源 Origin: new (Phase 6b)

**021 Edge Chime** — Edge / Topological · `bell, chiral, edge, pluck, topological`  
满手性拨奏：钟声泛音在衰减时仍沿边缘行进。  
*Plucked at full chirality: chime partials that keep travelling around the rim as they fade.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Defects  
来源 Origin: new (Phase 6b)

**022 Ember Field** — Excitable Textures · `excitable, morphogen, pluck, warm`  
拨奏激发形态场波，像余烬被短暂扇旺，随后冷却。  
*Plucks kick off morphogen waves that briefly fan the lattice like embers, then cool.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Defects  
来源 Origin: 23 Ember Field (rework: sub-critical Life)

**023 Static Bloom** — Excitable Textures · `bright, excitable, grainy, morphogen`  
快速 Barkley 介质（短 tau）：每个音符后都绽开噼啪作响的颗粒。  
*Fast Barkley medium (short tau): crackling blooms of grain after every note.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Defects  
来源 Origin: new (Phase 6b)

**024 Slow Tide** — Excitable Textures · `bowed, excitable, morphogen, pad, slow`  
慢速介质（长 tau）配合弓擦：音色像潮汐一样涨落。  
*Slow medium (long tau) under a bow: the timbre swells and recedes in tides.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Defects  
来源 Origin: new (Phase 6b)

**025 Nerve Net** — Excitable Textures · `excitable, hq, morphogen, pluck, restless`  
HQ 势加敏感介质：拨奏在雪花上激发神经脉冲般的连发。  
*HQ potential + a twitchy medium: plucks fire nerve-like volleys across the flake.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Gap  
来源 Origin: 24 Restless Swarm (rework: sub-critical Life)

**026 Spiral Drone** — Drones · `drone, long, low, pad, spiral`  
介质中植入的螺旋波缓慢旋转，扫动弓擦长音的音色。  
*A seeded spiral wave turns slowly in the medium, sweeping the bowed drone's colour.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Crosstalk  
来源 Origin: 22 Spiral Drone (rework: sub-critical Life)

**027 Low Orbit** — Drones · `dark, drone, long, pad, sub`  
极低音调、接近临界的 Life：释放后仍久久不散的厚重低频长音。  
*Very low tone, near-critical Life: a sub-heavy drone that hangs on after release.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Crosstalk  
来源 Origin: new (Phase 6b)

**028 Halo Choir** — Drones · `bright, choir, drone, hq, long, pad`  
HQ 能隙、宽拾音、慢形态场：空灵、带元音感的合唱铺底。  
*HQ gap, wide pickups, slow morphogen: airy, vowel-like choir pad.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Gap  
来源 Origin: 16 Twisted Choir (rework)

**029 Big Spiral** — Drones · `drone, long, pad, showcase, spiral`  
雪花加螺旋种子：缓慢旋转的稠密长音。  
*Snowflake with a seeded spiral: a slowly rotating, dense drone.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Gap  
来源 Origin: 26 Big Spiral 32 (rework: sub-critical Life; 6b2: 16x16 HQ fast for the i9 CPU budget)

**030 Morphogen Tapestry** — Morph Evolutions · `bowed, evolving, morphogen, slow`  
两个形态场源相撞；弓擦音慢慢重新编织泛音。  
*Two morphogen sources collide; the bowed tone slowly reweaves its partials.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Crosstalk  
来源 Origin: new (Phase 6b)

**031 Turing Bells** — Morph Evolutions · `bell, evolving, excitable, morphogen`  
在植入螺旋的介质上拨奏的钟：每一次敲击落在不同的纹理上。  
*Plucked bells over a spiral-seeded medium: each strike lands on a different texture.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Defects  
来源 Origin: new (Phase 6b)

**032 Breathing Glass** — Morph Evolutions · `bowed, bright, evolving, excitable, glass, morphogen`  
弓擦高音玻璃，其明亮度随介质慢波呼吸。  
*Bowed high glass whose brightness breathes with the medium's slow waves.*  
宏 Macros: 1 Brightness · 2 Chirality · 3 Sustain · 4 Grit · 5 Morphogen · 6 Life · 7 Width · 8 Defects  
来源 Origin: new (Phase 6b)

## 效果器 / FX (MORPHO-PHS FX) — 25 个预设

| # | 名称 Name | 类别 Category | 晶格 | 势能 | CPU | LUFS-I | 余音 s | 说明 |
|---|---|---|---|---|---|---|---|---|
| 101 | **Edge Shimmer** | Edge Dispersers | 16x16 | Quartic | eco ●○○ (0.16) | -21.2 | 0.1 | 能隙内频带沿单向手性边缘传播：在干声旁形成明亮、色散的鸣响。 |
| 102 | **Low Edge Rumble** | Edge Dispersers | 16x16 | Quartic | eco ●○○ (0.15) | -21.5 | 0.1 | 边缘调低：在底鼓与贝斯下方缓慢色散的暗沉低鸣。 |
| 103 | **Glass Corridor** | Edge Dispersers | 16x16 | HQ | standard ●●○ (0.22) | -21.7 | 0.1 | HQ 能隙抬高边缘频带：长而透明的玻璃走廊，尾音高而纯。 |
| 104 | **Grit Edge** | Edge Dispersers | 16x16 | Quartic | eco ●○○ (0.14) | -22.0 (峰值受限) | 0.1 | 边缘上的四次非线性：瞬态返回时带着颗粒感与音高涂抹。 |
| 105 | **Pinhole Edge** | Edge Dispersers | 16x16 | Quartic | eco ●○○ (0.14) | -22.6 (峰值受限) | 0.1 | 缺陷夹窄了边缘：边缘波在针孔处散射成梳状回声。 |
| 106 | **Tiny Edge 8** | Edge Dispersers | 8x8 | Quartic | eco ●○○ (0.08) | -22.2 (峰值受限) | 0.1 | 8x8 小雪花：短边缘、紧凑的边缘回声颤动；最轻量的效果器。 |
| 107 | **Long Edge 32** | Edge Dispersers | 32x32 | Quartic | standard ●●○ (0.32) | -20.9 | 0.1 | 32x32 边缘：路径长四倍，色散被拉成缓慢扫动的尾音。 |
| 108 | **Freeze Halo** | Defect Freezers | 16x16 | Quartic | eco ●○○ (0.14) | -20.0 | 4.2 | 缺陷环周围的部分冻结：每次敲击后悬停的柔和光晕。 |
| 109 | **Crystal Cage** | Defect Freezers | 16x16 | Quartic | eco ●○○ (0.18) | -20.1 | 0.6 | 重缺陷腔体：冻结的模态被困在腔内，像水晶和弦般鸣响。 |
| 110 | **Soft Hold** | Defect Freezers | 16x16 | Quartic | eco ●○○ (0.17) | -20.0 | 1.7 | 无缺陷的轻度冻结：整片雪花维持柔和、模糊的延音。 |
| 111 | **Shattered Ice** | Defect Freezers | 16x16 | HQ | standard ●●○ (0.34) | -20.0 | 1.8 | 散布缺陷加 HQ：充满颗粒与闪光泛音的冻结场。 |
| 112 | **Spiral Lock** | Defect Freezers | 16x16 | Quartic | eco ●○○ (0.17) | -20.1 | 2.7 | 缺陷沿螺旋臂排布：冻结的音色在消退时向外盘旋。 |
| 113 | **Glacier 32** | Defect Freezers | 32x32 | Quartic | standard ●●○ (0.36) | -20.1 | 4.7 | 32x32 冻结：带一条断层缺陷的巨大、缓慢的冻结模态冰川。 |
| 114 | **Bulk Smear** | Lattice Spaces | 16x16 | Quartic | eco ●○○ (0.18) | -21.3 | 0.1 | 低手性体态腔体：短而涂抹的共振，让鼓更厚实。 |
| 115 | **Honeycomb Room** | Lattice Spaces | 16x16 | Quartic | standard ●●○ (0.34) | -20.1 | 0.2 | 中等衰减、宽拾音：把雪花当作一间温暖的小木屋。 |
| 116 | **Gyro Chamber** | Lattice Spaces | 16x16 | Quartic | standard ●●○ (0.30) | -20.1 | 0.7 | 体态中的强手性：反射盘旋，立体声像随之旋转。 |
| 117 | **Dark Plate** | Lattice Spaces | 16x16 | Quartic | eco ●○○ (0.19) | -20.0 | 1.2 | 低音调、长衰减：垫在人声或键盘下方的暗色板式混响。 |
| 118 | **Ember Echo** | Excitable FX | 16x16 | Quartic | standard ●●○ (0.22) | -20.0 | 3.2 | 敲击扇起形态场波；回声在冷却前会微微变亮。 |
| 119 | **Static Gate** | Excitable FX | 16x16 | Quartic | standard ●●○ (0.30) | -20.1 | 0.9 | 快速介质：每个瞬态留下短暂的噼啪声，随后归于寂静。 |
| 120 | **Living Edge** | Excitable FX | 16x16 | Quartic | standard ●●○ (0.23) | -21.8 | 0.1 | 接近临界 Life 的边缘色散器：输入之后边缘鸣响会膨胀。 |
| 121 | **Spark Chamber** | Excitable FX | 16x16 | HQ | eco ●○○ (0.20) | -20.1 | 1.8 | HQ 体态配敏感介质：瞬态在高频带上迸出火花。 |
| 122 | **Breathing Edge** | Morph Evolutions | 16x16 | Quartic | standard ●●○ (0.26) | -22.7 (峰值受限) | 0.1 | 形态场波调制边缘阻尼：鸣响自行呼吸。 |
| 123 | **Tidal Freeze** | Morph Evolutions | 16x16 | Quartic | eco ●○○ (0.16) | -20.0 | 2.5 | 慢速介质调制部分冻结：保持的音像潮水般起落。 |
| 124 | **Turing Smear** | Morph Evolutions | 16x16 | Quartic | eco ●○○ (0.15) | -20.0 | 0.5 | 介质中旋转的螺旋波带动腔体音色一同旋转。 |
| 125 | **Chiral Drift** | Morph Evolutions | 16x16 | Quartic | standard ●●○ (0.22) | -21.6 | 0.1 | 形态场直接调制手性：边缘频带在音高与方向上漂移。 |

### 详情 / Details

**101 Edge Shimmer** — Edge Dispersers · `bright, dispersion, edge`  
能隙内频带沿单向手性边缘传播：在干声旁形成明亮、色散的鸣响。  
*In-gap bands ride the one-way chiral edge: a bright, dispersed ring beside the dry signal.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Crosstalk  
来源 Origin: 01 Edge Shimmer + 02 Reverse Edge (merged)

**102 Low Edge Rumble** — Edge Dispersers · `dark, edge, low`  
边缘调低：在底鼓与贝斯下方缓慢色散的暗沉低鸣。  
*Edge tuned low: a dark, slowly dispersing rumble under kick and bass.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Crosstalk  
来源 Origin: 03 Low Edge Rumble (rework)

**103 Glass Corridor** — Edge Dispersers · `edge, glass, hq`  
HQ 能隙抬高边缘频带：长而透明的玻璃走廊，尾音高而纯。  
*HQ gap lifts the edge band: long, glassy corridor with a high, pure tail.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Gap  
来源 Origin: 04 Glass Corridor (keep, re-gain)

**104 Grit Edge** — Edge Dispersers · `drive, edge, grit`  
边缘上的四次非线性：瞬态返回时带着颗粒感与音高涂抹。  
*Quartic nonlinearity on the edge: transients come back crunchy and pitch-smeared.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Crosstalk  
来源 Origin: 06 Grit Edge (rework)

**105 Pinhole Edge** — Edge Dispersers · `comb, defects, edge`  
缺陷夹窄了边缘：边缘波在针孔处散射成梳状回声。  
*Defects pinch the rim: the edge wave scatters at the pinholes into comb-like echoes.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Defects  
来源 Origin: 08 Pinhole Edge (rework)

**106 Tiny Edge 8** — Edge Dispersers · `eco, edge, small`  
8x8 小雪花：短边缘、紧凑的边缘回声颤动；最轻量的效果器。  
*8x8 flake: a short rim, tight flutter of edge echoes; the lightest FX.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Crosstalk  
来源 Origin: 27 Tiny Edge 8 (rework)

**107 Long Edge 32** — Edge Dispersers · `edge, large, showcase`  
32x32 边缘：路径长四倍，色散被拉成缓慢扫动的尾音。  
*32x32 rim: four times the path, so the dispersion stretches into a slow, sweeping tail.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Crosstalk  
来源 Origin: 25 Big Edge 32 (rework)

**108 Freeze Halo** — Defect Freezers · `defects, freeze, halo`  
缺陷环周围的部分冻结：每次敲击后悬停的柔和光晕。  
*Partial freeze around a defect ring: a soft halo that hangs on after each hit.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Freeze  
来源 Origin: 09 Freeze Halo (rework: finite tail by default)

**109 Crystal Cage** — Defect Freezers · `cavity, defects, freeze`  
重缺陷腔体：冻结的模态被困在腔内，像水晶和弦般鸣响。  
*Heavy defect cavity: frozen modes stay trapped inside and ring as a crystal chord.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Freeze  
来源 Origin: 10 Crystal Cage (rework)

**110 Soft Hold** — Defect Freezers · `freeze, pad, soft`  
无缺陷的轻度冻结：整片雪花维持柔和、模糊的延音。  
*Gentle freeze with no defects: the whole flake sustains a soft, blurred hold.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Freeze  
来源 Origin: 11 Soft Hold (keep, re-gain)

**111 Shattered Ice** — Defect Freezers · `freeze, grainy, hq`  
散布缺陷加 HQ：充满颗粒与闪光泛音的冻结场。  
*Scattered defects + HQ: a frozen field full of grainy, glinting partials.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Freeze  
来源 Origin: 12 Shattered Ice (rework)

**112 Spiral Lock** — Defect Freezers · `defects, freeze, spiral`  
缺陷沿螺旋臂排布：冻结的音色在消退时向外盘旋。  
*Defects laid along a spiral arm: the frozen tone winds outwards as it fades.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Freeze  
来源 Origin: new (Phase 6b)

**113 Glacier 32** — Defect Freezers · `freeze, large, showcase`  
32x32 冻结：带一条断层缺陷的巨大、缓慢的冻结模态冰川。  
*32x32 freeze: a vast, slow glacier of held modes with a line of fault defects.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Freeze  
来源 Origin: new (Phase 6b)

**114 Bulk Smear** — Lattice Spaces · `dark, smear, space`  
低手性体态腔体：短而涂抹的共振，让鼓更厚实。  
*Low chirality bulk body: a short, smeared resonance that thickens drums.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Defects  
来源 Origin: 13 Bulk Smear (rework)

**115 Honeycomb Room** — Lattice Spaces · `room, space, warm`  
中等衰减、宽拾音：把雪花当作一间温暖的小木屋。  
*Medium decay, wide pickups: the flake as a small warm wooden room.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Defects  
来源 Origin: new (Phase 6b)

**116 Gyro Chamber** — Lattice Spaces · `chiral, space, wide`  
体态中的强手性：反射盘旋，立体声像随之旋转。  
*Strong chirality in the bulk: reflections swirl and the stereo image rotates.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Crosstalk  
来源 Origin: new (Phase 6b)

**117 Dark Plate** — Lattice Spaces · `long, plate, space`  
低音调、长衰减：垫在人声或键盘下方的暗色板式混响。  
*Low tone, long decay: a dark plate-like bloom under vocals or keys.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Defects  
来源 Origin: new (Phase 6b)

**118 Ember Echo** — Excitable FX · `bloom, excitable, morphogen, warm`  
敲击扇起形态场波；回声在冷却前会微微变亮。  
*Hits fan morphogen waves; the echo glows a little louder before it cools.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Defects  
来源 Origin: new (Phase 6b)

**119 Static Gate** — Excitable FX · `excitable, grainy, morphogen, rhythmic`  
快速介质：每个瞬态留下短暂的噼啪声，随后归于寂静。  
*Fast medium: every transient leaves a short burst of crackle, then silence.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Defects  
来源 Origin: new (Phase 6b)

**120 Living Edge** — Excitable FX · `bloom, edge, excitable, morphogen`  
接近临界 Life 的边缘色散器：输入之后边缘鸣响会膨胀。  
*Edge disperser with near-critical Life: the rim ring swells after the input.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Crosstalk  
来源 Origin: new (Phase 6b)

**121 Spark Chamber** — Excitable FX · `bright, excitable, hq, morphogen`  
HQ 体态配敏感介质：瞬态在高频带上迸出火花。  
*HQ bulk with a twitchy medium: sparks fly off transients in the high band.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Gap  
来源 Origin: new (Phase 6b)

**122 Breathing Edge** — Morph Evolutions · `edge, evolving, morphogen`  
形态场波调制边缘阻尼：鸣响自行呼吸。  
*Morphogen waves modulate the edge's damping: the shimmer breathes on its own.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Crosstalk  
来源 Origin: 05 Breathing Edge (rework)

**123 Tidal Freeze** — Morph Evolutions · `evolving, freeze, slow`  
慢速介质调制部分冻结：保持的音像潮水般起落。  
*A slow medium modulates a partial freeze: held tones ebb and flow.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Freeze  
来源 Origin: new (Phase 6b)

**124 Turing Smear** — Morph Evolutions · `evolving, space, spiral`  
介质中旋转的螺旋波带动腔体音色一同旋转。  
*A spiral wave turns in the medium; the body's colour rotates with it.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Defects  
来源 Origin: new (Phase 6b)

**125 Chiral Drift** — Morph Evolutions · `chiral, edge, evolving`  
形态场直接调制手性：边缘频带在音高与方向上漂移。  
*Morphogen modulates chirality itself: the edge band drifts in pitch and direction.*  
宏 Macros: 1 Tone · 2 Chirality · 3 Mix · 4 Decay · 5 Morphogen · 6 Grit · 7 Width · 8 Crosstalk  
来源 Origin: new (Phase 6b)

## 宏的极端位置 / Macro corners

- 乐器 **Life** 宏的上端约为 Hopf 阈值的 1.35 倍：越过阈值后晶格自激，能量被能量门限制在 E_cap，声音持续（有界，不会失控），松开音符也不会自然结束——这是“活体”音色的设计，不是故障。
- 效果器 **Freeze** 宏推到 1 时晶格无损（mu = eta = 0），声音无限保持；回到较低值即开始衰减。
- 带螺旋波种子的预设（Spiral Drone、Turing Bells、Big Spiral、Spiral Lock 等）Life 设为 0：螺旋波在介质中永久存在，若再加 Life 会永久自持。
