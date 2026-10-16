#!/usr/bin/env python3
"""Generate original Loka card shadow tables, using no third-party data.

Inputs: centre-sampled radius-6 rounded rectangles; DROP displacement (3,3)
and density 128; WIDE displacement (0,4), three radius-4 separable box passes,
round(255 * convolution / 729**2), and body subtraction. All arithmetic is
integer. The Bayer matrix uses the standard recursive construction.
Regenerate: python3 tools/ci/gen_card_shadow_tables.py
Normal builds compile tracked output and never invoke this program.
"""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RADIUS = 6
WIDE_DY = 4


def mask(w, h, x, y):
    if not (0 <= x < w and 0 <= y < h):
        return False
    if RADIUS <= x < w - RADIUS or RADIUS <= y < h - RADIUS:
        return True
    cx = RADIUS if x < RADIUS else w - RADIUS
    cy = RADIUS if y < RADIUS else h - RADIUS
    return (2*x+1-2*cx)**2 + (2*y+1-2*cy)**2 <= (2*RADIUS)**2


def kernel(blur):
    result = [1]
    for _ in range(3 if blur else 0):
        expanded = [0] * (len(result) + 8)
        for i, value in enumerate(result):
            for j in range(9):
                expanded[i+j] += value
        result = expanded
    return result


def geometry(k, dx, dy):
    allowances = [max(0, k-dx), max(0, k-dy), max(0, k+dx), max(0, k+dy)]
    cuts = [max(RADIUS, RADIUS+k+dx), max(RADIUS, RADIUS+k+dy),
            max(RADIUS, RADIUS+k-dx), max(RADIUS, RADIUS+k-dy)]
    return allowances, cuts


