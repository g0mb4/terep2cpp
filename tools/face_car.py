#!/usr/bin/env python3
"""Copy a track and put a funny face in place of car 5's driver.

    python3 tools/face_car.py SRC_TRACK DST_TRACK

Car 5's model marks the driver's head with one OP_PIXEL on the reference
vertex (where the on-board cameras sit), but the roof and the windows are
drawn after it, so it is never seen.  An OP_SPRITE on that point is
added at the end of the program instead, so the face shows over the car
from every side; the relative offsets of SORT2 and the jumps are moved to
match.  The face is drawn into a free corner of a copy of textures.pcx
saved as car5.pcx, the skin only this car uses.  Needs Pillow.  The result
is derived from the track's files and is not stored in the repository.
"""
import os, shutil, struct, sys
from PIL import Image

CAR = 5
FACE = 32                       # pixels, at u, v = 0..31 of the texture page (car 5 uses u >= 118)
HEAD = 12 * 256                 # sprite half side: 12 map units (a bit of a bobblehead)

src, dst = sys.argv[1:]
shutil.rmtree(dst, ignore_errors=True)
shutil.copytree(src, dst)
files = {n.lower(): n for n in os.listdir(dst)}

# --- the face, in palette indices of col.pcx; 0xff is transparent ---------
pal = Image.open(os.path.join(dst, files['col.pcx'])).getpalette()


def colour(rgb):
    return min(range(255), key=lambda i: sum((pal[3 * i + k] - rgb[k]) ** 2 for k in range(3)))


YELLOW, BLACK, WHITE = colour((255, 220, 0)), colour((0, 0, 0)), colour((255, 255, 255))
RED, PINK = colour((200, 0, 0)), colour((255, 130, 160))
face = [[0xff] * FACE for _ in range(FACE)]
c = (FACE - 1) / 2
for y in range(FACE):
    for x in range(FACE):
        d = ((x - c) ** 2 + (y - c) ** 2) ** 0.5
        if d <= 15.5:
            face[y][x] = BLACK if d > 14.2 else YELLOW
        # googly eyes, the left one bigger and looking somewhere else
        for ex, ey, r, px, py in ((10, 11, 5.5, 8, 9), (22, 12, 3.8, 23, 13)):
            de = ((x - ex) ** 2 + (y - ey) ** 2) ** 0.5
            if de <= r:
                face[y][x] = BLACK if de > r - 1 else WHITE
            if ((x - px) ** 2 + (y - py) ** 2) ** 0.5 <= 1.6:
                face[y][x] = BLACK
        # big grin with the tongue out
        if 18 <= y <= 25 and ((x - c) / 10) ** 2 + ((y - 18) / 7) ** 2 <= 1:
            face[y][x] = BLACK if y == 18 or ((x - c) / 10) ** 2 + ((y - 18) / 7) ** 2 > 0.7 else RED
        if 22 <= y <= 27 and ((x - 18) / 4) ** 2 + ((y - 23) / 4.5) ** 2 <= 1:
            face[y][x] = PINK

# --- car5.pcx: textures.pcx with the face in its corner ---------------------
skin = Image.open(os.path.join(dst, files['textures.pcx']))
for y in range(FACE):           # texture (u, v) is pixel (u, v) of the image (cropped to 256 wide)
    for x in range(FACE):
        skin.putpixel((x, y), face[y][x])
skin.save(os.path.join(dst, files.get('car%d.pcx' % CAR, 'car%d.pcx' % CAR)), format='PCX')

# --- the model: PIXEL on the head -> SPRITE ---------------------------------
path = os.path.join(dst, files['car%d.dat' % CAR])
f = bytearray(open(path, 'rb').read())
vo, so, mo = struct.unpack_from('<HHH', f, 0)
assert mo > vo and mo > so, 'the model is not the last section'
code = bytes(f[mo:])
rd8 = lambda pc: code[pc] if pc < len(code) else 0
rd16 = lambda pc: rd8(pc) | rd8(pc + 1) << 8

n = struct.unpack_from('<H', f, vo)[0]
ref = next(i for i in range(n) if struct.unpack_from('<H', f, vo + 2 + i * 0x1c + 26)[0] == 0xffff)


def length(pc):
    op, k = rd8(pc) & 0x1f, rd8(pc + 1)
    return {1: 5, 2: 7, 3: 7, 4: 4 + (k + 1) * 2, 5: 1, 6: 2 + (k + 1) * 4, 7: 4 + (k + 1) * 4,
            8: 2 + (k + 1) * 6, 9: 21, 10: 1 + 0xba, 16: 5, 17: 3, 18: 3, 19: 3, 20: 3}.get(op, 1)


# every reachable instruction, with the absolute targets of its offsets
insns, work = {}, [0]
while work:
    pc = work.pop()
    if pc in insns:
        continue
    op = rd8(pc) & 0x1f
    rels = []                   # (position of a rel16, its base, target)
    if op == 16:
        rels = [(pc + 1, pc + 1, pc + 1 + rd16(pc + 1)), (pc + 3, pc + 1, pc + 1 + rd16(pc + 3))]
    elif op in (17, 18, 19, 20):
        rels = [(pc + 1, pc + 3, pc + 3 + rd16(pc + 1))]
    insns[pc] = rels
    work += [t for _, _, t in rels]
    if not (op == 0 or 11 <= op <= 15 or op > 20 or op == 17):
        work.append(pc + length(pc))

# the END of the main program (SORT2 and CALL come back before it)
at = 0
while rd8(at) & 0x1f not in (0, 11, 12, 13, 14, 15) and rd8(at) & 0x1f <= 20:
    at = insns[at][0][2] if rd8(at) & 0x1f == 17 else at + length(at)
sprite = struct.pack('<BHh', 9, 2 * ref, HEAD) + b''.join(
    struct.pack('<I', (v * 256 + 128) << 16 | (u * 256 + 128))
    for u, v in ((0, 0), (FACE - 1, 0), (FACE - 1, FACE - 1), (0, FACE - 1)))
moved = lambda a: a + len(sprite) if a >= at else a

new = bytearray(code[:at] + sprite + code[at:])
for pc, rels in insns.items():
    for pos, base, target in rels:
        struct.pack_into('<H', new, moved(pos), (moved(target) - moved(base)) & 0xffff)

assert mo + len(new) <= 10000
open(path, 'wb').write(f[:mo] + new)
print('car %d: sprite on vertex %d added at model offset %d, face in %s' % (CAR, ref, at, 'car%d.pcx' % CAR))
