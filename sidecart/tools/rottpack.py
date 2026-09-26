#!/usr/bin/env python3
# MD/ROTT host tool: what a ROTT WAD holds, set against the Multi-device's
# level-pack flash window.
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   rottpack.py census <WAD>          sections, sizes, and the flash budget
#   rottpack.py lumps <WAD> <n> [...] name, section and size of lump numbers
#                                      (e.g. from "R_Lump: lump N" in a
#                                      debug firmware's serial log)
#
# The firmware builds each level's pack itself from the WAD on its SD card
# (rp/src/md_pack.c); this tool only helps reason about the sizes.

import struct
import sys

# Keep in step with rp/src/memmap_rp.ld and rp/src/md_pack.c.
PACK_WINDOW = 896 * 1024
RING_MIN = 192 * 1024

# Marker lumps that open a section, in WAD order (shareware and registered).
MARKERS = {
    "WALLSTRT": "walls",
    "ANIMSTRT": "animated walls",
    "EXITSTRT": "exits, switches",
    "GUNSTART": "weapon frames",
    "ELEVSTRT": "elevators",
    "DOORSTRT": "doors",
    "SIDESTRT": "door sides",
    "MASKSTRT": "masked walls",
    "UPDNSTRT": "floors, ceilings",
    "SKYSTART": "skies",
    "SKYSTOP": "fonts, pictures",
    "ORDRSTRT": "order screens",
    "ORDRSTOP": "colour maps, misc",
    "SHAPSTRT": "sprites",
    "DIGISTRT": "digital sounds",
    "PCSTART": "PC speaker sounds",
    "ADSTART": "AdLib sounds",
}


def read_wad(path):
    data = open(path, "rb").read()
    kind, count, dirofs = struct.unpack("<4sII", data[:12])
    if kind not in (b"IWAD", b"PWAD"):
        raise SystemExit(f"{path}: not a WAD")
    lumps = []
    for i in range(count):
        pos, size, name = struct.unpack("<II8s", data[dirofs + 16 * i:dirofs + 16 * i + 16])
        lumps.append((name.rstrip(b"\0").decode("latin1").upper(), pos, size))
    return lumps


def sections(lumps):
    """Yield (section name, first lump, lumps in it) in WAD order."""
    current, first = "misc", 0
    out = []
    for i, (name, _, _) in enumerate(lumps):
        if name in MARKERS:
            out.append((current, first, i))
            current, first = MARKERS[name], i
    out.append((current, first, len(lumps)))
    return [(n, a, b) for (n, a, b) in out if b > a]


def census(path):
    lumps = read_wad(path)
    total = sum(s for _, _, s in lumps)
    print(f"{path}: {len(lumps)} lumps, {total / 1024:.0f} KB of data\n")
    print(f"{'section':20} {'first':>6} {'lumps':>6} {'KB':>7} {'largest':>18}")
    graphics = 0
    for name, a, b in sections(lumps):
        body = lumps[a:b]
        kb = sum(s for _, _, s in body) / 1024
        big = max(body, key=lambda l: l[2])
        if "sound" not in name:
            graphics += kb
        print(f"{name:20} {a:6} {b - a:6} {kb:7.0f} {big[0]:>10} {big[2] / 1024:5.1f} KB")
    print(f"\nEverything but sounds: {graphics:.0f} KB. Pack window "
          f"{PACK_WINDOW // 1024} KB: at most {(PACK_WINDOW - RING_MIN) // 1024} KB "
          f"preloaded per level when a level asks for more,\nthe rest "
          f"({RING_MIN // 1024} KB and up) is the ring for lumps loaded from "
          f"the SD card when first drawn.")


def lump_info(path, numbers):
    lumps = read_wad(path)
    secs = sections(lumps)
    for n in numbers:
        if not 0 <= n < len(lumps):
            print(f"{n}: out of range")
            continue
        sec = next(s for s, a, b in secs if a <= n < b)
        name, _, size = lumps[n]
        print(f"{n:5} {name:9} {size:7} bytes  ({sec})")


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "census":
        census(sys.argv[2])
    elif len(sys.argv) >= 4 and sys.argv[1] == "lumps":
        lump_info(sys.argv[2], [int(x) for x in sys.argv[3:]])
    else:
        raise SystemExit("usage: rottpack.py census <WAD> | lumps <WAD> <n> [...]")


if __name__ == "__main__":
    main()
