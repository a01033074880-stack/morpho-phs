# SPDX-License-Identifier: AGPL-3.0-only
# MORPHO-PHS Phase 4: migrate the factory presets from format version 1 to version 2.
#  - D1: g/mu interpolation "1 sample" (per-sample ramp)
#  - D4: new params edge_width (0..2, default 1) and edge_crosstalk (0..1, default 0); identity unless listed below
# Idempotent: re-running on v2 files changes nothing.
import json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
OVERRIDES = {   # (edge_width, edge_crosstalk) for presets that use the new stereo matrix
    "07_wide_pickups.morpho": (1.5, 0.0),
    "13_bulk_smear.morpho": (1.0, 0.35),
}
ORDER = ["mode", "lattice", "threads", "potential", "interp"]

def main():
    changed = 0
    for f in sorted((ROOT / "presets").glob("*.morpho")):
        d = json.loads(f.read_text(encoding="utf-8"))
        assert d.get("format") == "morpho-phs-preset", f
        p = d.setdefault("params", {})
        w, c = OVERRIDES.get(f.name, (1.0, 0.0))
        p["interp"] = "1 sample"
        p.setdefault("edge_width", w); p.setdefault("edge_crosstalk", c)
        if d.get("version", 1) < 2:
            p["edge_width"], p["edge_crosstalk"] = w, c
        d["version"] = 2
        new = json.dumps(d, indent=2, ensure_ascii=False) + "\n"
        if new != f.read_text(encoding="utf-8"):
            f.write_text(new, encoding="utf-8"); changed += 1
        print(f"{f.name}: v{d['version']} interp={p['interp']} edge_width={p['edge_width']} edge_crosstalk={p['edge_crosstalk']}")
    print(f"{changed} file(s) changed")

if __name__ == "__main__":
    sys.exit(main())
