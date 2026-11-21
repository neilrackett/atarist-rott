#!/usr/bin/env python3
"""regions.py LOG NM FUNC [BYTES]: a function's profile in regions of BYTES
(default 64) of code, with each region's first instructions."""
import re, sys
log, nm, fn = sys.argv[1], sys.argv[2], sys.argv[3]
step = int(sys.argv[4]) if len(sys.argv) > 4 else 64
syms = sorted((int(l.split()[0], 16), l.split()[2]) for l in open(nm)
              if len(l.split()) == 3 and l.split()[1] in 'tT' and not l.split()[2].startswith('.L'))
names = dict((s, a) for a, s in syms)
text = open(log, errors='replace').read()
base = None
for l in text.splitlines():
    m = re.match(r'^0x([0-9a-f]+)\s+[0-9.]+\s+(\d+)\s+(\S+)', l)
    if m and m.group(3) in names:
        base = int(m.group(1), 16) - names[m.group(3)]
        break
a0 = names[fn] + base
a1 = syms[[s for a, s in syms].index(fn) + 1][0] + base
reg = {}
first = {}
for l in text.splitlines():
    m = re.match(r'^0x([0-9a-f]+)\s+([0-9.]+)%\s+(\d+)\s+\S+\s+[0-9a-f ]+\s{2,}(.*?)\s{2,}', l)
    if m and a0 <= int(m.group(1), 16) < a1:
        a = int(m.group(1), 16) - a0
        r = a // step
        reg[r] = reg.get(r, 0) + int(m.group(3))
        first.setdefault(r, []).append((a, m.group(4)[:40]))
tot = sum(reg.values())
for r in sorted(reg):
    ins = '; '.join(d for a, d in sorted(first[r])[:4])
    print('+%04x %5.1f%%  %s' % (r * step, 100.0 * reg[r] / tot, ins))
