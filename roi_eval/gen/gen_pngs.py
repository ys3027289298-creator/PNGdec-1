#!/usr/bin/env python3
#
# Generate small, deterministic PNG test images with the Python standard
# library only (struct + zlib). No third-party modules, no network access.
# Every generated scanline uses a different PNG filter so that all five
# defilter paths (none/sub/up/avg/paeth) are exercised on every decode.
#
# Run with: python3 -I gen_pngs.py <out_dir>
#
import os
import struct
import sys
import zlib


def chunk(tag, data):
    out = struct.pack(">I", len(data)) + tag + data
    return out + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_png(path, width, height, color_type, bit_depth, raw_rows, palette=None):
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", width, height, bit_depth, color_type, 0, 0, 0)
    # Rotate the filter type per row: all 5 filters appear repeatedly
    filtered = bytearray()
    for y, row in enumerate(raw_rows):
        filtered.append(y % 5)
        filtered.extend(row)
    idat = zlib.compress(bytes(filtered), 9)
    with open(path, "wb") as f:
        f.write(sig)
        f.write(chunk(b"IHDR", ihdr))
        if palette is not None:
            f.write(chunk(b"PLTE", palette))
        f.write(chunk(b"IDAT", idat))
        f.write(chunk(b"IEND", b""))


def gray8(width, height):
    rows = []
    for y in range(height):
        row = bytearray(width)
        for x in range(width):
            row[x] = (x * 7 + y * 13 + (x ^ y) * 3) & 0xFF
        rows.append(row)
    return rows


def rgb24(width, height):
    rows = []
    for y in range(height):
        row = bytearray(width * 3)
        for x in range(width):
            row[x * 3 + 0] = (x * 11 + y * 5) & 0xFF      # R
            row[x * 3 + 1] = (x * 3 + y * 17 + 40) & 0xFF  # G
            row[x * 3 + 2] = (x * 19 + y * 7 + 80) & 0xFF  # B
        rows.append(row)
    return rows


def rgba32(width, height):
    rows = []
    for y in range(height):
        row = bytearray(width * 4)
        for x in range(width):
            row[x * 4 + 0] = (x * 11 + y * 5) & 0xFF
            row[x * 4 + 1] = (x * 3 + y * 17 + 40) & 0xFF
            row[x * 4 + 2] = (x * 19 + y * 7 + 80) & 0xFF
            row[x * 4 + 3] = ((x + y) * 9) & 0xFF  # varying alpha
        rows.append(row)
    return rows


def indexed4(width, height):
    # 16-entry grayscale palette; pixel values follow a formula
    palette = bytearray()
    for i in range(16):
        v = i * 17
        palette += bytes((v, v, v))
    rows = []
    for y in range(height):
        row = bytearray((width + 1) // 2)
        for x in range(width):
            idx = (x + y * 3 + (x ^ y)) & 0x0F
            if x & 1:
                row[x // 2] |= idx & 0x0F
            else:
                row[x // 2] |= (idx << 4) & 0xF0
        rows.append(row)
    return rows, bytes(palette)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    W, H = 256, 192

    write_png(os.path.join(out, "gray8_64x48.png"), W, H, 0, 8, gray8(W, H))
    write_png(os.path.join(out, "rgb24_64x48.png"), W, H, 2, 8, rgb24(W, H))
    write_png(os.path.join(out, "rgba32_64x48.png"), W, H, 6, 8, rgba32(W, H))
    rows, pal = indexed4(W, H)
    write_png(os.path.join(out, "indexed4_64x48.png"), W, H, 3, 4, rows, pal)
    print("generated 4 PNG files in", out)


if __name__ == "__main__":
    main()
