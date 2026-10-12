#!/usr/bin/env python3
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-2.0-or-later
"""
rottpalette.py - the 16 colours ROTT_ST.TOS and the ROTT Accelerator show
ROTT's game palette with, and how each of its 256 colours is dithered from
them: rott/atari_palette.c. See README.md.

  rottpalette.py usage    -o usage.txt --play DIR... [--hud F...] [--menu F...]
  rottpalette.py generate --wad HUNTBGIN.WAD --usage usage.txt -o atari_palette.c
  rottpalette.py preview  --wad HUNTBGIN.WAD --table atari_palette.c -o DIR FRAME...

Needs numpy (and Pillow for preview).
"""
import argparse
import glob
import os
import re
import struct
import sys

import numpy as np

# ---- colour ----------------------------------------------------------

M1 = np.array([[0.4122214708, 0.5363325363, 0.0514459929],
               [0.2119034982, 0.6806995451, 0.1073969566],
               [0.0883024619, 0.2817188376, 0.6299787005]])
M2 = np.array([[0.2104542553, 0.7936177850, -0.0040720468],
               [1.9779984951, -2.4285922050, 0.4505937099],
               [0.0259040371, 0.7827717662, -0.8086757660]])


def linear(rgb):
    """sRGB 0-255 to linear light."""
    c = np.asarray(rgb, float) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def oklab(lin, chroma=1.0):
    """Linear light to OKLab, where distance is how different colours look;
    chroma weights hue and colourfulness (a, b) against lightness."""
    return (np.cbrt(np.maximum(lin @ M1.T, 0)) @ M2.T) * np.array([1.0, chroma, chroma])


# What each machine can show: levels a channel (the STE, TT and Falcon 16,
# the ST 8), and how much hue errors count against lightness. With only 8
# levels the ST's darkest greys are far apart, and at 1 it dithers them
# from dark red and teal; at 2 it keeps them grey. On the STE, 2 makes
# ROTT's red brick garish and 1.5 its skin orange.
MACHINES = {
    'ste': (16, 1.0),
    'st': (8, 2.0),
}


def shown(levels, q):
    return np.asarray(q) * 255 // (levels - 1)


def st_word(levels, q):
    """A colour's palette register word; the STE keeps each level's low bit
    in bit 3 of its nibble, as the ST's own 3 bits stay where they were."""
    w = 0
    for v in q:
        v = int(v)
        n = ((v >> 1) | ((v & 1) << 3)) if levels == 16 else v
        w = (w << 4) | n
    return w


# ---- the game's palette -------------------------------------------

def wad_palette(path):
    data = open(path, 'rb').read()
    _, count, table = struct.unpack('<4sii', data[:12])
    for i in range(count):
        pos, size, name = struct.unpack('<ii8s', data[table + 16 * i:table + 16 * i + 16])
        if name.rstrip(b'\0').upper() == b'PAL':
            return scale(np.frombuffer(data[pos:pos + 768], np.uint8))
    sys.exit('%s: no PAL lump' % path)


def scale(pal):
    """As atari_c2p.c and md_video.c take a palette: 6-bit ones (VGA's
    0-63) times 4."""
    pal = np.asarray(pal, np.int64).reshape(256, 3)
    return pal << 2 if pal.max() <= 63 else pal


def fnv(pal):
    """atari_c2p_set_palette's hash of the scaled palette."""
    h = 2166136261
    for v in np.asarray(pal).ravel():
        h = ((h ^ int(v)) * 16777619) & 0xffffffff
    return h


# ---- usage: how much the game draws each colour ----------------------

RAW_NAME = re.compile(r'_(\d+)x(\d+)\.raw$')


def read_raw_frame(path):
    """EmuMD's chunky=DIR frames, cNNNNN_WxH.raw: the 768-byte palette,
    then the view."""
    m = RAW_NAME.search(path)
    w, h = int(m.group(1)), int(m.group(2))
    data = open(path, 'rb').read()
    return np.frombuffer(data[768:768 + w * h], np.uint8).reshape(h, w)


