#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 MORPHO-PHS contributors
"""Phase 6b factory bank: original presets designed from MORPHO-PHS's own physics.

Every preset is written out in full (all engine parameters, 8 named macros, Chinese + English text, tags, author,
cpu_class).  Macro positions are *derived* from the preset's own parameter values (a mapped parameter is overridden by
its macro, so the macro must sit where the designed value lies).  out_gain and cpu_class come from measurements
(scripts/presets/bank6b_measured.json, written by fit_gain6b.py / cpu6b.py); until measured, out_gain = 0 dB and
cpu_class = "standard".

usage: bank6b.py [--out presets] [--measured scripts/presets/bank6b_measured.json]   (lists: docs6b.py)
"""
import argparse, json, math, pathlib, re

ROOT = pathlib.Path(__file__).resolve().parents[2]
AUTHOR = "MORPHO-PHS factory (Phase 6b)"
LOG = {"tone", "tau"}
RANGES = dict(tone=(150, 3000), chirality=(-1, 1), nonlinear=(0, 1), hq=(0, 1), decay=(0, 1), life=(0, 1), morph=(0, 1),
              tau=(0.01, 2.0), freeze=(0, 1), wet=(0, 1), dry=(0, 1), spread=(0.05, 0.45), defect_depth=(0, 1),
              edge_width=(0, 2), edge_crosstalk=(0, 1))
MODES = {0: "FX Edge Disperser", 1: "FX Defect Freezer", 2: "Instrument"}
LAT = {8: "8x8", 16: "16x16", 32: "32x32"}
POT = {0: "Quartic", 1: "HQ (fast log-cosh)", 2: "HQ exact"}

def life_critical(decay):
    """Life value where the linear damping mu0 crosses zero (Hopf threshold, docs/phase6/preset_audit.md)."""
    dd = 1.0 - decay; mu0 = -(2.0 + 600.0 * dd ** 3)
    return -mu0 / (120.0 - mu0)

# ---------------------------------------------------------------- geometry helpers (normalised flake coords)
def ring(cx, cy, r, n, ph=0.0):
    return [[round(cx + r * math.cos(ph + 2 * math.pi * k / n), 4), round(cy + r * math.sin(ph + 2 * math.pi * k / n), 4)] for k in range(n)]
def line(x0, y0, x1, y1, n):
    return [[round(x0 + (x1 - x0) * k / (n - 1), 4), round(y0 + (y1 - y0) * k / (n - 1), 4)] for k in range(n)]
def scatter(n, seed, lo=0.12, hi=0.88):
    s = seed; out = []
    for _ in range(n):   # small LCG: deterministic, no numpy dependency
        s = (1103515245 * s + 12345) % (1 << 31); x = lo + (hi - lo) * s / (1 << 31)
        s = (1103515245 * s + 12345) % (1 << 31); y = lo + (hi - lo) * s / (1 << 31)
        out.append([round(x, 4), round(y, 4)])
    return out
def spiral_arm(cx, cy, r0, r1, turns, n):
    return [[round(cx + (r0 + (r1 - r0) * k / (n - 1)) * math.cos(2 * math.pi * turns * k / (n - 1)), 4),
             round(cy + (r0 + (r1 - r0) * k / (n - 1)) * math.sin(2 * math.pi * turns * k / (n - 1)), 4)] for k in range(n)]

# ---------------------------------------------------------------- macro builders
def T(param, lo, hi): return (param, float(lo), float(hi))
def tone_span(t, lo_f=0.5, hi_f=2.0): return T("tone", max(150, round(t * lo_f)), min(3000, round(t * hi_f)))

BANK = []

def P(name, cat, tags, en, zh, mode, L, pot, params, macros, defects=None, morphogen=None, spiral=None, origin=None):
    BANK.append(dict(name=name, cat=cat, tags=tags, en=en, zh=zh, mode=mode, L=L, pot=pot, params=params, macros=macros,
                     defects=defects or [], morphogen=morphogen or [], spiral=spiral, origin=origin))

def base(**kw):
    d = dict(tone=1500.0, chirality=0.6, nonlinear=0.25, hq=0.3, decay=0.6, life=0.0, morph=0.3, tau=0.0833, freeze=0.0,
             wet=0.9, dry=0.0, spread=0.25, exciter=0, defect_depth=0.6, edge_width=1.0, edge_crosstalk=0.0, interp=0, in_gain=0.0)
    d.update(kw); return d

