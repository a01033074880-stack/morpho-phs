# SPDX-License-Identifier: AGPL-3.0-only
"""Generate the MORPHO-PHS factory presets (presets/*.morpho, spec: docs/morpho_preset_spec.md).

Every preset lists all engine parameters explicitly (no reliance on defaults) and assigns 4-8 macros.
Run:  python python/scripts/phase3/p3_make_presets.py
"""
import json, math, pathlib

ROOT = pathlib.Path(__file__).resolve().parents[3]
OUT = ROOT / "presets"

DEFAULTS = dict(mode="FX Edge Disperser", lattice="16x16", threads="Auto", potential="Quartic", interp="8 samples",
                tone=1500.0, chirality=0.6, nonlinear=0.25, hq=0.3, decay=0.6, life=0.0, morph=0.3, tau=0.0833,
                freeze=0.0, wet=0.7, dry=0.5, in_gain=0.0, out_gain=0.0, spread=0.25, exciter="Bow (saw)", defect_depth=0.6,
                macro1=0.5, macro2=0.5, macro3=0.5, macro4=0.5, macro5=0.5, macro6=0.5, macro7=0.5, macro8=0.5)

def ring(cx, cy, r, n):
    return [[round(cx + r * math.cos(2 * math.pi * k / n), 4), round(cy + r * math.sin(2 * math.pi * k / n), 4)] for k in range(n)]

def line(x0, y0, x1, y1, n):
    return [[round(x0 + (x1 - x0) * k / (n - 1), 4), round(y0 + (y1 - y0) * k / (n - 1), 4)] for k in range(n)]

def M(names, targets):
    return {"names": names + [""] * (8 - len(names)),
            "targets": [{"macro": m, "param": p, "lo": lo, "hi": hi} for (m, p, lo, hi) in targets]}

# Standard macro sets (macro value 0.5 at load = preset's own value is NOT implied: a mapped param is overridden)
def fx_edge_macros(tone_lo, tone_hi, ch):
    return M(["Tone", "Chirality", "Edge Mix", "Decay", "Morphogen", "Grit"],
             [(1, "tone", tone_lo, tone_hi), (2, "chirality", -ch, ch), (3, "wet", 0.2, 1.0), (3, "dry", 0.8, 0.2),
              (4, "decay", 0.2, 0.95), (5, "morph", 0.0, 0.9), (5, "tau", 0.5, 0.05), (6, "nonlinear", 0.0, 0.9)])

def freezer_macros():
    return M(["Freeze", "Tone", "Defects", "Decay", "Morphogen", "Mix"],
             [(1, "freeze", 0.0, 1.0), (2, "tone", 300.0, 2500.0), (3, "defect_depth", 0.0, 1.0), (4, "decay", 0.3, 0.98),
              (5, "morph", 0.0, 0.9), (6, "wet", 0.0, 1.0), (6, "dry", 1.0, 0.0)])

def inst_macros(tone_lo=300.0, tone_hi=2400.0):
    return M(["Brightness", "Twist", "Body", "Sustain", "Life", "Morph Speed", "Grit"],
             [(1, "tone", tone_lo, tone_hi), (2, "chirality", -1.0, 1.0), (3, "defect_depth", 0.0, 1.0), (4, "decay", 0.3, 0.97),
              (5, "life", 0.0, 0.5), (6, "tau", 1.5, 0.02), (7, "nonlinear", 0.0, 0.9), (7, "hq", 0.0, 0.8)])

def macro_values_from(params, macros):
    """Choose each macro's stored value so that, on load, mapped params equal the preset's own values (first target wins)."""
    out = {}
    log = {"tone", "tau"}
    for t in macros["targets"]:
        k = f"macro{t['macro']}"
        if k in out:
            continue
        v, lo, hi = params[t["param"]], t["lo"], t["hi"]
        if t["param"] in log:
            x = math.log(v / lo) / math.log(hi / lo)
        else:
            x = (v - lo) / (hi - lo) if hi != lo else 0.5
        out[k] = round(min(1.0, max(0.0, x)), 4)
    return out