def read_screen(path):
    """A 320x200 chunky screen (CHUNKnnn.RAW), with or without a palette
    before it."""
    data = open(path, 'rb').read()
    return np.frombuffer(data[-64000:], np.uint8).reshape(200, 320)


def cmd_usage(args):
    play = np.zeros(256)
    for d in args.play:
        frames = sorted(glob.glob(os.path.join(d, '*.raw')))
        if not frames:
            sys.exit('%s: no frames' % d)
        h = np.zeros(256)
        for f in frames:
            h += np.bincount(read_raw_frame(f).ravel(), minlength=256)
        play += h / len(frames)
    play /= len(args.play)
    hud = np.zeros(256)
    for f in args.hud:
        s = read_screen(f)
        hud += np.bincount(np.concatenate([s[:16], s[184:]]).ravel(), minlength=256)
    if args.hud:
        hud /= len(args.hud)
    menu = np.zeros(256)
    for f in args.menu:
        menu += np.bincount(read_screen(f).ravel(), minlength=256)
    with open(args.output, 'w') as out:
        out.write('# How much ROTT draws each colour of its game palette, in pixels a\n'
                  '# screen: the 3D view (an average over levels), the status bars (an\n'
                  '# average) and the title and menu screens (a total). Made by\n'
                  '# rottpalette.py usage; see README.md.\n'
                  '# colour   view   bars   menus\n')
        for i in range(256):
            out.write('%3d %9.2f %7.2f %7.0f\n' % (i, play[i], hud[i], menu[i]))


def read_usage(path):
    rows = [l.split() for l in open(path) if l.strip() and not l.startswith('#')]
    u = np.array([[float(v) for v in r[1:4]] for r in rows])
    if u.shape != (256, 3):
        sys.exit('%s: want 256 rows of view, bars, menus' % path)
    return u


# ---- choosing the 16 colours ---------------------------------------

STEPS = np.arange(17) / 16.0  # a 4x4 Bayer cell: 0-16 of 16 pixels


def importance(usage, game, menus, floor):
    """Square roots of the pixel counts: big areas still matter most, but a
    small bright thing (the HUD's heart, an explosion) is not drowned."""
    w = np.sqrt(game * (usage[:, 0] + usage[:, 1]) + menus * usage[:, 2])
    w = w / w.sum()
    return w + floor / 256


def pair_mixes(pens, chroma):
    """Every pair of pens at every ratio, mixed in linear light (as the eye
    blends dither), in OKLab, and how much each pair's dither shows."""
    P = linear(pens)
    PL = oklab(P, chroma)
    i, j = np.triu_indices(len(P))
    mix = (1 - STEPS)[None, :, None] * P[i][:, None] + STEPS[None, :, None] * P[j][:, None]
    contrast = (STEPS * (1 - STEPS))[None] * ((PL[i] - PL[j]) ** 2).sum(1)[:, None]
    return oklab(mix.reshape(-1, 3), chroma), contrast.ravel(), i, j


def mix_costs(target, pens, penalty, chroma):
    ML, contrast, i, j = pair_mixes(pens, chroma)
    return ((target[:, None] - ML[None]) ** 2).sum(-1) + penalty * contrast[None], i, j


def objective(target, w, pens, penalty, chroma):
    cost, _, _ = mix_costs(target, pens, penalty, chroma)
    return float((w * cost.min(1)).sum())


