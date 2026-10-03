#!/usr/bin/env python3
"""Send input to a VNC Unix socket (security type None).

Usage: vnc-input.py SOCKET ACTION...
  key:NAME        press and release a key (X keysym name or 0xHEX)
  combo:A+B+C     hold A, B..., tap the last one, release
  text:STRING     type ASCII text
  move:X,Y        move the pointer
  click:X,Y       left-click at X,Y
  sleep:SECONDS
"""
import socket
import struct
import sys
import time

KEYSYMS = {
    "Return": 0xFF0D, "Escape": 0xFF1B, "Tab": 0xFF09, "BackSpace": 0xFF08,
    "space": 0x0020, "Super_L": 0xFFEB, "Shift_L": 0xFFE1, "Control_L": 0xFFE3,
    "Alt_L": 0xFFE9, "Left": 0xFF51, "Up": 0xFF52, "Right": 0xFF53, "Down": 0xFF54,
}


def keysym(name):
    if name in KEYSYMS:
        return KEYSYMS[name]
    if name.startswith("0x"):
        return int(name, 16)
    if len(name) == 1:
        return ord(name)
    raise SystemExit(f"unknown key {name}")


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise EOFError
        buf += chunk
    return buf


def connect(path):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect(path)
    recv_exact(s, 12)
    s.sendall(b"RFB 003.008\n")
    n = recv_exact(s, 1)[0]
    recv_exact(s, n)
    s.sendall(bytes([1]))
    if struct.unpack(">I", recv_exact(s, 4))[0] != 0:
        raise SystemExit("security handshake failed")
    s.sendall(bytes([1]))
    recv_exact(s, 4 + 16)
    recv_exact(s, struct.unpack(">I", recv_exact(s, 4))[0])
    return s


def key(s, sym, down):
    s.sendall(struct.pack(">BBxxI", 4, 1 if down else 0, sym))


def pointer(s, x, y, buttons=0):
    s.sendall(struct.pack(">BBHH", 5, buttons, x, y))


def main():
    s = connect(sys.argv[1])
    for action in sys.argv[2:]:
        kind, _, arg = action.partition(":")
        if kind == "key":
            key(s, keysym(arg), True)
            key(s, keysym(arg), False)
        elif kind == "combo":
            syms = [keysym(k) for k in arg.split("+")]
            for sym in syms:
                key(s, sym, True)
            time.sleep(0.05)
            for sym in reversed(syms):
                key(s, sym, False)
        elif kind == "text":
            for ch in arg:
                key(s, ord(ch), True)
                key(s, ord(ch), False)
                time.sleep(0.01)
        elif kind == "move":
            x, y = map(int, arg.split(","))
            pointer(s, x, y)
        elif kind == "click":
            x, y = map(int, arg.split(","))
            pointer(s, x, y)
            pointer(s, x, y, 1)
            pointer(s, x, y, 0)
        elif kind == "sleep":
            time.sleep(float(arg))
        else:
            raise SystemExit(f"unknown action {action}")
        time.sleep(0.05)
    time.sleep(0.3)
    s.close()


if __name__ == "__main__":
    main()
