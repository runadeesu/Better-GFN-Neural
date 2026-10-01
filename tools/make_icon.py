#!/usr/bin/env python3
"""Generates resources/app.ico (Better GFN Neural logo) with the Python stdlib only.

Logo: rounded square with a mint -> indigo gradient and a stylised "N" made of
three neural links and four nodes. Rendered with 4x4 supersampling into PNG
frames (16..256 px) packed into one ICO file.
"""
import math
import os
import struct
import sys
import zlib

MINT = (43, 227, 176)
INDIGO = (108, 123, 255)
INK = (9, 12, 17)


def lerp(a, b, t):
    return a + (b - a) * t


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    ex, ey = ax + t * dx - px, ay + t * dy - py
    return math.sqrt(ex * ex + ey * ey)


def render(size):
    ss = 4
    r = 0.24  # corner radius (relative)
    nodes = [(0.3, 0.72), (0.3, 0.28), (0.7, 0.72), (0.7, 0.28)]
    links = [(nodes[0], nodes[1]), (nodes[1], nodes[2]), (nodes[2], nodes[3])]
    stroke = 0.075
    node_r = 0.085
    pixels = bytearray()
    for y in range(size):
        pixels.append(0)  # PNG filter type
        for x in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    u = (x + (sx + 0.5) / ss) / size
                    v = (y + (sy + 0.5) / ss) / size
                    # rounded square
                    qx = max(abs(u - 0.5) - (0.5 - r), 0.0)
                    qy = max(abs(v - 0.5) - (0.5 - r), 0.0)
                    if math.sqrt(qx * qx + qy * qy) > r:
                        continue
                    t = (u + v) * 0.5
                    col = [lerp(MINT[i], INDIGO[i], t) for i in range(3)]
                    d = min(seg_dist(u, v, a[0], a[1], b[0], b[1]) for a, b in links)
                    dn = min(math.hypot(u - n[0], v - n[1]) for n in nodes)
                    if d < stroke * 0.5 or dn < node_r:
                        col = list(INK)
                    acc[0] += col[0]
                    acc[1] += col[1]
                    acc[2] += col[2]
                    acc[3] += 255
            n = ss * ss
            a = acc[3] / n
            if a > 0:
                pixels += bytes([int(acc[0] / (acc[3] / 255)), int(acc[1] / (acc[3] / 255)), int(acc[2] / (acc[3] / 255)), int(a + 0.5)])
            else:
                pixels += bytes([0, 0, 0, 0])
    return bytes(pixels)


def png(size):
    raw = render(size)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "resources", "app.ico")
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = [png(s) for s in sizes]
    header = struct.pack("<HHH", 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    entries = b""
    for s, data in zip(sizes, images):
        entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "wb") as f:
        f.write(header + entries + b"".join(images))
    # Also export a 256px PNG for documentation
    with open(os.path.join(os.path.dirname(os.path.abspath(out)), "logo_256.png"), "wb") as f:
        f.write(images[-1])
    print("wrote", out)


if __name__ == "__main__":
    main()