def inst_macros(p, eighth, life_hi=None, names=None, morph_name="Morphogen"):
    """Eight instrument macros.  Life tops out just past the Hopf threshold of the preset's own decay (the macro's last
    quarter is the self-sustaining 'living' state, energy-bounded by the gate)."""
    lc = life_critical(p["decay"])
    lh = life_hi if life_hi is not None else round(min(1.0, max(0.05, lc * 1.35)), 3)
    n = names or ["Brightness", "Chirality", "Sustain", "Grit", morph_name, "Life", "Width", eighth[0]]
    dlo, dhi = max(0.0, p["decay"] - 0.35), min(0.97, p["decay"] + 0.15)
    return [(n[0], [tone_span(p["tone"])]),
            (n[1], [T("chirality", -1.0 if p["chirality"] < 0 else 0.0, 1.0)]),
            (n[2], [T("decay", round(dlo, 3), round(dhi, 3))]),
            (n[3], [T("nonlinear", 0.0, max(0.6, min(1.0, p["nonlinear"] * 2.5 + 0.1)))]),
            (n[4], [T("morph", 0.0, 1.0), T("tau", 1.2, 0.03)]),
            (n[5], [T("life", 0.0, max(lh, p["life"] + 0.01))]),
            (n[6], [T("spread", 0.05, 0.45), T("edge_width", 0.2, 1.8)]),
            (n[7], eighth[1])]

def fx_macros(p, eighth, names=None, mix_lo=0.15):
    n = names or ["Tone", "Chirality", "Mix", "Decay", "Morphogen", "Grit", "Width", eighth[0]]
    dlo, dhi = max(0.0, p["decay"] - 0.4), min(0.98, p["decay"] + 0.15)
    return [(n[0], [tone_span(p["tone"], 0.4, 2.0)]),
            (n[1], [T("chirality", -1.0, 1.0)]),
            (n[2], [T("wet", 0.3, 1.0), T("dry", 1.0, 0.2)]),
            (n[3], [T("decay", round(dlo, 3), round(dhi, 3))]),
            (n[4], [T("morph", 0.0, 1.0), T("tau", 1.0, 0.03)]),
            (n[5], [T("nonlinear", 0.0, 1.0)]),
            (n[6], [T("spread", 0.05, 0.45), T("edge_width", 0.0, 2.0)]),
            (n[7], eighth[1])]

E8_HQ = ("Gap", [T("hq", 0.0, 1.0)])
E8_DEF = ("Defects", [T("defect_depth", 0.0, 1.0)])
E8_XT = ("Crosstalk", [T("edge_crosstalk", 0.0, 1.0)])
E8_FRZ = ("Freeze", [T("freeze", 0.0, 1.0)])

# ======================================================================== INSTRUMENT BANK
# --- Gyro Bells: plucked lattice + strong chirality (gyroscopic coupling splits degenerate modes -> slow beating,
#     inharmonic bell partials); HQ potential adds an on-site gap that stretches the partial series upward.
def gyro(name, tags, en, zh, tone, ch, decay, hq, pot, L=16, nl=0.1, spread=0.3, origin=None, defects=None, dd=0.6):
    p = base(tone=tone, chirality=ch, decay=decay, hq=hq, nonlinear=nl, spread=spread, exciter=1, defect_depth=dd, morph=0.15, tau=0.3)
    P(name, "Gyro Bells", ["bell", "pluck", "chiral"] + tags, en, zh, 2, L, pot, p, inst_macros(p, E8_HQ if pot else E8_DEF), defects=defects, origin=origin)

gyro("Gyro Bell", ["bright"], "Plucked lattice with strong gyroscopic coupling: split modes beat slowly like a struck bell.",
     "强陀螺耦合的拨奏晶格：简并模态被劈裂，像敲击的钟一样缓慢拍频。", 1900, 0.9, 0.88, 0.55, 1, origin="20 Metal Lattice (rework)")
gyro("Counter-Spin Bell", ["dark"], "Chirality reversed: the same bell, but the partials beat the other way around the flake.",
     "手性反向：同一口钟，泛音沿雪花反方向拍动。", 1300, -0.85, 0.86, 0.4, 1, nl=0.05)
gyro("Glass Gong", ["long", "hq"], "Wide on-site gap stretches the partials; long, shimmering gong tail.",
     "宽的在位能隙拉伸泛音列；长而闪烁的锣尾。", 1100, 0.75, 0.93, 0.85, 1, spread=0.4)
gyro("Toy Carillon", ["small", "eco"], "8x8 flake: few modes, clear inharmonic chime; the cheapest bell in the bank.",
     "8x8 小雪花：模态少，清晰的非谐和钟声；库里最省 CPU 的钟。", 2200, 0.8, 0.82, 0.3, 0, L=8)
gyro("Cathedral Gyro 32", ["large", "showcase"], "32x32 flake: dense mode cloud, slow chorus of split partials.",
     "32x32 大雪花：稠密的模态云，劈裂泛音形成缓慢的合唱。", 1000, 0.7, 0.9, 0.5, 1, L=32, spread=0.35)

# --- Plucks: short raised-cosine strikes; pitch-dependent strike length colours which modes ring.
def pluck(name, tags, en, zh, tone, ch, decay, nl=0.15, spread=0.25, pot=0, hq=0.3, defects=None, dd=0.6, origin=None, L=16):
    p = base(tone=tone, chirality=ch, decay=decay, nonlinear=nl, spread=spread, exciter=1, hq=hq, defect_depth=dd, morph=0.1, tau=0.5)
    P(name, "Plucks", ["pluck"] + tags, en, zh, 2, L, pot, p, inst_macros(p, E8_DEF if defects else E8_XT), defects=defects, origin=origin)

