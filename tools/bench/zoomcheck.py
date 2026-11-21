#!/usr/bin/env python3
"""zoomcheck.py DRIVE VIEWSIZE: ATARI_MD_AUTOTEST shots in 256 colours with
ViewZoom (an ATARI_VIEWSIZE build): each SHOTnnn.P8 (the screen read back)
against its CHUNKnnn.RAW (the chunky screen), pixel by pixel, with the zoom
placed as the ST's own (atari_c2p.c's "Viewport-only zoom", status bars at
both ends): the zoomed rows the view's pixels repeated, the rest the chunky
screen's own."""
import glob, os, sys
d, vs = sys.argv[1], int(sys.argv[2])
sizes = [(80,48),(128,72),(160,88),(192,104),(224,120),(256,136),(288,152)]
w, h = sizes[vs]
x0, y0 = (320 - w) // 2, 16 + (168 - h) // 2
z = 4 if w <= 80 else 2
cx, cy = x0 + w // 2, y0 + h // 2
# the view as whole 16-pixel groups, as the ST's zoom takes it
x1 = (x0 + w) & ~15
x0 = (x0 + 15) & ~15
w = x1 - x0
src_h = h
if src_h * z > 168:
    src_h = 168 // z
sy0 = y0 + (h - src_h) // 2
dx0, dy0 = cx - w * z // 2, cy - src_h * z // 2
dx1, dy1 = dx0 + w * z, dy0 + src_h * z
if dy0 < 16:
    dy0, dy1 = 16, 16 + src_h * z
if dy1 > 184:
    dy0, dy1 = 184 - src_h * z, 184
print('view %dx%d at (%d,%d), zoom %d: shown at (%d,%d)-(%d,%d)' % (w, h, x0, y0, z, dx0, dy0, dx1, dy1))
bad = 0
for p8 in sorted(glob.glob(os.path.join(d, 'SHOT*.P8'))):
    shot = open(p8, 'rb').read()[768:]
    ch = open(p8.replace('SHOT', 'CHUNK').replace('.P8', '.RAW'), 'rb').read()
    n = 0
    for y in range(200):
        for x in range(320):
            if dy0 <= y < dy1 and dx0 <= x < dx1:
                want = ch[(sy0 + (y - dy0) // z) * 320 + x0 + (x - dx0) // z]
            else:
                want = ch[y * 320 + x]
            n += shot[y * 320 + x] != want
    print('  %s: %d pixels differ' % (os.path.basename(p8), n))
    bad += n != 0
sys.exit(1 if bad else 0)
