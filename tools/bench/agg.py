#!/usr/bin/env python3
"""agg.py LOG NM FRAMES [-v]: a Hatari profile (bench.sh's log) as time a
frame and where it went, by function and by kind."""
import bisect
import re
import sys

log, nm, frames = sys.argv[1], sys.argv[2], int(sys.argv[3])
verbose = '-v' in sys.argv
syms = []
for l in open(nm):
    p = l.split()
    if len(p) == 3 and p[1] in 'tT' and not p[2].startswith('.L'):
        syms.append((int(p[0], 16), p[2]))
syms.sort()
names = dict((n, a) for a, n in syms)
text = open(log, errors='replace').read()
base = None
for l in text.splitlines():
    m2 = re.match(r'^0x([0-9a-f]+)\s+[0-9.]+\s+(\d+)\s+(\S+)', l)
    if m2 and m2.group(3) in names:
        base = int(m2.group(1), 16) - names[m2.group(3)]
        break
if base is None:
    sys.exit('no profile in ' + log)
addrs = [a + base for a, _ in syms]
end = addrs[-1] + 0x4000
tot, total, counts = {}, 0, {}
for l in text.splitlines():
    m2 = re.match(r'^0x([0-9a-f]+)\s+([0-9.]+)%\s+(\d+)\s', l)
    if m2:
        a, c = int(m2.group(1), 16), int(m2.group(3))
        if 0xe00000 <= a < 0xf00000:
            name = 'TOS'
        else:
            i = bisect.bisect_right(addrs, a) - 1
            name = syms[i][1] if i >= 0 and a < end else 'other'
        tot[name] = tot.get(name, 0) + c
        total += c
        continue
    m2 = re.match(r'^0x([0-9a-f]+)\s+[0-9.]+\s+(\d+)\s+(\S+)', l)
    if m2:
        counts[m2.group(3)] = int(m2.group(2))
m = re.search(r'used cycles:\s+(\d+) .*\n\s+= ([0-9.]+)s', text)
persec = int(m.group(1)) / float(m.group(2))  # the CPU's clock
ms = lambda c: 1000.0 * c / persec / frames
print('%s: %d frames, %.1f ms a frame, %.2f fps (%d tics of logic)' % (
    log.split('/')[-1], frames, ms(total), 1000.0 / ms(total),
    counts.get('_UpdateGameObjects', 0)))
libgcc = [k for k in tot if re.match(r'_(u?(mul|div|mod|udivmod)[sd]i[34]|ashl|ashr|lshr).*\.o$', k)]
kinds = [
    ('libgcc arithmetic', libgcc),
    ('c2p and screen', [k for k in tot if re.search(r'c2p|copy256|copy16|^_memcpy$|atari_eight|^common|^scopy|^both_even|^less', k)]),
]
for g, ns in kinds:
    v = sum(tot.get(n, 0) for n in ns)
    print('  %-20s %5.1f%% %7.1f ms' % (g, 100.0 * v / total, ms(v)))
n = 60 if verbose else 26
for k, v in sorted(tot.items(), key=lambda x: -x[1])[:n]:
    print('  %5.1f%% %7.1f ms  %s%s' % (100.0 * v / total, ms(v), k,
          ('  x%d' % counts[k]) if k in counts else ''))

# Who calls what: `-c NAME` lists NAME's callers by function; `-i` lists
# every function's inclusive time (itself and what it calls).
def fn_at(a):
    i = bisect.bisect_right(addrs, a) - 1
    return syms[i][1] if i >= 0 and a < end else hex(a)

callers = {}
incl = {}
for l in text.splitlines():
    m2 = re.match(r'^0x[0-9a-f]+: (.*), (\S+)$', l)
    if not m2:
        continue
    callee = m2.group(2)
    for part in m2.group(1).split(', '):
        m3 = re.match(r'0x([0-9a-f]+) = (\d+) (\w+)(?: (\d+)/(\d+)/(\d+)(?:/\d+/\d+)?)?', part)
        if not m3 or 's' not in m3.group(3):
            continue
        who = fn_at(int(m3.group(1), 16))
        d = callers.setdefault(callee, {})
        d[who] = d.get(who, 0) + int(m3.group(2))
        if m3.group(6):
            incl[callee] = incl.get(callee, 0) + int(m3.group(6))
for i, a in enumerate(sys.argv):
    if a == '-c':
        want = sys.argv[i + 1]
        for callee in callers:
            if callee == want or callee == '_' + want or callee == '___' + want:
                print('callers of %s:' % callee)
                for who, n in sorted(callers[callee].items(), key=lambda x: -x[1])[:25]:
                    print('  %8d (%.0f a frame)  %s' % (n, n / float(frames), who))
if '-i' in sys.argv:
    print('inclusive:')
    for k, v in sorted(incl.items(), key=lambda x: -x[1])[:45]:
        print('  %5.1f%% %7.1f ms  %s' % (100.0 * v / total, ms(v), k))
