#!/usr/bin/env python3
"""rawdiff.py DIR_A DIR_B: compare two logic-check runs' pictures: the
hashes in RENDER.TXT frame by frame, and the RENDnnn.RAW frames (320x200, a
byte a pixel) by how many pixels differ in each."""
import glob
import os
import sys

a, b = sys.argv[1], sys.argv[2]
la = open(os.path.join(a, 'RENDER.TXT')).read().split('\n')
lb = open(os.path.join(b, 'RENDER.TXT')).read().split('\n')
n = min(len(la), len(lb))
# tic, the view's hash, the chunky screen's, and what the machine showed
bad = [i for i in range(n) if la[i].split()[:3] != lb[i].split()[:3]]
view = [i for i in bad if la[i].split()[:2] != lb[i].split()[:2]]
print('RENDER.TXT: %d and %d lines, %d frames differ (%d in the view)%s' % (
    len(la), len(lb), len(bad), len(view),
    (', first at frame %d' % bad[0]) if bad else ''))
both = [i for i in range(n) if len(la[i].split()) > 3 and len(lb[i].split()) > 3]
if both:
    shown = [i for i in both if la[i].split()[3] != lb[i].split()[3]]
    print('  shown: %d of %d frames differ on the screen itself%s' % (
        len(shown), len(both), (', first at frame %d' % shown[0]) if shown else ''))
    if shown and not bad:
        bad = shown
for f in sorted(glob.glob(os.path.join(a, 'REND*.RAW'))):
    g = os.path.join(b, os.path.basename(f))
    if not os.path.exists(g):
        continue
    x, y = open(f, 'rb').read(), open(g, 'rb').read()
    d = sum(1 for i in range(min(len(x), len(y))) if x[i] != y[i])
    if d:
        rows = sorted(set(i // 320 for i in range(min(len(x), len(y))) if x[i] != y[i]))
        print('  %s: %d pixels differ, rows %d-%d' % (os.path.basename(f), d, rows[0], rows[-1]))
sys.exit(1 if bad else 0)
