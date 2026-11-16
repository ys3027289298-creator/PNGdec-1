#!/usr/bin/env python3
#
# gen_images.py - generate tiny deterministic PNG test images and reference
# raw output using ONLY the Python standard library (no third-party code,
# no network). Filter type 0 is used for every scanline, so the reference
# bytes are exactly what the decoder must leave in the image buffer.
#
import json
import os
import struct
import sys
import zlib

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")

# Adam7 pass starting position and step
ADAM7 = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
         (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]

GRAYSCALE, TRUECOLOR, INDEXED, GRAY_ALPHA, TRUECOLOR_ALPHA = 0, 2, 3, 4, 6


def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))


def pack_pixels(pixels, bitdepth, ptype):
    """pack a list of integer samples/indices into PNG scanline bytes"""
    if bitdepth >= 8:
        if ptype == GRAYSCALE:
            return bytes(pixels)
        out = bytearray()
        for px in pixels:
            out.extend(px if isinstance(px, tuple) else (px,))
        return bytes(out)
    # sub-byte packing: 1/2/4 bits, leftmost pixel in the high bits
    ppb = 8 // bitdepth
    mask = (1 << bitdepth) - 1
    out = bytearray((len(pixels) + ppb - 1) // ppb)
    for i, v in enumerate(pixels):
        byte, shift = divmod(i, ppb)
        out[byte] |= (v & mask) << (8 - bitdepth - shift * bitdepth)
    return bytes(out)


def pitch_of(width, bitdepth, ptype):
    if ptype in (GRAYSCALE, INDEXED):
        return (width * bitdepth + 7) // 8
    if ptype == TRUECOLOR:
        return (3 * bitdepth * width + 7) // 8
    if ptype == GRAY_ALPHA:
        return (2 * bitdepth * width + 7) // 8
    return (4 * bitdepth * width + 7) // 8  # truecolor+alpha


def pixel_value(x, y, w, h, ptype, bitdepth, palette_size=None):
    """deterministic pattern, identical pixels at the same (x,y)"""
    if ptype == INDEXED:
        return (x * 3 + y * 5 + (x ^ y)) % palette_size
    if ptype == GRAYSCALE:
        return (x * 7 + y * 11) & 0xff
    r = (x * 13 + y * 17) & 0xff
    g = (x * 19 + y * 23 + 7) & 0xff
    b = (x * 29 + y * 31 + 13) & 0xff
    if ptype == TRUECOLOR:
        return (r, g, b)
    if ptype == GRAY_ALPHA:
        return (r & 0xff, ((x + y) * 17) & 0xff)
    # truecolor + alpha
    a = ((x * 5 + y * 9) ^ 0x5a) & 0xff
    return (r, g, b, a)


def build_image(w, h, bitdepth, ptype, interlaced, palette_size):
    pixels = [[pixel_value(x, y, w, h, ptype, bitdepth, palette_size)
               for x in range(w)] for y in range(h)]
    return pixels


def subimage(pixels, w, h, xstart, ystart, xstep, ystep):
    sub = []
    y = ystart
    while y < h:
        row = []
        x = xstart
        while x < w:
            row.append(pixels[y][x])
            x += xstep
        sub.append(row)
        y += ystep
    return sub


def filtered_stream(pixels, w, h, bitdepth, ptype, interlaced, use_filters):
    """zlib stream of filtered scanlines.

    Filter 0 (NONE) everywhere by default so the reference bytes are the raw
    pixels. With use_filters, the five PNG filters are cycled per scanline and
    applied correctly, still producing the same decoded reference output.
    """
    def paeth(a, b, c):
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        if pa <= pb and pa <= pc:
            return a
        if pb <= pc:
            return b
        return c

    raw = bytearray()
    if not interlaced:
        passes = [(pixels, w, h)]
    else:
        passes = []
        for (x0, y0, sx, sy) in ADAM7:
            sub = subimage(pixels, w, h, x0, y0, sx, sy)
            sw = (w - x0 + sx - 1) // sx if w > x0 else 0
            sh = (h - y0 + sy - 1) // sy if h > y0 else 0
            passes.append((sub, sw, sh))
    line_no = 0
    for (sub, sw, sh) in passes:
        if sw == 0 or sh == 0:
            continue
        bpp = max(1, bitdepth // 8)
        if ptype in (TRUECOLOR, TRUECOLOR_ALPHA, GRAY_ALPHA) and bitdepth >= 8:
            bpp = {TRUECOLOR: 3, TRUECOLOR_ALPHA: 4, GRAY_ALPHA: 2}[ptype]
        prev = bytearray()
        for row in sub:
            line = bytearray()
            line.append(0)
            line.extend(pack_pixels(row, bitdepth, ptype))
            pitch = len(line) - 1
            if use_filters and pitch > 0:
                ftype = line_no % 5
                recon = bytearray(line[1:])
                enc = bytearray(pitch)
                for x in range(pitch):
                    a = recon[x - bpp] if x >= bpp else 0
                    b = prev[x] if prev else 0
                    c = prev[x - bpp] if (prev and x >= bpp) else 0
                    cur = recon[x]
                    if ftype == 0:
                        enc[x] = cur
                    elif ftype == 1:
                        enc[x] = (cur - a) & 0xff
                    elif ftype == 2:
                        enc[x] = (cur - b) & 0xff
                    elif ftype == 3:
                        enc[x] = (cur - ((a + b) >> 1)) & 0xff
                    else:
                        enc[x] = (cur - paeth(a, b, c)) & 0xff
                raw.append(ftype)
                raw.extend(enc)
                prev = recon
            else:
                raw.extend(line)
                prev = bytearray(line[1:])
            line_no += 1
    return zlib.compress(bytes(raw), 9)


def reference_bytes(pixels, w, h, bitdepth, ptype):
    """raw full-image bytes the decoder should produce (pitch-aligned rows)"""
    pitch = pitch_of(w, bitdepth, ptype)
    out = bytearray(pitch * h)
    for y, row in enumerate(pixels):
        out[y * pitch:y * pitch + pitch] = pack_pixels(row, bitdepth, ptype)
    return bytes(out), pitch


def write_png(path, w, h, bitdepth, ptype, interlaced, palette_size, pixels,
              use_filters=False):
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, bitdepth, ptype, 0, 0,
                       1 if interlaced else 0)
    plte = b""
    trns = b""
    if ptype == INDEXED:
        plte = bytearray()
        for i in range(palette_size):
            plte += bytes(((i * 37) & 0xff, (i * 61 + 5) & 0xff,
                           (i * 97 + 11) & 0xff))
        plte = bytes(plte)
        # half the palette entries carry an alpha value (tRNS chunk)
        trns = bytes((0 if (i & 1) else 255) for i in range(palette_size))
    data = filtered_stream(pixels, w, h, bitdepth, ptype, interlaced,
                           use_filters)
    with open(path, "wb") as f:
        f.write(sig)
        f.write(chunk(b"IHDR", ihdr))
        if plte:
            f.write(chunk(b"PLTE", plte))
            f.write(chunk(b"tRNS", trns))
        f.write(chunk(b"IDAT", data))
        f.write(chunk(b"IEND", b""))


# (name, width, height, bitdepth, pixel type, palette entries)
IMAGES = [
    ("small_gray",      13,   9, 8, GRAYSCALE,          0),
    ("small_idx2",      17,  11, 2, INDEXED,            4),
    ("small_idx4",      15,   7, 4, INDEXED,            16),
    ("big_truecolor", 2800, 120, 8, TRUECOLOR,          0),
    ("alpha_rgba",      31,  23, 8, TRUECOLOR_ALPHA,    0),
    ("alpha_idx8",      19,  17, 8, INDEXED,          256),
    ("alpha_ga",        23,  13, 8, GRAY_ALPHA,         0),
]


def main():
    os.makedirs(OUT, exist_ok=True)
    specs = []
    for (name, w, h, bd, ptype, palsize) in IMAGES:
        pixels = build_image(w, h, bd, ptype, False, palsize)
        ref, pitch = reference_bytes(pixels, w, h, bd, ptype)
        for interlaced in (False, True):
            tag = name + ("_i7" if interlaced else "")
            png_path = os.path.join(OUT, tag + ".png")
            write_png(png_path, w, h, bd, ptype, interlaced, palsize, pixels)
            ref_path = os.path.join(OUT, tag + ".ref")
            with open(ref_path, "wb") as f:
                f.write(ref)
            specs.append({
                "name": tag, "file": tag + ".png", "ref": tag + ".ref",
                "width": w, "height": h, "bitdepth": bd, "ptype": ptype,
                "interlaced": interlaced, "pitch": pitch,
                "palette_size": palsize,
                "image_size": pitch * h,
            })
    # extra cases exercising all five PNG filter types (mixed per scanline);
    # one interlaced (Adam7 + filters) and one progressive, both 8-bit RGBA
    for tag, w, h, bd, ptype, interlaced in (
            ("filtered_rgba_i7", 29, 21, 8, TRUECOLOR_ALPHA, True),
            ("filtered_rgba", 29, 21, 8, TRUECOLOR_ALPHA, False),
            ("filtered_idx4_i7", 17, 13, 4, INDEXED, True)):
        palsize = 16 if ptype == INDEXED else 0
        pixels = build_image(w, h, bd, ptype, False, palsize)
        ref, pitch = reference_bytes(pixels, w, h, bd, ptype)
        png_path = os.path.join(OUT, tag + ".png")
        write_png(png_path, w, h, bd, ptype, interlaced, palsize, pixels,
                  use_filters=True)
        with open(os.path.join(OUT, tag + ".ref"), "wb") as f:
            f.write(ref)
        specs.append({
            "name": tag, "file": tag + ".png", "ref": tag + ".ref",
            "width": w, "height": h, "bitdepth": bd, "ptype": ptype,
            "interlaced": interlaced, "pitch": pitch,
            "palette_size": palsize, "image_size": pitch * h,
        })
    with open(os.path.join(OUT, "specs.json"), "w") as f:
        json.dump(specs, f, indent=1)
    # line-oriented copy for simple, dependency-free parsing in the C++ harness
    with open(os.path.join(OUT, "specs.tsv"), "w") as f:
        f.write("name\tfile\tref\twidth\theight\tbitdepth\tptype\t"
                "interlaced\tpitch\tpalette_size\timage_size\n")
        for s in specs:
            row = dict(s)
            row["interlaced"] = 1 if s["interlaced"] else 0
            f.write("\t".join(str(row[k]) for k in (
                "name", "file", "ref", "width", "height", "bitdepth",
                "ptype", "interlaced", "pitch", "palette_size",
                "image_size")) + "\n")
    print("generated %d png+ref pairs in %s" % (len(specs), OUT))


if __name__ == "__main__":
    sys.exit(main())