pluck("Honeycomb Harp", ["warm"], "Soft plucks on the honeycomb; mild chirality keeps the decay slightly swirling.",
      "蜂窝上的柔和拨奏；轻微手性让余音略带旋动。", 1500, 0.45, 0.8, origin="18 Honeycomb Harp (rework)")
pluck("Snowflake Koto", ["bright", "narrow"], "Narrow pickups near the centre: nasal, koto-like attack with a quick bloom.",
      "拾音点贴近中心：鼻音般、类似古筝的起音，随后快速绽开。", 2100, 0.3, 0.7, nl=0.3, spread=0.1)
pluck("Felt Lattice", ["soft", "dark"], "Low tone and short decay: muted, felt-covered plucks.",
      "低音调、短衰减：像包了毛毡的闷音拨奏。", 600, 0.2, 0.62, nl=0.05, spread=0.35)
pluck("Bent Tine", ["metallic", "hq"], "HQ gap plus nonlinearity: tine-like pluck whose pitch bends as it rings out.",
      "HQ 能隙加非线性：像音叉齿的拨奏，振幅衰减时音高随之弯曲。", 1700, 0.55, 0.78, nl=0.6, pot=1, hq=0.6)

# --- Percussive Lattice: heavy defects localise modes (Anderson-like trapping) -> short tuned hits.
def perc(name, tags, en, zh, tone, decay, defects, dd, nl=0.25, ch=0.3, spread=0.25, origin=None, L=16, pot=0, hq=0.3):
    p = base(tone=tone, chirality=ch, decay=decay, nonlinear=nl, spread=spread, exciter=1, defect_depth=dd, hq=hq, morph=0.05, tau=0.8)
    P(name, "Percussive Lattice", ["percussion", "defects"] + tags, en, zh, 2, L, pot, p, inst_macros(p, E8_DEF), defects=defects, origin=origin)

perc("Defect Kalimba", ["tuned"], "A row of heavy defects traps modes in pockets: short, tuned, kalimba-like hits.",
     "一排重缺陷把模态困在小腔里：短促、有音高、像拇指琴的敲击。", 1200, 0.7, line(0.15, 0.35, 0.85, 0.35, 7), 1.0, origin="19 Defect Kalimba (rework)")
perc("Low Mallets", ["low", "wood"], "Low tone, scattered defects: woody marimba-ish mallets.",
     "低音调、散布缺陷：带木质感的马林巴式槌击。", 380, 0.64, scatter(6, 7), 0.8, nl=0.1, origin="21 Low Mallets (rework)")
perc("Shard Clave", ["short", "bright"], "Dense random defects and fast damping: dry, glassy clicks with pitch.",
     "密集随机缺陷与快速阻尼：干脆、玻璃质、带音高的咔哒声。", 2400, 0.62, scatter(12, 31), 1.0, nl=0.3, ch=0.1)
perc("Ring Cavity Tom", ["drum", "low"], "A ring of defects forms a drum-head cavity; the voice inside booms.",
     "缺陷围成的圆环像鼓皮腔体；腔内的声部低沉轰鸣。", 450, 0.7, ring(0.5, 0.5, 0.28, 12), 1.0, nl=0.45, ch=0.15, spread=0.15)

# --- Chiral Strings: bowed (PolyBLEP saw) through the lattice; chirality twists the bow's harmonics into the bands.
def bowed(name, cat, tags, en, zh, tone, ch, decay, nl=0.2, spread=0.25, pot=0, hq=0.3, morph=0.2, tau=0.3, life=0.0, origin=None, L=16, dd=0.6, defects=None, eighth=None, morphogen=None, spiral=None, xt=0.0, width=1.0):
    p = base(tone=tone, chirality=ch, decay=decay, nonlinear=nl, spread=spread, exciter=0, hq=hq, morph=morph, tau=tau, life=life,
             defect_depth=dd, edge_crosstalk=xt, edge_width=width)
    e8 = eighth or (E8_HQ if pot else E8_XT)
    P(name, cat, ["bowed"] + tags, en, zh, 2, L, pot, p, inst_macros(p, e8), defects=defects, morphogen=morphogen, spiral=spiral, origin=origin)

bowed("Chiral Cello", "Chiral Strings", ["warm", "low"], "A bowed saw through a low-tuned chiral flake: woody, slightly twisted cello body.",
      "弓擦锯齿波穿过低调手性雪花：木质、略带扭转感的大提琴腔体。", 700, 0.6, 0.72, origin="14 Chiral Cello (keep, re-gain)")
bowed("Rusty Bow", "Chiral Strings", ["grit", "dark"], "High quartic nonlinearity: the bow scrapes, partials crowd and growl.",
      "高四次非线性：弓在刮擦，泛音拥挤并发出低吼。", 600, 0.4, 0.68, nl=0.85, origin="17 Rusty Bow (rework)")
bowed("Viola Gyre", "Chiral Strings", ["mid", "chiral"], "Mid register; strong chirality pulls the bowed tone into a rotating chorus.",
      "中音区；强手性把弓擦音拉成旋转的合唱感。", 1100, 0.9, 0.75, nl=0.15, spread=0.33)
