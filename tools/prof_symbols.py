#!/usr/bin/env python3
"""prof_symbols.py -- prof.txt ([debug] profile, source/ssx_prof.c) to function names.

    python tools/prof_symbols.py prof.txt <libgame.so> [<libgnustl_shared.so>] [--elf ssx_nx.elf] [--top N]
                                [--minus older_prof.txt]

Each sampled address is put to the function around it: libgame.so's and libgnustl_shared.so's
symbols (arm-none-eabi-nm, dynamic and static), this program's from ssx_nx.elf (its live address is
found from ssx_prof_write's). Per thread name: the busiest functions, as a share of that thread's
samples. --minus subtracts an earlier prof.txt of the same run (the samples between the two).
Any of the profiler's files reads the same way: prof.txt (the session), prof-race.txt (the race
windows at full speed alone), prof-long.txt, prof-dip<n>.txt. Since build 202611182110 their
tables drop rarely hit addresses when half full (the header's "pruned"), so a share here is of the
samples kept; earlier builds' prof.txt dropped every address first seen after 8,192 (the header's
"lost"). The .so files are the player's own (never in this repository). MIT.
"""
import bisect
import os
import subprocess
import sys
from collections import defaultdict

NM = os.environ.get('NM', 'C:/devkitPro/devkitARM/bin/arm-none-eabi-nm.exe')


def symbols(path, dynamic):
    args = [NM, '-C', '--defined-only', '-n'] + (['-D'] if dynamic else []) + [path]
    try:
        out = subprocess.run(args, capture_output=True, text=True, errors='replace').stdout
    except OSError:
        return []
    syms = []
    for line in out.splitlines():
        parts = line.split(' ', 2)
        if len(parts) == 3 and parts[1] in 'TtWw':
            syms.append((int(parts[0], 16) & ~1, parts[2]))
    return syms


def table(path):
    syms = {a: n for a, n in symbols(path, False)}
    for a, n in symbols(path, True):
        syms.setdefault(a, n)
    keys = sorted(syms)
    return keys, [syms[k] for k in keys]


def name_at(tab, addr):
    keys, names = tab
    i = bisect.bisect_right(keys, addr) - 1
    if i < 0:
        return '?'
    return '%s+0x%x' % (names[i], addr - keys[i]) if False else names[i]


def read(path):
    ranges, classes, samples = {}, {}, defaultdict(int)
    port_ref = None
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            p = line.split()
            if not p or p[0].startswith('#'):
                continue
            if p[0] in ('game', 'stl'):
                ranges[p[0]] = (int(p[1], 16), int(p[2], 16))
            elif p[0] == 'port':
                port_ref = int(p[2], 16)
            elif p[0] == 'class':
                classes[int(p[1])] = ' '.join(p[3:])
            elif p[0] == 's':
                samples[(int(p[1]), int(p[2], 16), int(p[3], 16))] += int(p[4])
    return ranges, classes, samples, port_ref


def main():
    args = sys.argv[1:]
    top = 25
    elf = None
    minus = None
    if '--top' in args:
        i = args.index('--top'); top = int(args[i + 1]); del args[i:i + 2]
    if '--elf' in args:
        i = args.index('--elf'); elf = args[i + 1]; del args[i:i + 2]
    if '--minus' in args:
        i = args.index('--minus'); minus = args[i + 1]; del args[i:i + 2]
    if len(args) < 2:
        sys.exit(__doc__)
    ranges, classes, samples, port_ref = read(args[0])
    if minus:
        _, _, older, _ = read(minus)
        for k, v in older.items():
            samples[k] -= v
    game = table(args[1])
    stl = table(args[2]) if len(args) > 2 else ([], [])
    port = table(elf) if elf else ([], [])
    port_delta = None
    if elf and port_ref:
        for a, n in zip(*port):
            if n == 'ssx_prof_write':
                port_delta = port_ref - a
                break

    def where(pc):
        for key, tab in (('game', game), ('stl', stl)):
            if key in ranges:
                base, size = ranges[key]
                if base <= pc < base + size:
                    return '%s:%s' % ('G' if key == 'game' else 'S', name_at(tab, pc - base))
        if port_delta is not None:
            return 'P:' + name_at(port, pc - port_delta)
        return 'other'

    per_class = defaultdict(lambda: defaultdict(int))
    per_class_lr = defaultdict(lambda: defaultdict(lambda: defaultdict(int)))
    totals = defaultdict(int)
    for (cls, pc, lr), n in samples.items():
        if n <= 0:
            continue
        fn = where(pc)
        per_class[cls][fn] += n
        per_class_lr[cls][fn][where(lr)] += n
        totals[cls] += n
    for cls in sorted(per_class, key=lambda c: -totals[c]):
        print('== %s: %d samples' % (classes.get(cls, cls), totals[cls]))
        for fn, n in sorted(per_class[cls].items(), key=lambda kv: -kv[1])[:top]:
            callers = sorted(per_class_lr[cls][fn].items(), key=lambda kv: -kv[1])[:2]
            via = '; '.join('%s %d%%' % (c, 100 * m // n) for c, m in callers)
            print('  %5.1f%%  %s   (from %s)' % (100.0 * n / totals[cls], fn[:110], via[:150]))
        print()


if __name__ == '__main__':
    main()