def split_kmeans(target, w, levels, chroma, k=16, rounds=40):
    """A start: weighted median cut in OKLab, then k-means, onto the grid."""
    boxes = [np.arange(256)]

    def mean(b):
        return (target[b] * w[b, None]).sum(0) / w[b].sum()

    def spread(b):
        return (w[b, None] * (target[b] - mean(b)) ** 2).sum() if len(b) > 1 else -1

    while len(boxes) < k:
        b = boxes.pop(int(np.argmax([spread(b) for b in boxes])))
        axis = int(np.argmax((w[b, None] * (target[b] - mean(b)) ** 2).sum(0)))
        o = b[np.argsort(target[b, axis], kind='stable')]
        cw = np.cumsum(w[o])
        cut = min(max(int(np.searchsorted(cw, cw[-1] / 2)) + 1, 1), len(o) - 1)
        boxes += [o[:cut], o[cut:]]
    C = np.array([mean(b) for b in boxes])
    for _ in range(rounds):
        a = np.argmin(((target[:, None] - C[None]) ** 2).sum(-1), 1)
        for c in range(k):
            if w[a == c].sum() > 0:
                C[c] = (target[a == c] * w[a == c, None]).sum(0) / w[a == c].sum()
    grid = np.array([[r, g, b] for r in range(levels) for g in range(levels) for b in range(levels)])
    grid_lab = oklab(linear(shown(levels, grid)), chroma)
    q = []
    for c in C:
        d = ((grid_lab - c) ** 2).sum(1)
        for p in q:
            d[np.all(grid == p, 1)] = np.inf
        q.append(grid[int(np.argmin(d))])
    return np.array(q)


MOVES = np.array([[r, g, b] for r in (-1, 0, 1) for g in (-1, 0, 1) for b in (-1, 0, 1)
                  if (r, g, b) != (0, 0, 0)])


def optimise(target, w, levels, penalty, chroma, q):
    """Move one colour at a time to whichever neighbour on the machine's
    grid best lowers the error of every colour's best dither, until none
    does."""
    q = np.array(q)
    best = objective(target, w, shown(levels, q), penalty, chroma)
    while True:
        moved = False
        for c in range(len(q)):
            pick = None
            for step in np.concatenate([MOVES, 2 * MOVES]):
                t = q.copy()
                t[c] = np.clip(t[c] + step, 0, levels - 1)
                if any(np.all(t[c] == t[o]) for o in range(len(q)) if o != c):
                    continue
                v = objective(target, w, shown(levels, t), penalty, chroma)
                if v < best:
                    best, pick = v, t
            if pick is not None:
                q, moved = pick, True
        if not moved:
            return q, best


def dithers(target, pens, penalty, chroma):
    """Each colour's dither: the pair of pens (darker first) and how many of
    a Bayer cell's 16 pixels get the second."""
    cost, i, j = mix_costs(target, pens, penalty, chroma)
    best = cost.argmin(1)
    pair, step = best // len(STEPS), best % len(STEPS)
    a, b, k = i[pair], j[pair], step
    # one pen alone: the darker pen of the pair at 16 is the lighter one
    solid = (k == 0) | (k == 16) | (a == b)
    one = np.where(k == 16, b, a)
    a = np.where(solid, one, a)
    b = np.where(solid, one, b)
    k = np.where(solid, 0, k)
    return a, b, k


def make_table(pal, w, levels, chroma, opt_penalty, mix_penalty):
    target = oklab(linear(pal), chroma)
    q = split_kmeans(target, w, levels, chroma)
    q, err = optimise(target, w, levels, opt_penalty, chroma, q)
    order = np.argsort(oklab(linear(shown(levels, q)))[:, 0], kind='stable')
    q = q[order]  # dark to light: the Bayer cell's low thresholds get the darker pen
    a, b, k = dithers(target, shown(levels, q), mix_penalty, chroma)
    print('%s: %s (error %.5f)' % ('STE' if levels == 16 else 'ST',
                                   ' '.join('%x%x%x' % tuple(p) for p in q), err),
          file=sys.stderr)
    return q, a, b, k


