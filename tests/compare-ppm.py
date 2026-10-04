#!/usr/bin/env python3
"""Compare two same-size binary PPMs (server render vs client view).

H.264 at the encoder's quality differs by a few levels per pixel; corruption
(a lost reference frame) leaves 16x16 blocks that are far off. Reports the
share of 16x16 blocks whose mean difference exceeds 24 levels; exit 1 if
over 0.5%.
"""
import sys


def read_ppm(path):
    data = open(path, "rb").read()
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[pos + 1:pos + 1 + w * h * 3]


w1, h1, a = read_ppm(sys.argv[1])
w2, h2, b = read_ppm(sys.argv[2])
if (w1, h1) != (w2, h2):
    print(f"FAIL size differs: server {w1}x{h1}, client {w2}x{h2}")
    sys.exit(2)

bad = total = 0
step = 2  # sample every other pixel in a block: plenty, and faster
for by in range(0, h1 - 15, 16):
    for bx in range(0, w1 - 15, 16):
        diff = n = 0
        for y in range(by, by + 16, step):
            row = (y * w1 + bx) * 3
            for x in range(0, 48, 3 * step):
                i = row + x
                diff += abs(a[i] - b[i]) + abs(a[i + 1] - b[i + 1]) + abs(a[i + 2] - b[i + 2])
                n += 3
        total += 1
        if diff / n > 24:
            bad += 1
share = 100 * bad / total
print(f"{'FAIL' if share > 0.5 else 'PASS'} {bad} of {total} blocks corrupted ({share:.2f}%)")
sys.exit(1 if share > 0.5 else 0)
