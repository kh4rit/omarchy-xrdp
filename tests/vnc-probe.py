#!/usr/bin/env python3
"""Minimal RFB client: connect to a VNC Unix socket (security type None),
request one full framebuffer update in Raw encoding and save it as a PPM.

Usage: vnc-probe.py SOCKET OUT.ppm [timeout-seconds]
"""
import os
import socket
import struct
import sys


def recv_exact(sock, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise EOFError(f"connection closed after {len(buf)}/{n} bytes")
        buf += chunk
    return bytes(buf)


def main():
    path, out = sys.argv[1], sys.argv[2]
    timeout = float(sys.argv[3]) if len(sys.argv) > 3 else 10

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(path)

    version = recv_exact(s, 12)
    print("server version", version.strip().decode())
    s.sendall(b"RFB 003.008\n")

    ntypes = recv_exact(s, 1)[0]
    types = list(recv_exact(s, ntypes))
    print("security types", types)
    if 1 not in types:
        sys.exit("server does not offer security type None")
    s.sendall(bytes([1]))
    result = struct.unpack(">I", recv_exact(s, 4))[0]
    if result != 0:
        sys.exit(f"security handshake failed ({result})")

    s.sendall(bytes([1]))  # ClientInit, shared
    width, height = struct.unpack(">HH", recv_exact(s, 4))
    pf = recv_exact(s, 16)
    bpp, depth, big_endian, true_color = pf[0], pf[1], pf[2], pf[3]
    rmax, gmax, bmax = struct.unpack(">HHH", pf[4:10])
    rshift, gshift, bshift = pf[10], pf[11], pf[12]
    name_len = struct.unpack(">I", recv_exact(s, 4))[0]
    name = recv_exact(s, name_len).decode(errors="replace")
    print(f"desktop '{name}' {width}x{height} bpp {bpp} depth {depth}")

    # Ask for 32bpp little-endian true colour so decoding is predictable
    s.sendall(struct.pack(">BxxxBBBBHHHBBBxxx", 0, 32, 24, 0, 1, 255, 255, 255, 16, 8, 0))
    # SetEncodings: Raw, plus ExtendedDesktopSize (-308) and DesktopSize
    # (-223) the way xrdp asks for them
    encodings = [int(e) for e in os.environ.get("PROBE_ENCODINGS", "0,-308,-223").split(",")]
    s.sendall(struct.pack(">BxH", 2, len(encodings)) + b"".join(struct.pack(">i", e) for e in encodings))
    s.sendall(struct.pack(">BBHHHH", 3, 0, 0, 0, width, height))  # full update

    pixels = bytearray(width * height * 3)
    covered = 0
    while covered < width * height:
        msg_type = recv_exact(s, 1)[0]
        if msg_type != 0:
            # Skip non-update messages we know the size of
            if msg_type == 2:  # Bell
                continue
            if msg_type == 3:  # ServerCutText
                recv_exact(s, 3)
                n = struct.unpack(">I", recv_exact(s, 4))[0]
                recv_exact(s, n)
                continue
            sys.exit(f"unexpected server message {msg_type}")
        recv_exact(s, 1)
        nrects = struct.unpack(">H", recv_exact(s, 2))[0]
        print(f"FramebufferUpdate with {nrects} rects")
        for _ in range(nrects):
            x, y, w, h, enc = struct.unpack(">HHHHi", recv_exact(s, 12))
            if enc != 0:
                print(f"skipping pseudo/other encoding {enc} ({w}x{h})")
                if enc == -239:  # Cursor: pixels + bitmask
                    recv_exact(s, w * h * 4 + ((w + 7) // 8) * h)
                elif enc in (-223,):  # DesktopSize
                    pass
                elif enc == -308:  # ExtendedDesktopSize
                    nscreens = recv_exact(s, 4)[0]
                    for _ in range(nscreens):
                        sid, sx, sy, sw, sh, flags = struct.unpack(">IHHHHI", recv_exact(s, 16))
                        print(f"  screen id {sid} {sw}x{sh}+{sx}+{sy} flags {flags}")
                    print(f"  ExtendedDesktopSize rect reason x={x} status y={y}")
                else:
                    sys.exit(f"cannot skip encoding {enc}")
                continue
            data = recv_exact(s, w * h * 4)
            for row in range(h):
                for col in range(w):
                    i = (row * w + col) * 4
                    b, g, r = data[i], data[i + 1], data[i + 2]
                    o = ((y + row) * width + (x + col)) * 3
                    pixels[o:o + 3] = bytes((r, g, b))
            covered += w * h
            print(f"rect {x},{y} {w}x{h} raw")

    with open(out, "wb") as f:
        f.write(f"P6 {width} {height} 255\n".encode())
        f.write(pixels)
    distinct = len(set(bytes(pixels[i:i + 3]) for i in range(0, len(pixels), 3 * 997)))
    print(f"saved {out}; ~{distinct} distinct sampled colours")


if __name__ == "__main__":
    main()
