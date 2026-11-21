#!/usr/bin/env python3
"""hot.py LOG NM FUNC [N]: the costliest instructions of one function in a
bench.sh profile, with their disassembly."""
import bisect, re, sys
log, nm, fn = sys.argv[1], sys.argv[2], sys.argv[3]
n = int(sys.argv[4]) if len(sys.argv) > 4 else 30
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
i = [s for a, s in syms].index(fn)
a1 = syms[i + 1][0] + base
rows = []
for l in text.splitlines():
    m = re.match(r'^0x([0-9a-f]+)\s+([0-9.]+)%\s+(\d+)\s+(.*)$', l)
    if m and a0 <= int(m.group(1), 16) < a1:
        rows.append((int(m.group(3)), int(m.group(1), 16), m.group(4)))
tot = sum(r[0] for r in rows)
print('%s: %d cycles in %d instructions' % (fn, tot, len(rows)))
for c, a, d in sorted(rows, reverse=True)[:n]:
    print('  %5.1f%%  +%04x  %s' % (100.0 * c / tot, a - a0, d[:90]))
