#!/usr/bin/env python3
"""Generate the tiny PNG fixtures used by the pull-mode demo.

Standard library only (zlib, struct) - no third-party packages, no network.
Each file is a few hundred bytes so the whole demo runs in one shot.
"""
import os
import struct
import sys
import zlib


def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))


def make_png(path, width, height, pixfmt, bitdepth, raw_lines, idat_parts=1):
    """pixfmt: 0 gray, 2 rgb, 3 indexed, 6 rgba; raw_lines: bytes per row."""
    color_type = {0: 0, 2: 2, 3: 3, 6: 6}[pixfmt]
    out = [b"\x89PNG\r\n\x1a\n"]
    out.append(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height,
                                          bitdepth, color_type, 0, 0, 0)))
    if pixfmt == 3:
        pal = bytes([0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0])
        out.append(chunk(b"PLTE", pal))
    out.append(chunk(b"tEXt", b"Comment\x00pull-mode fixture"))
    raw = b"".join(b"\x00" + r for r in raw_lines)  # filter type 0 per row
    comp = zlib.compress(raw, 9)
    # split the compressed stream over several IDAT chunks (valid per spec)
    step = (len(comp) + idat_parts - 1) // idat_parts
    for i in range(0, len(comp), step):
        out.append(chunk(b"IDAT", comp[i:i + step]))
    out.append(chunk(b"IEND", b""))
    data = b"".join(out)
    with open(path, "wb") as f:
        f.write(data)
    return len(data)


def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    sizes = {}

    # 16x12 RGBA - alpha varies so the draw callback exercises blending paths
    w, h = 16, 12
    lines = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            row += bytes([(x * 16) & 0xff, (y * 20) & 0xff,
                          ((x + y) * 8) & 0xff, 255 if (x + y) % 3 else 96])
        lines.append(bytes(row))
    sizes["rgba.png"] = make_png(os.path.join(outdir, "rgba.png"), w, h, 6, 8, lines)

    # 24x12 RGB, 8 bits/sample
    w, h = 24, 12
    lines = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            row += bytes([x * 10 & 0xff, y * 20 & 0xff, (x ^ y) * 7 & 0xff])
        lines.append(bytes(row))
    sizes["rgb.png"] = make_png(os.path.join(outdir, "rgb.png"), w, h, 2, 8, lines)

    # 20x10 indexed, 8-bit indices -> palette + tRNS-free alpha path
    w, h = 20, 10
    lines = []
    for y in range(h):
        lines.append(bytes([((x * 3 + y) % 5) for x in range(w)]))
    sizes["indexed8.png"] = make_png(os.path.join(outdir, "indexed8.png"),
                                     w, h, 3, 8, lines)

    # 32x8 grayscale, 8 bits
    w, h = 32, 8
    lines = [bytes([(x * 8 + y * 3) & 0xff for x in range(w)])
             for y in range(h)]
    sizes["gray8.png"] = make_png(os.path.join(outdir, "gray8.png"),
                                  w, h, 0, 8, lines)

    # 16x6 indexed, 4 bits per pixel (two pixels per byte, padded rows)
    w, h = 16, 6
    lines = []
    for y in range(h):
        packed = bytearray()
        for x in range(0, w, 2):
            packed.append(((x + y) % 5) << 4 | ((x + 1 + y) % 5))
        lines.append(bytes(packed))
    sizes["indexed4.png"] = make_png(os.path.join(outdir, "indexed4.png"),
                                     w, h, 3, 4, lines)

    # 24x12 RGB split over four IDAT chunks, so chunk-boundary resumption is
    # exercised by the pause/short-read scenarios
    w, h = 24, 12
    lines = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            row += bytes([x * 11 & 0xff, y * 19 & 0xff, (x * y) & 0xff])
        lines.append(bytes(row))
    sizes["multi_idat.png"] = make_png(os.path.join(outdir, "multi_idat.png"),
                                       w, h, 2, 8, lines, idat_parts=4)

    for name in sorted(sizes):
        print("%-14s %4d bytes" % (name, sizes[name]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "samples"))