# Output trims (dB) from the 18:52 level audit of the preset renders (experiments/results/phase3/render_levels.txt):
# post-lattice gain only, so the sound is unchanged; aims for peaks below the 0.9 safety limiter.
TRIM = {"Freeze Halo": 4.0, "Crystal Cage": -12.0, "Chiral Cello": -6.0, "Glass Harmonica": -7.0, "Twisted Choir": -9.0,
        "Rusty Bow": -4.0, "Defect Kalimba": 13.0, "Metal Lattice": 11.0, "Low Mallets": 8.0}

P = []
def add(name, cat, desc, params, macros, defects=None, morph=None, spiral=None):
    p = dict(DEFAULTS); p.update(params)
    if name in TRIM: p["out_gain"] = TRIM[name]
    p.update(macro_values_from(p, macros))
    st = {"defects": defects or [], "morphogen": morph or []}
    if spiral: st["seed_spiral"] = spiral
    P.append({"format": "morpho-phs-preset", "version": 1, "name": name, "category": cat, "author": "MORPHO-PHS factory",
              "description": desc, "params": p, "macros": macros, "lattice_state": st})

# ---------------- FX: Edge Disperser (parallel in-gap channel) ----------------
add("Edge Shimmer", "FX Edge", "In-gap bands ride the chiral edge; bright, dispersed echo of the transients beside the dry signal.",
    dict(tone=1500, chirality=0.7, decay=0.55, morph=0.2, wet=0.7, dry=0.6), fx_edge_macros(600, 2600, 1.0))
add("Reverse Edge", "FX Edge", "Chirality flipped: the edge current runs the other way, swapping the L/R arrival order.",
    dict(tone=1500, chirality=-0.7, decay=0.55, wet=0.7, dry=0.6), fx_edge_macros(600, 2600, 1.0))
add("Low Edge Rumble", "FX Edge", "Tone lowered so the gaps sit in the low-mids; slow dark dispersion under drums.",
    dict(tone=420, chirality=0.8, decay=0.75, wet=0.75, dry=0.6, nonlinear=0.15), fx_edge_macros(200, 1200, 1.0))
add("Glass Corridor", "FX Edge", "High tone, long decay: glassy in-gap tails travelling around the edge.",
    dict(tone=2600, chirality=0.9, decay=0.85, wet=0.65, dry=0.55, potential="HQ (fast log-cosh)", hq=0.25), fx_edge_macros(1200, 3000, 1.0))
add("Breathing Edge", "FX Edge", "Morphogen waves flip the local chirality, so the edge path opens and closes over time.",
    dict(tone=1300, chirality=0.7, decay=0.6, morph=0.75, tau=0.25, wet=0.7, dry=0.55),
    fx_edge_macros(500, 2400, 1.0), morph=[[0.3, 0.5, 0.8], [0.7, 0.5, 0.8]])
add("Grit Edge", "FX Edge", "High nonlinearity on the edge channel; transients bend and smear.",
    dict(tone=1100, chirality=0.6, decay=0.5, nonlinear=0.85, wet=0.6, dry=0.6), fx_edge_macros(400, 2200, 1.0))
add("Wide Pickups", "FX Edge", "Pickups far apart along the edge: large inter-channel delay, wide image.",
    dict(tone=1500, chirality=0.65, decay=0.6, spread=0.42, wet=0.7, dry=0.5), fx_edge_macros(600, 2600, 1.0))
add("Pinhole Edge", "FX Edge", "A short defect line near the source scatters part of the edge current.",
    dict(tone=1600, chirality=0.75, decay=0.65, defect_depth=0.8, wet=0.7, dry=0.5), fx_edge_macros(600, 2600, 1.0),
    defects=line(0.35, 0.12, 0.45, 0.2, 3))

