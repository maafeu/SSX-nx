#!/usr/bin/env python3
"""sym.py -- names for libgame.so addresses (vaddrs, or run-time addresses with
--base): python tools/sym.py <readelf -s output> [--base 0x...] addr ... MIT."""
import bisect, sys
args = sys.argv[1:]
symfile = args.pop(0)
base = 0
if args and args[0] == '--base':
    base = int(args[1], 16); args = args[2:]
syms = []
for line in open(symfile):
    p = line.split()
    if len(p) >= 8 and p[3] == 'FUNC':
        try:
            syms.append((int(p[1], 16) & ~1, int(p[2]), p[7]))
        except ValueError:
            pass
syms.sort()
addrs = [s[0] for s in syms]
for a in args:
    v = int(a, 16) - base
    i = bisect.bisect_right(addrs, v) - 1
    s = syms[i] if i >= 0 else (0, 0, '?')
    print(f'0x{v:06x}  {s[2]}+0x{v - s[0]:x}' + ('' if v - s[0] < max(s[1], 4) else '  (past its end)'))