bowed("Glass Harmonica", "Chiral Strings", ["glass", "hq", "bright"], "HQ gap and a high tone: wet-finger glass, pure and slightly unstable.",
      "HQ 能隙加高音调：湿指摩擦玻璃般纯净，略带不稳定。", 2200, 0.7, 0.8, nl=0.05, pot=1, hq=0.45, origin="15 Glass Harmonica (rework)")
bowed("Sul Ponticello", "Chiral Strings", ["thin", "bright"], "Pickups spread to the rim: thin, glassy bowing near the bridge.",
      "拾音点外移至边缘：贴近琴码的纤细玻璃质弓音。", 1800, 0.35, 0.65, nl=0.3, spread=0.44, width=1.5)

# --- Edge / Topological (instrument): very strong chirality opens a topological gap; in-gap energy rides the rim.
bowed("Rim Rider", "Edge / Topological", ["edge", "chiral", "wide"], "Maximum chirality: the bowed energy rides the flake's rim, pickups at the edge hear it circle.",
      "最大手性：弓擦能量沿雪花边缘传播，边缘拾音点听到它绕圈。", 1400, 1.0, 0.82, nl=0.1, spread=0.45, width=1.6)
bowed("Mirror Rim", "Edge / Topological", ["edge", "chiral"], "The same edge, counter-rotating, with crosstalk folding the two pickups together.",
      "同一条边缘反向旋转，串扰把两个拾音点折叠在一起。", 1400, -1.0, 0.82, nl=0.1, spread=0.45, xt=0.35)
gyro("Edge Chime", ["edge", "topological"], "Plucked at full chirality: chime partials that keep travelling around the rim as they fade.",
     "满手性拨奏：钟声泛音在衰减时仍沿边缘行进。", 1700, 1.0, 0.87, 0.35, 0, spread=0.45)
BANK[-1]["cat"] = "Edge / Topological"

# --- Excitable Textures: sub-critical Life (damping just below zero) + an active Barkley medium; notes trigger
#     morphogen waves that briefly push local damping negative -> blooming, grainy textures that still decay.
def excitable(name, tags, en, zh, tone, decay, life_frac, morph, tau, exc=1, nl=0.25, ch=0.5, pot=0, hq=0.3, morphogen=None, L=16, spread=0.25, origin=None, cat="Excitable Textures", spiral=None):
    life = round(life_critical(decay) * life_frac, 4)
    p = base(tone=tone, chirality=ch, decay=decay, nonlinear=nl, exciter=exc, hq=hq, morph=morph, tau=tau, life=life, spread=spread)
    P(name, cat, ["excitable", "morphogen"] + tags, en, zh, 2, L, pot, p,
      inst_macros(p, E8_HQ if pot else E8_DEF), morphogen=morphogen, spiral=spiral, origin=origin)

excitable("Ember Field", ["warm", "pluck"], "Plucks kick off morphogen waves that briefly fan the lattice like embers, then cool.",
          "拨奏激发形态场波，像余烬被短暂扇旺，随后冷却。", 1300, 0.75, 0.8, 0.45, 0.5,
          morphogen=[[0.3, 0.6, 0.8]], origin="23 Ember Field (rework: sub-critical Life)")
excitable("Static Bloom", ["grainy", "bright"], "Fast Barkley medium (short tau): crackling blooms of grain after every note.",
          "快速 Barkley 介质（短 tau）：每个音符后都绽开噼啪作响的颗粒。", 1900, 0.7, 0.85, 0.85, 0.04, nl=0.4)
excitable("Slow Tide", ["slow", "bowed", "pad"], "Slow medium (long tau) under a bow: the timbre swells and recedes in tides.",
          "慢速介质（长 tau）配合弓擦：音色像潮汐一样涨落。", 900, 0.8, 0.7, 0.45, 1.2, exc=0, nl=0.15)
excitable("Nerve Net", ["hq", "pluck", "restless"], "HQ potential + a twitchy medium: plucks fire nerve-like volleys across the flake.",
          "HQ 势加敏感介质：拨奏在雪花上激发神经脉冲般的连发。", 1600, 0.72, 0.9, 0.9, 0.08, nl=0.3, ch=0.8, pot=1, hq=0.45,
          origin="24 Restless Swarm (rework: sub-critical Life)")

# --- Drones: bowed, long decay, Life close to (but below) the Hopf threshold: the field nearly sustains itself,
#     so releases are long but finite (no endless self-oscillation after note-off).
def drone(name, tags, en, zh, tone, decay, life_frac, ch=0.6, nl=0.2, morph=0.35, tau=0.6, pot=0, hq=0.3, L=16, spread=0.3, spiral=None, morphogen=None, origin=None, exc=0):
    life = round(life_critical(decay) * life_frac, 4)
    p = base(tone=tone, chirality=ch, decay=decay, nonlinear=nl, exciter=exc, hq=hq, morph=morph, tau=tau, life=life, spread=spread)
    P(name, "Drones", ["drone", "pad", "long"] + tags, en, zh, 2, L, pot, p, inst_macros(p, E8_HQ if pot else E8_XT),
      spiral=spiral, morphogen=morphogen, origin=origin)

