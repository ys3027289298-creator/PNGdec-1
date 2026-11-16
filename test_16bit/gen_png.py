#!/usr/bin/env python3
# Generate small 8/16-bit PNG test images using only the Python standard
# library (zlib/struct). No third-party code, no network access.
# Run with: python3 -I gen_png.py
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")

W, H = 8, 5


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def be16(v):
    return struct.pack(">H", v & 0xFFFF)


def raw_bytes(pixels, channels, bpp_bytes, filt):
    """Build PNG scanlines (filter byte + filtered bytes) from a flat
    list of per-channel integer samples."""
    bpp = bpp_bytes * channels
    rows = []
    prev = bytearray()
    idx = 0
    for y in range(H):
        raw = bytearray()
        # PNG packs samples per pixel: for each pixel, all its channels.
        for x in range(W):
            for ch in range(channels):
                v = pixels[(y * W + x) * channels + ch]
                if bpp_bytes == 2:
                    raw += be16(v)
                else:
                    raw.append(v & 0xFF)
        cur = bytearray(raw)
        f = filt[y] if isinstance(filt, (list, tuple)) else filt
        out = bytearray(len(cur) + 1)
        out[0] = f
        for i in range(len(cur)):
            a = cur[i - bpp] if i >= bpp else 0
            b = prev[i] if prev else 0
            c = prev[i - bpp] if (prev and i >= bpp) else 0
            if f == 0:
                out[i + 1] = cur[i]
            elif f == 1:
                out[i + 1] = (cur[i] - a) & 0xFF
            elif f == 2:
                out[i + 1] = (cur[i] - b) & 0xFF
            elif f == 3:
                out[i + 1] = (cur[i] - ((a + b) >> 1)) & 0xFF
            else:
                out[i + 1] = (cur[i] - paeth(a, b, c)) & 0xFF
        rows.append(bytes(out))
        prev = cur
        idx += W * channels
    return b"".join(rows)


def chunk(tag, payload):
    return (struct.pack(">I", len(payload)) + tag + payload +
            struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))


def write_png(path, color_type, bit_depth, pixels, filt=0, interlace=0):
    channels = {0: 1, 2: 3, 4: 2, 6: 4}[color_type]
    bpp_bytes = 1 if bit_depth <= 8 else 2
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", W, H, bit_depth, color_type, 0, 0,
                       interlace)
    idat = zlib.compress(raw_bytes(pixels, channels, bpp_bytes, filt), 9)
    with open(path, "wb") as f:
        f.write(sig)
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", idat))
        f.write(chunk(b"IEND", b""))


def write_sidecar(path, pixels, channels, bit_depth):
    """Dump the exact sample values the decoder must reproduce."""
    bpp_bytes = 1 if bit_depth <= 8 else 2
    with open(path, "wb") as f:
        f.write(struct.pack(">IIHH", W, H, channels, bit_depth))
        for v in pixels:
            f.write(v.to_bytes(bpp_bytes, "big"))


def gray_pixels():
    # Deterministic pattern exercising the low byte (rounding must differ
    # from truncation): ramps plus values ending in 0x80..0xFF.
    px = []
    boundaries = [0xFFFF, 0xFF80, 0x0080, 0x0000]
    for y in range(H):
        for x in range(W):
            v = (y * 11000 + x * 4099 + 0x123) & 0xFFFF
            if (x + y) % 3 == 0:
                v = (v & 0xFF00) | 0x9A  # low byte >= 128
            if y == 0 and x < len(boundaries):
                v = boundaries[x]  # round/clamp edge values
            px.append(v)
    return px


def rgb_pixels():
    px = []
    g_boundaries = [0xFFFF, 0xFF80, 0x0080, 0x0000]
    for y in range(H):
        for x in range(W):
            r = (x * 7001 + y * 3001 + 17) & 0xFFFF
            g = (x * 991 + y * 22001 + 4095) & 0xFFFF
            b = (x * 33001 + y * 661 + 200) & 0xFFFF
            if x % 2 == 0:
                g = (g & 0xFF00) | 0x81  # force rounding difference
            if y == 0 and x < len(g_boundaries):
                g = g_boundaries[x]  # round/clamp edge values
            px += [r, g, b]
    return px


# Every filter type appears at least once so all de-filter paths run on
# 16-bit data (scanlines are 1 and 2 bytes per sample respectively).
FILTERS = [0, 1, 2, 3, 4]


def main():
    os.makedirs(DATA, exist_ok=True)

    g16 = gray_pixels()
    write_png(os.path.join(DATA, "gray16.png"), 0, 16, g16, FILTERS)
    write_sidecar(os.path.join(DATA, "gray16.raw"), g16, 1, 16)

    rgb16 = rgb_pixels()
    write_png(os.path.join(DATA, "rgb16.png"), 2, 16, rgb16, FILTERS)
    write_sidecar(os.path.join(DATA, "rgb16.raw"), rgb16, 3, 16)

    # 8-bit regression set: same family of patterns, native 8-bit files.
    g8 = [(v >> 8) & 0xFF for v in g16]
    write_png(os.path.join(DATA, "gray8.png"), 0, 8, g8, FILTERS)
    write_sidecar(os.path.join(DATA, "gray8.raw"), g8, 1, 8)

    rgb8 = [v >> 8 for v in rgb16]
    write_png(os.path.join(DATA, "rgb8.png"), 2, 8, rgb8, FILTERS)
    write_sidecar(os.path.join(DATA, "rgb8.raw"), rgb8, 3, 8)

    # Must be rejected: unsupported bit depth / pixel type / interlace.
    ga16 = []
    for i in range(W * H):
        ga16 += [g16[i], g16[W * H - 1 - i]]  # gray + alpha per pixel
    write_png(os.path.join(DATA, "bad_grayalpha16.png"), 4, 16, ga16, 0)
    write_png(os.path.join(DATA, "bad_interlace16.png"), 0, 16, g16, 0,
              interlace=1)

    # Bit depth 32 is not defined by PNG: patch the IHDR of gray16 and
    # fix its CRC so the file is otherwise structurally valid.
    with open(os.path.join(DATA, "gray16.png"), "rb") as f:
        buf = bytearray(f.read())
    buf[24] = 32
    crc = zlib.crc32(bytes(buf[12:29])) & 0xFFFFFFFF
    buf[29:33] = struct.pack(">I", crc)
    with open(os.path.join(DATA, "bad_bpp32.png"), "wb") as f:
        f.write(bytes(buf))

    for name in sorted(os.listdir(DATA)):
        print(name, os.path.getsize(os.path.join(DATA, name)), "bytes")


if __name__ == "__main__":
    main()