def cmd_generate(args):
    pal = wad_palette(args.wad)
    usage = read_usage(args.usage)
    w = importance(usage, args.game, args.menus, args.floor)
    tables = {m: make_table(pal, w, n, chroma, args.opt_penalty, args.mix_penalty)
              for m, (n, chroma) in MACHINES.items()}
    out = open(args.output, 'w')
    out.write('/*\n'
              ' * Copyright (C) 2026 Neil Rackett\n'
              ' * SPDX-License-Identifier: GPL-2.0-or-later\n'
              ' *\n'
              ' * atari_palette.c - made by tools/rottpalette/rottpalette.py from the\n'
              ' * WAD\'s palette and tools/rottpalette/usage.txt: do not edit.\n'
              ' */\n'
              '#include "atari_palette.h"\n\n'
              'const unsigned long atari_palette_hash = 0x%08xUL;\n' % fnv(pal))
    for m, (n, _) in MACHINES.items():
        q, a, b, k = tables[m]
        out.write('\nconst unsigned short atari_palette_%s_words[16] = {\n' % m)
        for r in range(0, 16, 8):
            out.write('    ' + ', '.join('0x%03x' % st_word(n, p) for p in q[r:r + 8]) + ',\n')
        out.write('};\n\nconst unsigned char atari_palette_%s_rgb[16][3] = {\n' % m)
        for r in range(0, 16, 4):
            out.write('    ' + ', '.join('{%d, %d, %d}' % tuple(int(v) for v in shown(n, p))
                                         for p in q[r:r + 4]) + ',\n')
        out.write('};\n\nconst unsigned char atari_palette_%s_mix[256][2] = {\n' % m)
        for r in range(0, 256, 8):
            out.write('    ' + ', '.join('{0x%02x, %2d}' % ((a[c] << 4) | b[c], k[c])
                                         for c in range(r, r + 8)) + ',\n')
        out.write('};\n')


# ---- preview ---------------------------------------------------------

BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]])


def read_table(path, m):
    src = open(path).read()

    def body(name):
        return re.search(r'atari_palette_%s_%s\[[^=]*= \{(.*?)\};' % (m, name), src, re.S).group(1)

    rgb = np.array([int(v) for v in re.findall(r'\d+', body('rgb'))]).reshape(16, 3)
    mix = np.array([int(v, 0) for v in re.findall(r'0x[0-9a-f]+|\d+', body('mix'))]).reshape(256, 2)
    return rgb, mix


def render(screen, rgb, mix):
    h, w = screen.shape
    y, x = np.mgrid[0:h, 0:w]
    t = BAYER4[y & 3, x & 3]
    m = mix[screen]
    pen = np.where(t < 16 - m[..., 1], m[..., 0] >> 4, m[..., 0] & 15)
    return rgb[pen].astype(np.uint8)


def cmd_preview(args):
    from PIL import Image
    pal = wad_palette(args.wad)
    tables = {m: read_table(args.table, m) for m in MACHINES}
    os.makedirs(args.output, exist_ok=True)
    for f in args.frames:
        s = read_raw_frame(f) if RAW_NAME.search(f) else read_screen(f)
        row = [pal[s].astype(np.uint8)] + [render(s, *tables[m]) for m in MACHINES]
        im = Image.fromarray(np.concatenate(row, 1))
        im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
        im.save(os.path.join(args.output, os.path.splitext(os.path.basename(f))[0] + '.png'))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    u = sub.add_parser('usage', help='count colours in captured frames')
    u.add_argument('-o', '--output', required=True)
    u.add_argument('--play', nargs='+', required=True, help='EmuMD chunky=DIR folders, a level each')
    u.add_argument('--hud', nargs='*', default=[], help='320x200 screens with the status bars')
    u.add_argument('--menu', nargs='*', default=[], help='320x200 title and menu screens')
    u.set_defaults(func=cmd_usage)
    g = sub.add_parser('generate', help='write atari_palette.c')
    g.add_argument('--wad', required=True)
    g.add_argument('--usage', required=True)
    g.add_argument('-o', '--output', required=True)
    g.add_argument('--game', type=float, default=5.0, help='weight of a game screen (view + bars)')
    g.add_argument('--menus', type=float, default=1.0, help='weight of the menu screens')
    g.add_argument('--floor', type=float, default=0.02, help='share for colours never seen')
    g.add_argument('--opt-penalty', type=float, default=0.2, help='dither contrast cost, choosing')
    g.add_argument('--mix-penalty', type=float, default=0.3, help='dither contrast cost, dithering')
    g.set_defaults(func=cmd_generate)
    p = sub.add_parser('preview', help='PNGs: the original, STE, ST')
    p.add_argument('--wad', required=True)
    p.add_argument('--table', required=True)
    p.add_argument('-o', '--output', required=True)
    p.add_argument('frames', nargs='+')
    p.set_defaults(func=cmd_preview)
    args = ap.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