drone("Spiral Drone", ["spiral", "low"], "A seeded spiral wave turns slowly in the medium, sweeping the bowed drone's colour.",
      "介质中植入的螺旋波缓慢旋转，扫动弓擦长音的音色。", 900, 0.78, 0.0, spiral=[0.5, 0.5], tau=0.3, morph=0.5,
      origin="22 Spiral Drone (rework: sub-critical Life)")
drone("Low Orbit", ["sub", "dark"], "Very low tone, near-critical Life: a sub-heavy drone that hangs on after release.",
      "极低音调、接近临界的 Life：释放后仍久久不散的厚重低频长音。", 300, 0.8, 0.75, ch=0.5, nl=0.1)
drone("Halo Choir", ["choir", "hq", "bright"], "HQ gap, wide pickups, slow morphogen: airy, vowel-like choir pad.",
      "HQ 能隙、宽拾音、慢形态场：空灵、带元音感的合唱铺底。", 1400, 0.82, 0.5, ch=0.75, pot=1, hq=0.4, spread=0.42, morph=0.3, tau=0.9,
      origin="16 Twisted Choir (rework)")
# Phase 6b2: was "Big Spiral 32" (32x32, HQ fast, 3 threads).  On the i9 its spiral control path gave a p99/budget of
# 1.226 with 56 overruns (the per-tick Barkley/map work is serial on the audio thread), so it now runs at 16x16 HQ fast,
# 1 thread.  Same tone, decay, spiral seed and morph; the 16x16 render has the same centroid (771 vs 742 Hz) and roll-off.
drone("Big Spiral", ["spiral", "showcase"], "Snowflake with a seeded spiral: a slowly rotating, dense drone.",
      "雪花加螺旋种子：缓慢旋转的稠密长音。", 1100, 0.78, 0.0, pot=1, hq=0.3, L=16, spiral=[0.45, 0.55], morph=0.55, tau=0.2,
      origin="26 Big Spiral 32 (rework: sub-critical Life; 6b2: 16x16 HQ fast for the i9 CPU budget)")

# --- Morph Evolutions: the Barkley medium drives g / mu / alpha modulation (morph) while notes hold; seeded stimuli
#     make each preset evolve on its own clock.
drone("Morphogen Tapestry", ["evolving", "slow"], "Two morphogen sources collide; the bowed tone slowly reweaves its partials.",
      "两个形态场源相撞；弓擦音慢慢重新编织泛音。", 1200, 0.75, 0.6, morph=0.85, tau=0.5, morphogen=[[0.2, 0.3, 1.0], [0.8, 0.7, 1.0]], ch=0.55)
BANK[-1]["cat"] = "Morph Evolutions"; BANK[-1]["tags"] = ["bowed", "evolving", "morphogen", "slow"]
excitable("Turing Bells", ["bell", "evolving"], "Plucked bells over a spiral-seeded medium: each strike lands on a different texture.",
          "在植入螺旋的介质上拨奏的钟：每一次敲击落在不同的纹理上。", 1700, 0.85, 0.0, 0.8, 0.15, ch=0.85, cat="Morph Evolutions", spiral=[0.35, 0.4])
excitable("Breathing Glass", ["glass", "evolving", "bowed", "bright"], "Bowed high glass whose brightness breathes with the medium's slow waves.",
          "弓擦高音玻璃，其明亮度随介质慢波呼吸。", 2200, 0.78, 0.0, 0.45, 1.5, exc=0, pot=0, ch=0.65, cat="Morph Evolutions")

# ======================================================================== FX BANK
def fx(name, cat, tags, en, zh, mode, tone, ch, decay, eighth, nl=0.25, hq=0.3, pot=0, wet=0.8, dry=0.43, spread=0.25, morph=0.2,
       tau=0.3, freeze=0.0, life=0.0, dd=0.6, L=16, defects=None, morphogen=None, spiral=None, origin=None, xt=0.0, width=1.0, mix_lo=0.15):
    p = base(tone=tone, chirality=ch, decay=decay, nonlinear=nl, hq=hq, wet=wet, dry=dry, spread=spread, morph=morph, tau=tau,
             freeze=freeze, life=life, defect_depth=dd, edge_crosstalk=xt, edge_width=width)
    P(name, cat, tags, en, zh, mode, L, pot, p, fx_macros(p, eighth, mix_lo=mix_lo), defects=defects, morphogen=morphogen, spiral=spiral, origin=origin)

# --- Edge Dispersers (mode 0): the input's in-gap band is launched into the chiral edge state at the bottom rim; the
#     edge is one-way (chirality sign = direction), dispersive, and immune to the absorbing bulk.
ED = "Edge Dispersers"
fx("Edge Shimmer", ED, ["edge", "bright", "dispersion"], "In-gap bands ride the one-way chiral edge: a bright, dispersed ring beside the dry signal.",
   "能隙内频带沿单向手性边缘传播：在干声旁形成明亮、色散的鸣响。", 0, 1500, 0.7, 0.6, E8_XT, origin="01 Edge Shimmer + 02 Reverse Edge (merged)")
