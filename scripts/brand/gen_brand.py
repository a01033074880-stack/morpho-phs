#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# MORPHO-PHS brand generator (Phase 6a).  Single source of truth for the glyph and the wordmark:
#   * writes the SVG logos / wordmarks / icons into plugin/Assets/brand/
#   * writes plugin/Source/ui/BrandGeometry.h (the same geometry as juce::Path builders)
#   * writes 512 / 256 px PNG plugin icons (supersampled, PIL) for juce_add_plugin(ICON_BIG / ICON_SMALL)
# All shapes are drawn here from scratch: honeycomb site (Y of three bonds = an isometric cube), the pointy-top
# hexagon edge with a chiral circulation arrow, and a monoline 60-degree alphabet for M O R P H S F X and the bond hyphen.
import math, os, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT_SVG = os.path.join(ROOT, 'plugin', 'Assets', 'brand')
OUT_H = os.path.join(ROOT, 'plugin', 'Source', 'ui', 'BrandGeometry.h')

# palette tokens (keep in sync with plugin/Source/ui/Theme.h and docs/phase6/art_direction.md)
BULK0 = '#0B0D12'; INK = '#E9E6DF'; ACC = {'inst': '#59D8F0', 'fx': '#C8F05A'}

T60 = math.tan(math.radians(60.0))
CH = 10.0                 # cap height (units)
SW = 1.5                  # stroke width
GAP = 1.7                 # space between glyph boxes
C = 2.5 / T60             # 60-degree chamfer: horizontal run for a 2.5 vertical rise (1.443)

# --- monoline alphabet: list of polylines (x right, y down, cap height 10) + width; dots = (x, y, r)
G = {
    'M': ([[(0, 10), (0, 0), (5 / T60, 5), (10 / T60, 0), (10 / T60, 10)]], 10 / T60, []),
    'O': ([[(0, 2.5), (C, 0), (5 - C, 0), (5, 2.5), (5, 7.5), (5 - C, 10), (C, 10), (0, 7.5), (0, 2.5)]], 5.0, []),
    'R': ([[(0, 10), (0, 0), (3.4, 0), (3.4 + C, 2.5), (3.4, 5), (0, 5)], [(2.1, 5), (2.1 + 5 / T60, 10)]], 2.1 + 5 / T60, []),
    'P': ([[(0, 10), (0, 0), (3.4, 0), (3.4 + C, 2.5), (3.4, 5), (0, 5)]], 3.4 + C, []),
    'H': ([[(0, 0), (0, 10)], [(5, 0), (5, 10)], [(0, 5), (5, 5)]], 5.0, []),
    'S': ([[(5, 0), (C, 0), (0, 2.5), (C, 5), (5 - C, 5), (5, 7.5), (5 - C, 10), (0, 10)]], 5.0, []),
    'F': ([[(0, 10), (0, 0), (4.8, 0)], [(0, 5), (3.6, 5)]], 4.8, []),
    'X': ([[(0, 0), (10 / T60, 10)], [(10 / T60, 0), (0, 10)]], 10 / T60, []),
    '-': ([[(1.0, 5), (3.6, 5)]], 4.6, [(0.55, 5, 0.95), (4.05, 5, 0.95)]),   # a bond between two sites
    ' ': ([], 2.2, []),
}

def layout(text):
    strokes, dots, x = [], [], 0.0
    for k, ch in enumerate(text):
        pl, w, dd = G[ch]
        for line in pl: strokes.append([(x + px, py) for px, py in line])
        for (dx, dy, r) in dd: dots.append((x + dx, dy, r))
        x += w + (GAP + SW if k + 1 < len(text) else 0)
    return strokes, dots, x

# --- glyph: pointy-top hexagon edge (radius 1) with a chiral gap + arrow, and the site's three bonds
def glyph(variant):
    A = [math.radians(90 + 60 * k) for k in range(6)]
    V = [(math.cos(a), -math.sin(a)) for a in A]        # svg coords (y down)
    ccw = variant == 'inst'                             # instrument circulates CCW, FX is its mirror (CW)
    order = list(range(6)) if ccw else [0, 5, 4, 3, 2, 1]
    # start 0.22 of the way along the first edge after vertex 'order[0]', end 0.78 into the last edge -> gap at vertex 0
    def lerp(p, q, t): return (p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t)
    pts = [lerp(V[order[0]], V[order[1]], 0.22)] + [V[order[i]] for i in range(1, 6)] + [lerp(V[order[5]], V[order[0]], 0.70)]
    # arrow head at the end, pointing along the travel direction
    e = pts[-1]; d = (V[order[0]][0] - V[order[5]][0], V[order[0]][1] - V[order[5]][1]); n = math.hypot(*d); d = (d[0] / n, d[1] / n)
    hl, hw = 0.30, 0.20
    head = [(e[0] - d[0] * hl + d[1] * hw, e[1] - d[1] * hl - d[0] * hw), (e[0] + d[0] * 0.06, e[1] + d[1] * 0.06),
            (e[0] - d[0] * hl - d[1] * hw, e[1] - d[1] * hl + d[0] * hw)]
    arms = [90, 210, 330] if variant == 'inst' else [270, 30, 150]
    bonds = [[(0.0, 0.0), (0.56 * math.cos(math.radians(a)), -0.56 * math.sin(math.radians(a)))] for a in arms]
    return {'edge': pts, 'head': head, 'bonds': bonds, 'node': 0.17}

