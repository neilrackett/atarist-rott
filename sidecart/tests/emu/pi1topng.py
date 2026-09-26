#!/usr/bin/env python3
# Convert Degas .PI1 screenshots (ATARI_MD_AUTOTEST's SHOTnnn.PI1) to PNG,
# optionally side by side: pi1topng.py out.png a.PI1 [b.PI1 ...]
# Several inputs are placed left to right, 8 pixels apart.
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later

import struct
import sys
import zlib


def st_level(n):
    """STE 4-bit channel (low bit on top) to 0..255."""
    return (((n & 7) << 1) | (n >> 3)) * 17


def decode(path):
    data = open(path, "rb").read()
    if len(data) < 34 + 32000:
        raise SystemExit(f"{path}: too short for a .PI1")
    pal = struct.unpack(">16H", data[2:34])
    rgb = [(st_level(c >> 8 & 15), st_level(c >> 4 & 15), st_level(c & 15))
           for c in pal]
    screen = data[34:34 + 32000]
    rows = []
    for y in range(200):
        row = bytearray()
        for g in range(20):
            words = struct.unpack(">4H", screen[y * 160 + g * 8:y * 160 + g * 8 + 8])
            for bit in range(15, -1, -1):
                pen = sum(((words[p] >> bit) & 1) << p for p in range(4))
                row += bytes(rgb[pen])
        rows.append(row)
    return rows


def write_png(path, width, rows):
    raw = b"".join(b"\0" + bytes(r) for r in rows)

    def chunk(kind, body):
        c = kind + body
        return struct.pack(">I", len(body)) + c + struct.pack(">I", zlib.crc32(c))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, len(rows), 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__ or "usage: pi1topng.py out.png a.PI1 [b.PI1 ...]")
    images = [decode(p) for p in sys.argv[2:]]
    gap = bytes(8 * 3)
    rows = []
    for y in range(200):
        rows.append(gap.join(img[y] for img in images))
    width = 320 * len(images) + 8 * (len(images) - 1)
    write_png(sys.argv[1], width, rows)


if __name__ == "__main__":
    main()