fx("Low Edge Rumble", ED, ["edge", "low", "dark"], "Edge tuned low: a dark, slowly dispersing rumble under kick and bass.",
   "边缘调低：在底鼓与贝斯下方缓慢色散的暗沉低鸣。", 0, 420, 0.8, 0.75, E8_XT, nl=0.15, origin="03 Low Edge Rumble (rework)")
fx("Glass Corridor", ED, ["edge", "hq", "glass"], "HQ gap lifts the edge band: long, glassy corridor with a high, pure tail.",
   "HQ 能隙抬高边缘频带：长而透明的玻璃走廊，尾音高而纯。", 0, 2600, 0.9, 0.85, E8_HQ, pot=1, hq=0.25, origin="04 Glass Corridor (keep, re-gain)")
fx("Grit Edge", ED, ["edge", "grit", "drive"], "Quartic nonlinearity on the edge: transients come back crunchy and pitch-smeared.",
   "边缘上的四次非线性：瞬态返回时带着颗粒感与音高涂抹。", 0, 1100, 0.6, 0.5, E8_XT, nl=0.85, origin="06 Grit Edge (rework)")
fx("Pinhole Edge", ED, ["edge", "defects", "comb"], "Defects pinch the rim: the edge wave scatters at the pinholes into comb-like echoes.",
   "缺陷夹窄了边缘：边缘波在针孔处散射成梳状回声。", 0, 1600, 0.75, 0.65, E8_DEF, dd=0.8,
   defects=[[0.3, 0.05], [0.5, 0.05], [0.7, 0.05]], origin="08 Pinhole Edge (rework)")
fx("Tiny Edge 8", ED, ["edge", "small", "eco"], "8x8 flake: a short rim, tight flutter of edge echoes; the lightest FX.",
   "8x8 小雪花：短边缘、紧凑的边缘回声颤动；最轻量的效果器。", 0, 1500, 0.7, 0.6, E8_XT, L=8, origin="27 Tiny Edge 8 (rework)")
fx("Long Edge 32", ED, ["edge", "large", "showcase"], "32x32 rim: four times the path, so the dispersion stretches into a slow, sweeping tail.",
   "32x32 边缘：路径长四倍，色散被拉成缓慢扫动的尾音。", 0, 1500, 0.7, 0.7, E8_XT, L=32, spread=0.3, origin="25 Big Edge 32 (rework)")

# --- Defect Freezers (mode 1): Freeze removes damping (mu, eta -> 0): the lattice becomes lossless and holds what it
#     heard; defects decide which modes are trapped.  Default positions decay (finite tails); Freeze macro -> 1 holds.
DF = "Defect Freezers"
fx("Freeze Halo", DF, ["freeze", "halo", "defects"], "Partial freeze around a defect ring: a soft halo that hangs on after each hit.",
   "缺陷环周围的部分冻结：每次敲击后悬停的柔和光晕。", 1, 900, 0.5, 0.8, E8_FRZ, freeze=0.6, defects=ring(0.5, 0.5, 0.3, 8),
   origin="09 Freeze Halo (rework: finite tail by default)")
fx("Crystal Cage", DF, ["freeze", "defects", "cavity"], "Heavy defect cavity: frozen modes stay trapped inside and ring as a crystal chord.",
   "重缺陷腔体：冻结的模态被困在腔内，像水晶和弦般鸣响。", 1, 1400, 0.3, 0.6, E8_FRZ, freeze=0.5, dd=1.0, defects=ring(0.5, 0.5, 0.22, 14),
   origin="10 Crystal Cage (rework)")
fx("Soft Hold", DF, ["freeze", "soft", "pad"], "Gentle freeze with no defects: the whole flake sustains a soft, blurred hold.",
   "无缺陷的轻度冻结：整片雪花维持柔和、模糊的延音。", 1, 700, 0.4, 0.75, E8_FRZ, freeze=0.45, origin="11 Soft Hold (keep, re-gain)")
fx("Shattered Ice", DF, ["freeze", "hq", "grainy"], "Scattered defects + HQ: a frozen field full of grainy, glinting partials.",
   "散布缺陷加 HQ：充满颗粒与闪光泛音的冻结场。", 1, 1800, 0.6, 0.75, E8_FRZ, nl=0.5, hq=0.5, pot=1, freeze=0.5, defects=scatter(8, 5),
   origin="12 Shattered Ice (rework)")
fx("Spiral Lock", DF, ["freeze", "defects", "spiral"], "Defects laid along a spiral arm: the frozen tone winds outwards as it fades.",
   "缺陷沿螺旋臂排布：冻结的音色在消退时向外盘旋。", 1, 1100, 0.8, 0.6, E8_FRZ, freeze=0.55, dd=0.9, defects=spiral_arm(0.5, 0.5, 0.05, 0.38, 1.5, 12))