# ---------------- FX: Defect Freezer ----------------
add("Freeze Halo", "FX Freezer", "Freeze at 0.9: the lattice becomes nearly lossless and holds the field as a halo.",
    dict(mode="FX Defect Freezer", tone=900, chirality=0.5, decay=0.85, freeze=0.9, wet=0.75, dry=0.5), freezer_macros(),
    defects=ring(0.5, 0.5, 0.22, 8))
add("Crystal Cage", "FX Freezer", "Ring of heavy defects forms a cavity; frozen modes stay trapped inside.",
    dict(mode="FX Defect Freezer", tone=1400, chirality=0.3, decay=0.9, freeze=0.75, defect_depth=1.0, wet=0.8, dry=0.4), freezer_macros(),
    defects=ring(0.5, 0.5, 0.3, 14))
add("Soft Hold", "FX Freezer", "Gentle partial freeze; a long but still-decaying wash behind the dry signal.",
    dict(mode="FX Defect Freezer", tone=700, chirality=0.4, decay=0.8, freeze=0.5, wet=0.6, dry=0.7), freezer_macros())
add("Shattered Ice", "FX Freezer", "Random scattered defects with HQ potential: frozen field with grainy partials.",
    dict(mode="FX Defect Freezer", tone=1800, chirality=0.6, decay=0.75, freeze=0.7, potential="HQ (fast log-cosh)", hq=0.5, nonlinear=0.5,
         wet=0.7, dry=0.5), freezer_macros(),
    defects=[[0.2, 0.3], [0.33, 0.72], [0.48, 0.41], [0.62, 0.66], [0.71, 0.24], [0.84, 0.55], [0.27, 0.52], [0.56, 0.18]])
add("Bulk Smear", "FX Freezer", "No freeze, low decay: the bulk lattice acts as a short dense diffuser.",
    dict(mode="FX Defect Freezer", tone=1200, chirality=0.2, decay=0.4, freeze=0.0, wet=0.55, dry=0.7), freezer_macros())

# ---------------- Instrument: Bowed ----------------
add("Chiral Cello", "Inst Bowed", "Saw-bowed voices on a mid tone lattice; pressure swells, CC74 moves the bow point.",
    dict(mode="Instrument", exciter="Bow (saw)", tone=700, chirality=0.6, decay=0.75, nonlinear=0.2, wet=0.9, dry=0.0), inst_macros(250, 1800))
add("Glass Harmonica", "Inst Bowed", "High tone, HQ potential, long decay: bowed glass.",
    dict(mode="Instrument", exciter="Bow (saw)", tone=2200, chirality=0.8, decay=0.85, potential="HQ (fast log-cosh)", hq=0.4,
         nonlinear=0.1, wet=0.9, dry=0.0), inst_macros(900, 3000))
add("Twisted Choir", "Inst Bowed", "Morphogen activity slowly rotates the edge flow under held chords.",
    dict(mode="Instrument", exciter="Bow (saw)", tone=1000, chirality=0.7, decay=0.8, morph=0.65, tau=0.4, wet=0.9, dry=0.0),
    inst_macros(400, 2200), morph=[[0.5, 0.5, 0.6]])
add("Rusty Bow", "Inst Bowed", "Strong quartic nonlinearity; louder playing bends the timbre.",
    dict(mode="Instrument", exciter="Bow (saw)", tone=600, chirality=0.4, decay=0.7, nonlinear=0.9, wet=0.85, dry=0.0), inst_macros(250, 1600))

# ---------------- Instrument: Plucked ----------------
add("Honeycomb Harp", "Inst Plucked", "Raised-cosine plucks into a ringing chiral lattice.",
    dict(mode="Instrument", exciter="Pluck", tone=1500, chirality=0.6, decay=0.85, nonlinear=0.15, wet=0.9, dry=0.0), inst_macros())
