#!/usr/bin/env python3
"""Generate the omarchy-xrdp-hidden Xcursor theme.

Every cursor in it is fully transparent, so Hyprland's own cursor drawing
(which lands in the RDP video and trails the hand by an encode round trip)
disappears. Each shape gets its own hotspot instead: on an 8x8 grid of the
image, cell (id % 8, id // 8). The backend reads the hotspot from Hyprland's
cursor capture session, decodes the shape and sends the RDP client the real
image from the user's theme, which the client draws locally without lag.

The ids must match SHAPES in backend/src/cursor.c.

Usage: generate.py OUTDIR
"""
import os
import struct
import sys

# The cursor-shape-v1 names, in protocol order: the id is the index
SHAPES = [
    None, "default", "context-menu", "help", "pointer", "progress", "wait",
    "cell", "crosshair", "text", "vertical-text", "alias", "copy", "move",
    "no-drop", "not-allowed", "grab", "grabbing", "e-resize", "n-resize",
    "ne-resize", "nw-resize", "s-resize", "se-resize", "sw-resize", "w-resize",
    "ew-resize", "ns-resize", "nesw-resize", "nwse-resize", "col-resize",
    "row-resize", "all-scroll", "zoom-in", "zoom-out", "dnd-ask", "all-resize",
]

# Legacy X cursor names apps and Hyprland still ask for
ALIASES = {
    "default": ["left_ptr", "arrow", "top_left_arrow"],
    "context-menu": [],
    "help": ["question_arrow", "whats_this", "left_ptr_help"],
    "pointer": ["hand2", "hand1", "pointing_hand", "hand"],
    "progress": ["left_ptr_watch", "half-busy"],
    "wait": ["watch"],
    "cell": ["plus"],
    "crosshair": ["cross", "tcross", "cross_reverse", "diamond_cross"],
    "text": ["xterm", "ibeam"],
    "vertical-text": [],
    "alias": ["dnd-link", "link"],
    "copy": ["dnd-copy"],
    "move": ["fleur", "dnd-move", "size_all"],
    "no-drop": ["dnd-no-drop"],
    "not-allowed": ["crossed_circle", "circle", "X_cursor", "forbidden"],
    "grab": ["openhand"],
    "grabbing": ["closedhand", "dnd-none"],
    "e-resize": ["right_side"],
    "n-resize": ["top_side"],
    "ne-resize": ["top_right_corner"],
    "nw-resize": ["top_left_corner"],
    "s-resize": ["bottom_side"],
    "se-resize": ["bottom_right_corner"],
    "sw-resize": ["bottom_left_corner"],
    "w-resize": ["left_side"],
    "ew-resize": ["sb_h_double_arrow", "h_double_arrow", "size_hor"],
    "ns-resize": ["sb_v_double_arrow", "v_double_arrow", "size_ver"],
    "nesw-resize": ["size_bdiag", "fd_double_arrow"],
    "nwse-resize": ["size_fdiag", "bd_double_arrow"],
    "col-resize": ["split_h", "sb_h_double_arrow"],
    "row-resize": ["split_v", "sb_v_double_arrow"],
    "all-scroll": [],
    "zoom-in": [],
    "zoom-out": [],
    "dnd-ask": [],
    "all-resize": [],
}

SIZES = [24, 32, 36, 48, 64, 72, 96]
IMAGE_TYPE = 0xFFFD0002


def xcursor(shape_id):
    col, row = shape_id % 8, shape_id // 8
    chunks = []
    for size in SIZES:
        hot_x, hot_y = col * size // 8, row * size // 8
        header = struct.pack("<9I", 36, IMAGE_TYPE, size, 1, size, size, hot_x, hot_y, 0)
        chunks.append((size, header + bytes(size * size * 4)))
    ntoc = len(chunks)
    out = struct.pack("<4sIII", b"Xcur", 16, 0x10000, ntoc)
    pos = 16 + ntoc * 12
    toc = b""
    for size, data in chunks:
        toc += struct.pack("<III", IMAGE_TYPE, size, pos)
        pos += len(data)
    return out + toc + b"".join(data for _, data in chunks)


def main():
    out = sys.argv[1]
    cursors = os.path.join(out, "cursors")
    os.makedirs(cursors, exist_ok=True)
    with open(os.path.join(out, "index.theme"), "w") as f:
        f.write("[Icon Theme]\nName=omarchy-xrdp-hidden\n"
                "Comment=Invisible cursors for Omarchy RDP sessions (drawn by the client)\n")
    written = set()
    for shape_id, name in enumerate(SHAPES):
        if name is None:
            continue
        data = xcursor(shape_id)
        for n in [name] + ALIASES[name]:
            if n in written:  # an alias claimed by an earlier shape
                continue
            with open(os.path.join(cursors, n), "wb") as f:
                f.write(data)
            written.add(n)


if __name__ == "__main__":
    main()