fx("Glacier 32", DF, ["freeze", "large", "showcase"], "32x32 freeze: a vast, slow glacier of held modes with a line of fault defects.",
   "32x32 冻结：带一条断层缺陷的巨大、缓慢的冻结模态冰川。", 1, 800, 0.5, 0.82, E8_FRZ, L=32, freeze=0.55, defects=line(0.1, 0.6, 0.9, 0.4, 10))

# --- Lattice Spaces (mode 1, no freeze): the flake as a resonant body/room; chirality turns it into a 'gyro room'.
LS = "Lattice Spaces"
fx("Bulk Smear", LS, ["space", "smear", "dark"], "Low chirality bulk body: a short, smeared resonance that thickens drums.",
   "低手性体态腔体：短而涂抹的共振，让鼓更厚实。", 1, 1200, 0.2, 0.45, E8_DEF, wet=0.6, dry=0.7, origin="13 Bulk Smear (rework)")
fx("Honeycomb Room", LS, ["space", "room", "warm"], "Medium decay, wide pickups: the flake as a small warm wooden room.",
   "中等衰减、宽拾音：把雪花当作一间温暖的小木屋。", 1, 950, 0.35, 0.62, E8_DEF, spread=0.4, width=1.4)
fx("Gyro Chamber", LS, ["space", "chiral", "wide"], "Strong chirality in the bulk: reflections swirl and the stereo image rotates.",
   "体态中的强手性：反射盘旋，立体声像随之旋转。", 1, 1300, 0.95, 0.68, E8_XT, spread=0.38, width=1.6)
fx("Dark Plate", LS, ["space", "plate", "long"], "Low tone, long decay: a dark plate-like bloom under vocals or keys.",
   "低音调、长衰减：垫在人声或键盘下方的暗色板式混响。", 1, 520, 0.45, 0.8, E8_DEF, wet=0.8, dry=0.6, nl=0.1)

# --- Excitable FX: sub-critical Life in an effect: loud input fans the medium, the lattice briefly 'comes alive'.
EX = "Excitable FX"
def fx_ex(name, tags, en, zh, mode, tone, decay, life_frac, morph, tau, eighth, **kw):
    fx(name, EX, ["excitable", "morphogen"] + tags, en, zh, mode, tone, kw.pop("ch", 0.6), decay, eighth,
       life=round(life_critical(decay) * life_frac, 4), morph=morph, tau=tau, **kw)
fx_ex("Ember Echo", ["warm", "bloom"], "Hits fan morphogen waves; the echo glows a little louder before it cools.",
      "敲击扇起形态场波；回声在冷却前会微微变亮。", 1, 1300, 0.7, 0.8, 0.7, 0.4, E8_DEF)
fx_ex("Static Gate", ["grainy", "rhythmic"], "Fast medium: every transient leaves a short burst of crackle, then silence.",
      "快速介质：每个瞬态留下短暂的噼啪声，随后归于寂静。", 1, 1900, 0.5, 0.85, 0.9, 0.04, E8_DEF, nl=0.5)
fx_ex("Living Edge", ["edge", "bloom"], "Edge disperser with near-critical Life: the rim ring swells after the input.",
      "接近临界 Life 的边缘色散器：输入之后边缘鸣响会膨胀。", 0, 1400, 0.7, 0.85, 0.6, 0.3, E8_XT, ch=0.85)
fx_ex("Spark Chamber", ["hq", "bright"], "HQ bulk with a twitchy medium: sparks fly off transients in the high band.",
      "HQ 体态配敏感介质：瞬态在高频带上迸出火花。", 1, 2300, 0.65, 0.8, 0.85, 0.08, E8_HQ, pot=1, hq=0.5, ch=0.7)

# --- Morph Evolutions FX: morphogen-driven modulation of g / mu / alpha; seeded stimuli give each its own motion.
ME = "Morph Evolutions"
fx("Breathing Edge", ME, ["edge", "evolving", "morphogen"], "Morphogen waves modulate the edge's damping: the shimmer breathes on its own.",
   "形态场波调制边缘阻尼：鸣响自行呼吸。", 0, 1300, 0.7, 0.62, E8_XT, morph=0.75, tau=0.25, morphogen=[[0.3, 0.2, 1.0], [0.7, 0.2, 1.0]],
   origin="05 Breathing Edge (rework)")
fx("Tidal Freeze", ME, ["freeze", "evolving", "slow"], "A slow medium modulates a partial freeze: held tones ebb and flow.",
   "慢速介质调制部分冻结：保持的音像潮水般起落。", 1, 1000, 0.5, 0.78, E8_FRZ, freeze=0.45, morph=0.8, tau=1.3, morphogen=[[0.5, 0.5, 1.0]])
fx("Turing Smear", ME, ["space", "evolving", "spiral"], "A spiral wave turns in the medium; the body's colour rotates with it.",
   "介质中旋转的螺旋波带动腔体音色一同旋转。", 1, 1250, 0.6, 0.65, E8_DEF, morph=0.85, tau=0.15, spiral=[0.5, 0.5])
