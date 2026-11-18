#!/usr/bin/env python3
"""Show where a program spends its time.

   cc9 -k -o prog.x prog.c
   spechtrun -p prog.x            (writes specht.prof)
   profile.py prog.sym [specht.prof] [count]
"""
import sys, bisect

symfile = sys.argv[1]
proffile = sys.argv[2] if len(sys.argv) > 2 else 'specht.prof'
count = int(sys.argv[3]) if len(sys.argv) > 3 else 25

syms = []
for line in open(symfile).read().split('\n')[2:]:
    if len(line) > 7:
        try:
            syms.append((int(line[2:6], 16), line[7:].strip()))
        except ValueError:
            pass
# ignore compiler generated local labels
syms = sorted(s for s in syms
              if not (s[1].startswith('L') and (s[1][1:].isdigit() or s[1].startswith('L$'))))
keys = [a for a, _ in syms]
total = {}
for line in open(proffile):
    addr, n = line.split()
    addr, n = int(addr, 16), int(n)
    if addr >= 0x8000:
        name = '(ROM)'
    else:
        i = bisect.bisect_right(keys, addr) - 1
        name = syms[i][1] if i >= 0 else '?'
    total[name] = total.get(name, 0) + n
t = sum(total.values())
for name, n in sorted(total.items(), key=lambda x: -x[1])[:count]:
    print("%12d %5.1f%%  %s" % (n, 100.0 * n / t, name))
print("%12d total instructions" % t)