def raster(w, h, k, dx, dy):
    weights = kernel(k)
    al, at, ar, ab = geometry(k, dx, dy)[0]
    xs = range(-al, w+ar)
    horizontal = {(x,y): sum(v * mask(w,h,x-dx-(i-k),y) for i,v in enumerate(weights))
                  for y in range(h) for x in xs}
    total = sum(weights)**2
    return {(x,y): 0 if mask(w,h,x,y) else
            ((255*sum(v*horizontal.get((x,y-dy-(i-k)),0) for i,v in enumerate(weights))
              + total//2)//total if k else 128*mask(w,h,x-dx,y-dy))
            for y in range(-at,h+ab) for x in xs}


def select(value, size, near, far):
    first, last = value < near, value >= size-far
    if first and last:
        first = value < size//2
        last = not first
    return 0 if first else 1 if last else 2


def prove_clearance(w, h, mw, mh, k, dx, dy, allowances):
    al, at, ar, ab = allowances
    bands = [(-al,-at,w-3,3), (-al,3,3,h+ab),
             (w-3,-at,w+ar,h-3), (3,h-3,w+ar,h+ab)]
    bodies = [(0,0,w,mh), (0,0,mw,h), (w-mw,0,mw,h), (0,h-mh,w,mh)]
    for band, (ox,oy,vw,vh) in zip(bands,bodies):
        left,top,right,bottom = band
        for y in range(h):
            for x in range(w):
                if mask(w,h,x,y) == mask(vw,vh,x-ox,y-oy):
                    continue
                # Inclusive changed samples, finite support, plus two-unit clearance.
                if not (x+dx+k+2 < left or x+dx-k-2 >= right or
                        y+dy+k+2 < top or y+dy-k-2 >= bottom):
                    raise RuntimeError(f'clearance failure: {w,h=} {band=} {x,y=}')


def generate_extent(k, dx, dy, mw, mh):
    a, c = geometry(k, dx, dy)
    al, at, ar, ab = a
    cl, ct, cr, cb = c
    ref_size = 100
    ref = raster(ref_size, ref_size, k, dx, dy)
    tiles = []
    for bottom in range(2):
        for right in range(2):
            xs = range(ref_size-cr,ref_size+ar) if right else range(-al,cl)
            ys = range(ref_size-cb,ref_size+ab) if bottom else range(-at,ct)
            tiles.append([ref[x,y] for y in ys for x in xs])
    profiles = [[ref[x,50] for x in range(-al,0)],
                [ref[50,y] for y in range(-at,0)],
                [ref[x,50] for x in range(ref_size,ref_size+ar)],
                [ref[50,y] for y in range(ref_size,ref_size+ab)]]
    def compose(w,h,x,y):
        sx, sy = select(x,w,cl,cr), select(y,h,ct,cb)
        if sx != 2 and sy != 2:
            tx = x+al if sx == 0 else x-w+cr
            ty = y+at if sy == 0 else y-h+cb
            width = al+cl if sx == 0 else ar+cr
            return tiles[sy*2+sx][ty*width+tx]
        if x < 0: return profiles[0][x+al]
        if y < 0: return profiles[1][y+at]
        if x >= w: return profiles[2][x-w]
        if y >= h: return profiles[3][y-h]
        return 0
    checked = 0
    for w in range(mw,mw+18):
        for h in range(mh,mh+18):
            if k:
                prove_clearance(w,h,mw,mh,k,dx,dy,a)
            for (x,y), expected in raster(w,h,k,dx,dy).items():
                actual = compose(w,h,x,y)
                if actual != expected:
                    raise RuntimeError(f'exactness failure: {k,dx,dy=} {w,h=} {x,y=} {actual=} {expected=}')
                assert not mask(w,h,x,y) or actual == 0
                checked += 1
    sizes = [(al+cl,at+ct),(ar+cr,at+ct),(al+cl,ab+cb),(ar+cr,ab+cb)]
    capacity = max(w*h for w,h in sizes)
    assert all(len(tile) == w*h <= capacity for tile,(w,h) in zip(tiles,sizes))
    print(f'{k,dx,dy=}: tiles {sizes}, density bytes {sum(map(len,tiles+profiles))}, checked {checked} pixels')
    return a,c,tiles+profiles,sizes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-root', type=Path, default=ROOT)
    args = parser.parse_args()
    wide_reach = (len(kernel(True)) - 1) // 2
    wide_a = geometry(wide_reach, 0, WIDE_DY)[0]
    wide_width = 2 * (RADIUS + max(wide_a[0], wide_a[2]))
    wide_height = 2 * (RADIUS + max(wide_a[1], wide_a[3]))
    extents = [('Drop',0,3,3,14,14,12,12),
               ('Wide',wide_reach,0,WIDE_DY,wide_width,wide_height,wide_width,wide_height)]
    data, descriptors, constants = [], [], []
    all_sizes = []
    for name,k,dx,dy,mw,mh,vw,vh in extents:
        a,c,parts,sizes = generate_extent(k,dx,dy,mw,mh)
        all_sizes.extend(sizes)
        offsets = []
        for part in parts:
            offsets.append(len(data))
            data.extend(part)
        constants += [f'      k{name}MinimumWidth = {mw}, k{name}MinimumHeight = {mh},',
                      f'      k{name}TileCapacity = {max(w*h for w,h in sizes)},']
        descriptors.append('      {{%s}, {%s}, %s, %s, %s, %s, %s, %s, %s, {%s}}' %
                           (', '.join(map(str,a)), ', '.join(map(str,c)),dx,dy,k,mw,mh,vw,vh,', '.join(map(str,offsets))))
    bayer = [[0]]
    for _ in range(3):
        n = len(bayer)
        # This orientation matches the probe: first row 0,48,12,60,3,51,15,63.
        bayer = [[4*bayer[y%n][x%n] + ((0,3),(2,1))[y//n][x//n]
                  for x in range(n*2)] for y in range(n*2)]
    notice = '''// Generated by tools/ci/gen_card_shadow_tables.py. Do not edit.
// Original Loka formulas; no third-party data. Centre-sampled radius-6 masks.
// DROP: (3,3), density 128. WIDE: (0,4), (1+z+...+z^8)^3 per axis,
// round(255*sum/729^2), then subtract the body. Integer arithmetic only.
// Bayer: standard recursive construction, window anchored.
// Regenerate: python3 tools/ci/gen_card_shadow_tables.py
// clang-format off
'''
    header = notice + '''#ifndef LOKA_APP_STYLE_CARD_SHADOW_TABLES_HPP
#define LOKA_APP_STYLE_CARD_SHADOW_TABLES_HPP
namespace loka
{
  namespace app
  {
    namespace detail
    {
      /** Immutable extent facts; sides are left, top, right, bottom.
          Offsets address TL, TR, BL, BR tiles, then L, T, R, B profiles. */
      struct CardShadowTable
      {
        const int allowance[4];
        const int cut[4];
        const int dx, dy, reach;
        const int minimumWidth, minimumHeight;
        const int virtualWidth, virtualHeight;
        const unsigned offset[8];
      };
      enum
      {
''' + '\n'.join(constants) + f'''
        kCardShadowDensityBytes = {len(data)},
        kCardShadowMaximumTileWidth = {max(w for w,h in all_sizes)},
        kCardShadowMaximumTileHeight = {max(h for w,h in all_sizes)}
      }};
      extern const CardShadowTable CardShadowTables[3];
      extern const unsigned char CardShadowDensities[kCardShadowDensityBytes];
      extern const unsigned char CardShadowBayer[64];
    }} // namespace detail
  }} // namespace app
}} // namespace loka
#endif
'''
    def rows(values):
        return '\n'.join('        '+', '.join(map(str,values[i:i+16]))+',' for i in range(0,len(values),16))
    cpp = notice + '''#include "app/style/CardShadowTables.hpp"
namespace loka
{
  namespace app
  {
    namespace detail
    {
      const CardShadowTable CardShadowTables[3] = {
      {{0, 0, 0, 0}, {6, 6, 6, 6}, 0, 0, 0, 12, 12, 12, 12, {0, 0, 0, 0, 0, 0, 0, 0}},
''' + ',\n'.join(descriptors) + '''};
      const unsigned char CardShadowDensities[kCardShadowDensityBytes] = {
''' + rows(data) + '''};
      const unsigned char CardShadowBayer[64] = {
''' + rows(sum(bayer,[])) + '''};
    } // namespace detail
  } // namespace app
} // namespace loka
'''
    output = args.output_root/'common/app/style'
    output.mkdir(parents=True,exist_ok=True)
    (output/'CardShadowTables.hpp').write_text(header)
    (output/'CardShadowTables.cpp').write_text(cpp)


if __name__ == '__main__':
    main()
