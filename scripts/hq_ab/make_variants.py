# SPDX-License-Identifier: AGPL-3.0-only
# HQ A/B pack: write the same preset with only the potential (and lattice) changed.
import json, os, sys
SRC = '/workspace/morpho-phs-workspace/presets'
PICK = [('glass_gong', '003_glass_gong.morpho', '16x16'),
        ('glass_harmonica', '017_glass_harmonica.morpho', '16x16'),
        ('spiral_drone', '026_spiral_drone.morpho', '16x16'),
        ('shattered_ice', '111_shattered_ice.morpho', '16x16'),
        ('cathedral_gyro_32', '005_cathedral_gyro_32.morpho', '32x32')]
POT = {'quartic': 'Quartic', 'hqfast': 'HQ (fast log-cosh)', 'hqexact': 'HQ exact'}
os.makedirs('variants', exist_ok=True)
for key, f, lat in PICK:
    p = json.load(open(os.path.join(SRC, f)))
    for m, pv in POT.items():
        if lat == '32x32' and m == 'hqexact': continue
        q = json.loads(json.dumps(p)); q['params']['potential'] = pv; q['params']['lattice'] = lat
        q['params']['out_gain'] = p['params']['out_gain'] - 12.0  # headroom: no limiter in any version
        q['name'] = f"{p['name']} [{m}]"
        json.dump(q, open(f'variants/{key}__{m}.morpho', 'w'), indent=1, ensure_ascii=False)
print('ok')