def f(v): return ('%.3f' % v).rstrip('0').rstrip('.').replace('-0', '-0') if abs(v) >= 5e-4 else '0'
def cf(v):
    t = f(v)
    return t if '.' in t else t + '.0'
def poly_d(pts, close=False): return 'M' + ' L'.join(f(x) + ' ' + f(y) for x, y in pts) + (' Z' if close else '')

def glyph_svg_group(variant, cx, cy, s, edge_w=0.13, bond_w=0.12):
    g = glyph(variant); acc = ACC[variant]
    tr = 'translate(%s %s) scale(%s)' % (f(cx), f(cy), f(s))
    out = ['<g transform="%s" fill="none" stroke-linejoin="miter" stroke-linecap="butt">' % tr]
    out.append('<path d="%s" stroke="%s" stroke-width="%s"/>' % (poly_d(g['edge']), acc, f(edge_w)))
    out.append('<path d="%s" stroke="%s" stroke-width="%s"/>' % (poly_d(g['head']), acc, f(edge_w)))
    for b in g['bonds']: out.append('<path d="%s" stroke="%s" stroke-width="%s"/>' % (poly_d(b), INK, f(bond_w)))
    out.append('<circle cx="0" cy="0" r="%s" fill="%s" stroke="none"/>' % (f(g['node']), INK))
    out.append('</g>')
    return '\n'.join(out)

def wordmark_svg_group(text, x0, y0, s, colour, fx_colour=None, fx_from=None):
    strokes, dots, w = layout(text)
    out = ['<g transform="translate(%s %s) scale(%s)" fill="none" stroke-width="%s" stroke-linejoin="miter" stroke-linecap="butt">' % (f(x0), f(y0), f(s), f(SW))]
    for st in strokes:
        col = fx_colour if (fx_from is not None and st[0][0] >= fx_from - 1e-6) else colour
        out.append('<path d="%s" stroke="%s"/>' % (poly_d(st), col))
    for (dx, dy, r) in dots: out.append('<circle cx="%s" cy="%s" r="%s" fill="%s" stroke="none"/>' % (f(dx), f(dy), f(r), colour))
    out.append('</g>')
    return '\n'.join(out), w

HDR = '<?xml version="1.0" encoding="UTF-8"?>\n<!-- SPDX-License-Identifier: AGPL-3.0-only -->\n<!-- MORPHO-PHS brand asset, generated by scripts/brand/gen_brand.py (drawn in code; no third-party artwork) -->\n'

def write(name, body):
    with open(os.path.join(OUT_SVG, name), 'w') as fh: fh.write(body)

def main():
    os.makedirs(OUT_SVG, exist_ok=True)
    for v in ('inst', 'fx'):
        # glyph alone (transparent)
        write('glyph_%s.svg' % v, HDR + '<svg xmlns="http://www.w3.org/2000/svg" viewBox="-1.25 -1.25 2.5 2.5" width="256" height="256">\n%s\n</svg>\n' % glyph_svg_group(v, 0, 0, 1))
        # icon: rounded square, bulk background
        write('icon_%s.svg' % v, HDR + '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="512" height="512">\n<rect x="0" y="0" width="512" height="512" rx="112" fill="%s"/>\n%s\n</svg>\n' % (BULK0, glyph_svg_group(v, 256, 262, 168, 0.12, 0.11)))
        # wordmark lockup
        text = 'MORPHO-PHS' + (' FX' if v == 'fx' else '')
        strokes, dots, w = layout(text)
        fx_from = layout('MORPHO-PHS ')[2] if v == 'fx' else None
        wm, w = wordmark_svg_group(text, 0, 0, 1, INK, ACC[v], fx_from)
        W = 30 + w + 4; H = 26
        body = HDR + '<svg xmlns="http://www.w3.org/2000/svg" viewBox="-2 -8 %s %s" width="%d" height="%d">\n<rect x="-2" y="-8" width="%s" height="%s" fill="%s"/>\n%s\n<g transform="translate(26 0)">%s</g>\n</svg>\n' % (
            f(W), f(H), int(W * 12), int(H * 12), f(W), f(H), BULK0, glyph_svg_group(v, 9, 5, 9.2, 0.14, 0.13), wm)
        write('wordmark_%s.svg' % v, body)
    write_header()
    write_pngs()
    print('brand assets written to', OUT_SVG, 'and', OUT_H)