add("Defect Kalimba", "Inst Plucked", "Heavy defects make localised modes: short, tuned-percussion plucks.",
    dict(mode="Instrument", exciter="Pluck", tone=1200, chirality=0.3, decay=0.7, defect_depth=1.0, wet=0.9, dry=0.0), inst_macros(),
    defects=line(0.15, 0.35, 0.85, 0.35, 7) + line(0.15, 0.65, 0.85, 0.65, 7))
add("Metal Lattice", "Inst Plucked", "HQ log-cosh potential plus high nonlinearity: metallic, inharmonic plucks.",
    dict(mode="Instrument", exciter="Pluck", tone=1900, chirality=0.9, decay=0.9, potential="HQ (fast log-cosh)", hq=0.7,
         nonlinear=0.6, wet=0.85, dry=0.0), inst_macros(800, 3000))
add("Low Mallets", "Inst Plucked", "Low tone, short decay; woody mallet strikes.",
    dict(mode="Instrument", exciter="Pluck", tone=380, chirality=0.5, decay=0.55, wet=0.9, dry=0.0), inst_macros(200, 1200))

# ---------------- Instrument: Living (self-oscillation, gated) ----------------
add("Spiral Drone", "Inst Living", "Life on: a seeded spiral wave in the medium drives gated self-oscillation. Bounded sustained irregularity.",
    dict(mode="Instrument", exciter="Pluck", tone=900, chirality=0.6, decay=0.7, life=0.35, morph=0.6, tau=0.12, wet=0.8, dry=0.0),
    inst_macros(300, 2000), spiral=[0.5, 0.5])
add("Ember Field", "Inst Living", "Low Life with slow medium: sparse self-sustaining embers that drift around.",
    dict(mode="Instrument", exciter="Pluck", tone=1300, chirality=0.5, decay=0.8, life=0.2, morph=0.5, tau=0.6, wet=0.8, dry=0.0),
    inst_macros(), morph=[[0.25, 0.3, 0.9], [0.75, 0.7, 0.9]])
add("Restless Swarm", "Inst Living", "High Life, fast medium, HQ potential: busy, bounded, never-repeating texture.",
    dict(mode="Instrument", exciter="Bow (saw)", tone=1600, chirality=0.8, decay=0.6, life=0.5, morph=0.8, tau=0.05,
         potential="HQ (fast log-cosh)", hq=0.4, wet=0.75, dry=0.0), inst_macros(), spiral=[0.4, 0.5])

# ---------------- Large lattice showcases (32x32; need 3-4+ threads) ----------------
add("Big Edge 32", "Showcase 32x32", "32x32 lattice edge disperser: longer edge, longer delays (uses 4 worker threads in Auto).",
    dict(lattice="32x32", tone=1500, chirality=0.7, decay=0.6, wet=0.7, dry=0.55), fx_edge_macros(600, 2600, 1.0))
add("Big Spiral 32", "Showcase 32x32", "32x32 lattice with a seeded spiral and HQ potential (fast log-cosh).",
    dict(mode="Instrument", lattice="32x32", exciter="Pluck", tone=1100, chirality=0.6, decay=0.75, life=0.3, morph=0.6, tau=0.15,
         potential="HQ (fast log-cosh)", hq=0.3, wet=0.8, dry=0.0), inst_macros(), spiral=[0.5, 0.5])
add("Tiny Edge 8", "Showcase 8x8", "8x8 lattice: lowest CPU, short edge, tight dispersion.",
    dict(lattice="8x8", tone=1500, chirality=0.7, decay=0.6, wet=0.7, dry=0.6), fx_edge_macros(600, 2600, 1.0))

def slug(s):
    return "".join(c if c.isalnum() else "_" for c in s).strip("_").lower()

OUT.mkdir(exist_ok=True)
for f in OUT.glob("*.morpho"):
    f.unlink()
for i, p in enumerate(P):
    fn = OUT / f"{i + 1:02d}_{slug(p['name'])}.morpho"
    fn.write_text(json.dumps(p, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
print(f"wrote {len(P)} presets to {OUT}")
