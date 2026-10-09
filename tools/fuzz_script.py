#!/usr/bin/env python3
"""Random key script for the oracle: python3 fuzz_script.py SEED FRAMES"""
import random, sys
seed, frames = int(sys.argv[1]), int(sys.argv[2])
rnd = random.Random(seed)
# driving keys of car 1 and 2, cameras, split screen, car switching,
# gravity, fog, chase camera keys, jumps
held = [0x48, 0x50, 0x4b, 0x4d, 0x11, 0x1f, 0x1e, 0x20, 0x17, 0x24, 0x02, 0x03, 0x4e, 0x4a, 0x35, 0x37, 0x39, 0x1d]
taps = [0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x43, 0x44, 0x0f, 0x10, 0x1a, 0x1b]
ev = []
for f in range(1, frames, 7):
    if rnd.random() < 0.5:
        sc = rnd.choice(held)
        ev.append((f, sc)); ev.append((min(frames - 1, f + rnd.randint(1, 120)), sc | 0x80))
    if rnd.random() < 0.08:
        sc = rnd.choice(taps)
        ev.append((f, sc)); ev.append((f + 1, sc | 0x80))
ev.sort(key=lambda x: x[0])
print(','.join('%d:%d' % e for e in ev))
