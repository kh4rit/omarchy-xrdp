#!/usr/bin/env python3
"""Convert a binary PPM (P6) to PNG with only the standard library.
Optional crop: ppm2png.py IN OUT [X Y W H]"""
import struct
import sys
import zlib


def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()
    parts = data.split(maxsplit=4)
    w, h = int(parts[1]), int(parts[2])
    pixels = parts[4]
    x0, y0, cw, ch = 0, 0, w, h
    if len(sys.argv) == 7:
        x0, y0, cw, ch = map(int, sys.argv[3:7])
    rows = b"".join(b"\0" + pixels[((y0 + y) * w + x0) * 3:((y0 + y) * w + x0 + cw) * 3]
                    for y in range(ch))

    def chunk(kind, body):
        c = kind + body
        return struct.pack(">I", len(body)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", cw, ch, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
    open(dst, "wb").write(png)


if __name__ == "__main__":
    main()
