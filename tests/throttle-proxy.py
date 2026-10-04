#!/usr/bin/env python3
"""TCP relay that caps server-to-client throughput, standing in for Wi-Fi
between an RDP client and xrdp (no root needed, unlike tc).

Usage: throttle-proxy.py LISTEN_PORT TARGET_PORT MBIT
"""
import socket
import sys
import threading
import time

listen_port, target_port, mbit = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3])
rate = mbit * 1_000_000 / 8  # bytes/s


def pipe(src, dst, limited):
    sent, start = 0, time.monotonic()
    try:
        while True:
            data = src.recv(16384)
            if not data:
                break
            dst.sendall(data)
            if limited:
                sent += len(data)
                ahead = sent / rate - (time.monotonic() - start)
                if ahead > 0:
                    time.sleep(ahead)
                elif ahead < -0.05:  # idle a while: don't bank credit
                    sent, start = 0, time.monotonic()
    except OSError:
        pass
    for s in (src, dst):
        try:
            s.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass


srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", listen_port))
srv.listen()
while True:
    client, _ = srv.accept()
    server = socket.create_connection(("127.0.0.1", target_port))
    for s in (client, server):
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    # Small client-side buffer, as over a real link
    client.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 256 * 1024)
    threading.Thread(target=pipe, args=(server, client, True), daemon=True).start()
    threading.Thread(target=pipe, args=(client, server, False), daemon=True).start()