def write_header():
    L = []
    L.append('// SPDX-License-Identifier: AGPL-3.0-only')
    L.append('// GENERATED by scripts/brand/gen_brand.py -- do not edit by hand.  Phase 6a brand geometry (drawn in code).')
    L.append('#pragma once')
    L.append('#include <juce_graphics/juce_graphics.h>')
    L.append('')
    L.append('namespace morpho_plugin::brand {')
    L.append('')
    L.append('inline constexpr float kCapHeight = %sf, kStroke = %sf;' % (cf(CH), cf(SW)))
    L.append('')
    for v in ('inst', 'fx'):
        g = glyph(v)
        L.append('// glyph (%s): unit pointy-top hexagon edge with chiral gap + arrow, three bonds, node radius %s' % (v, cf(g['node'])))
        L.append('inline void glyph_%s(juce::Path& edge, juce::Path& bonds, float& node_r) {' % v)
        for key, path in (('edge', g['edge']), ('edge', g['head'])):
            L.append('    edge.startNewSubPath(%sf, %sf);' % (cf(path[0][0]), cf(path[0][1])))
            for x, y in path[1:]: L.append('    edge.lineTo(%sf, %sf);' % (cf(x), cf(y)))
        for b in g['bonds']:
            L.append('    bonds.startNewSubPath(%sf, %sf); bonds.lineTo(%sf, %sf);' % (cf(b[0][0]), cf(b[0][1]), cf(b[1][0]), cf(b[1][1])))
        L.append('    node_r = %sf;' % cf(g['node']))
        L.append('}')
        L.append('')
    for name, text in (('wordmark', 'MORPHO-PHS'), ('wordmark_fx_suffix', 'FX')):
        strokes, dots, w = layout(text)
        L.append('// "%s": monoline strokes (stroke width kStroke, mitred joins, butt caps), dots filled; returns the width' % text)
        L.append('inline float %s(juce::Path& strokes, juce::Path& dots) {' % name)
        for st in strokes:
            L.append('    strokes.startNewSubPath(%sf, %sf);' % (cf(st[0][0]), cf(st[0][1])) + ''.join(' strokes.lineTo(%sf, %sf);' % (cf(x), cf(y)) for x, y in st[1:]))
        for (dx, dy, r) in dots:
            L.append('    dots.addEllipse(%sf, %sf, %sf, %sf);' % (cf(dx - r), cf(dy - r), cf(2 * r), cf(2 * r)))
        L.append('    return %sf;' % cf(w))
        L.append('}')
        L.append('')
    L.append('inline constexpr float kWordGap = %sf;   // gap between "MORPHO-PHS" and "FX"' % cf(GAP + SW + G[' '][1]))
    L.append('')
    L.append('}  // namespace morpho_plugin::brand')
    with open(OUT_H, 'w') as fh: fh.write('\n'.join(L) + '\n')

def write_pngs():
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print('PIL not available: PNG icons skipped'); return
    def hexrgb(h): return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))
    for v in ('inst', 'fx'):
        S = 2048; k = S / 512.0
        im = Image.new('RGBA', (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
        d.rounded_rectangle([0, 0, S - 1, S - 1], radius=int(112 * k), fill=hexrgb(BULK0) + (255,))
        g = glyph(v); cx, cy, sc = 256 * k, 262 * k, 168 * k
        P = lambda p: (cx + p[0] * sc, cy + p[1] * sc)
        def pl(pts, w, col):
            q = [P(p) for p in pts]
            d.line(q, fill=col, width=int(w * sc), joint='curve')
        pl(g['edge'], 0.12, hexrgb(ACC[v]) + (255,)); pl(g['head'], 0.12, hexrgb(ACC[v]) + (255,))
        for b in g['bonds']: pl(b, 0.11, hexrgb(INK) + (255,))
        r = g['node'] * sc; d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=hexrgb(INK) + (255,))
        for size in (512, 256):
            im.resize((size, size), Image.LANCZOS).save(os.path.join(OUT_SVG, 'icon_%s_%d.png' % (v, size)))

if __name__ == '__main__':
    main()