fx("Chiral Drift", ME, ["edge", "evolving", "chiral"], "Morphogen modulates chirality itself: the edge band drifts in pitch and direction.",
   "形态场直接调制手性：边缘频带在音高与方向上漂移。", 0, 1700, 0.5, 0.7, E8_XT, morph=0.95, tau=0.6, spiral=[0.4, 0.3])

# ======================================================================== writer
def inv(param, lo, hi, v):
    if param in LOG: return math.log(v / lo) / math.log(hi / lo)
    return (v - lo) / (hi - lo)
def fwd(param, lo, hi, x):
    if param in LOG: return lo * (hi / lo) ** x
    return lo + (hi - lo) * x

def slug(s): return re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_")

def build(pr, measured):
    p = dict(pr["params"])
    assert not (pr["spiral"] and p["life"] > 0), (pr["name"], "spiral seed + Life > 0 self-sustains forever")
    macros, names, positions = [], [], []
    owners = set()
    for k, (nm, targets) in enumerate(pr["macros"]):
        names.append(nm)
        par0, lo0, hi0 = targets[0]
        v0 = p[par0]
        assert min(lo0, hi0) - 1e-9 <= v0 <= max(lo0, hi0) + 1e-9, (pr["name"], nm, par0, v0, lo0, hi0)
        x = round(min(1.0, max(0.0, inv(par0, lo0, hi0, v0))), 4)
        positions.append(x)
        for (par, lo, hi) in targets:
            assert par not in owners, (pr["name"], par); owners.add(par)
            rlo, rhi = RANGES[par]; assert rlo <= min(lo, hi) and max(lo, hi) <= rhi, (pr["name"], par, lo, hi)
            p[par] = fwd(par, lo, hi, x)
            macros.append({"macro": k + 1, "param": par, "lo": round(lo, 4), "hi": round(hi, 4)})
    m = measured.get(pr["name"], {})
    params = {"mode": MODES[pr["mode"]], "lattice": LAT[pr["L"]], "threads": "Auto", "potential": POT[pr["pot"]],
              "interp": ["1 sample", "8 samples", "Off (64)"][p["interp"]]}
    for k in ["tone", "chirality", "nonlinear", "hq", "decay", "life", "morph", "tau", "freeze", "wet", "dry", "in_gain"]:
        params[k] = round(float(p[k]), 4)
    params["out_gain"] = round(float(m.get("out_gain", 0.0)), 2)
    params["spread"] = round(float(p["spread"]), 4)
    params["exciter"] = ["Bow (saw)", "Pluck"][p["exciter"]]
    params["defect_depth"] = round(float(p["defect_depth"]), 4)
    for k in range(8): params["macro%d" % (k + 1)] = positions[k]
    params["edge_width"] = round(float(p["edge_width"]), 4); params["edge_crosstalk"] = round(float(p["edge_crosstalk"]), 4)
    st = {"defects": pr["defects"], "morphogen": pr["morphogen"]}
    if pr["spiral"]: st["seed_spiral"] = pr["spiral"]
    return {"format": "morpho-phs-preset", "version": 2, "name": pr["name"], "category": pr["cat"], "author": AUTHOR,
            "description": pr["en"], "description_zh": pr["zh"], "tags": sorted(set(pr["tags"])), "cpu_class": m.get("cpu_class", "standard"),
            "params": params, "macros": {"names": names, "targets": macros}, "lattice_state": st}

def ordered():
    inst = [b for b in BANK if b["mode"] == 2]; fxs = [b for b in BANK if b["mode"] != 2]
    cat_order = ["Gyro Bells", "Plucks", "Percussive Lattice", "Chiral Strings", "Edge / Topological", "Excitable Textures", "Drones",
                 "Morph Evolutions", "Edge Dispersers", "Defect Freezers", "Lattice Spaces", "Excitable FX"]
    key = lambda b: cat_order.index(b["cat"]) if b["cat"] in cat_order else 99
    return sorted(inst, key=key) + sorted(fxs, key=lambda b: (key(b) if b["cat"] != "Morph Evolutions" else 98))

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--out", default=str(ROOT / "presets"))
    ap.add_argument("--measured", default=str(ROOT / "scripts/presets/bank6b_measured.json")); a = ap.parse_args()
    mp = pathlib.Path(a.measured); measured = json.loads(mp.read_text()) if mp.exists() else {}
    out = pathlib.Path(a.out); out.mkdir(parents=True, exist_ok=True)
    for f in out.glob("*.morpho"): f.unlink()
    names = set()
    for i, pr in enumerate(ordered()):
        assert pr["name"] not in names, pr["name"]; names.add(pr["name"])
        num = (i + 1) if pr["mode"] == 2 else (101 + i - sum(1 for b in BANK if b["mode"] == 2))
        (out / ("%03d_%s.morpho" % (num, slug(pr["name"])))).write_text(json.dumps(build(pr, measured), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("%d presets (%d instrument, %d FX) -> %s" % (len(BANK), sum(1 for b in BANK if b["mode"] == 2), sum(1 for b in BANK if b["mode"] != 2), out))

if __name__ == "__main__":
    main()
