#!/usr/bin/env python3
#
# Generates small APNG/PNG test samples for the PNGdec APNG test suite.
# Uses only the Python standard library (struct + zlib).
# Also computes the expected composited canvas for every frame of the
# 5-frame animation (mirroring the decoder's dispose/blend rules) so the
# C test output can be diffed against it.
#
import struct, zlib, os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "samples")
os.makedirs(OUT, exist_ok=True)

def chunk(typ, data):
    c = struct.pack(">I", len(data)) + typ + data
    return c + struct.pack(">I", zlib.crc32(typ + data) & 0xffffffff)

SIG = b"\x89PNG\r\n\x1a\n"

def ihdr(w, h, depth, ctype):
    return chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, 0))

def actl(num_frames, num_plays):
    return chunk(b"acTL", struct.pack(">II", num_frames, num_plays))

def fctl(seq, w, h, x, y, dnum, dden, dispose, blend):
    return chunk(b"fcTL", struct.pack(">IIIIIHHBB", seq, w, h, x, y, dnum, dden, dispose, blend))

def fdat(seq, zdata):
    return chunk(b"fdAT", struct.pack(">I", seq) + zdata)

def idat(zdata):
    return chunk(b"IDAT", zdata)

def raw_lines(pixels, w, h):
    # pixels: list of rows, each row a list of pixel tuples; filter type 0
    out = b""
    for y in range(h):
        out += b"\x00" + b"".join(bytes(px) for px in pixels[y])
    return out

# ---------------------------------------------------------------- 5-frame APNG
W = H = 8
# Frame definitions: (rect(x,y,w,h), delay_num, delay_den, dispose, blend, pixel_func)
FRAMES = [
    ((0, 0, 8, 8), 1, 10,  0, 0, lambda x, y: (x * 32, y * 32, 128, 255)),   # F0 SOURCE, NONE
    ((2, 2, 4, 4), 1, 5,   0, 1, lambda x, y: (0, 255, 0, 128)),             # F1 OVER,   NONE
    ((1, 1, 3, 3), 1, 20,  1, 0, lambda x, y: (0, 0, 255, 255)),             # F2 SOURCE, BACKGROUND
    ((4, 4, 2, 2), 3, 100, 2, 1, lambda x, y: (255, 255, 255, 64)),          # F3 OVER,   PREVIOUS
    ((0, 0, 4, 4), 1, 2,   0, 0, lambda x, y: ((x * 60 + y * 7) % 256,) * 3 + (255,)),  # F4 SOURCE, NONE
]

def frame_pixels(fn, w, h):
    return [[fn(x, y) for x in range(w)] for y in range(h)]

def build_apng(frames, mutate=None):
    seq = 0
    data = SIG + ihdr(W, H, 8, 6) + actl(len(frames), 0)
    for i, (rect, dn, dd, disp, blend, fn) in enumerate(frames):
        x, y, w, h = rect
        if mutate:
            (x, y, w, h), dn, dd, disp, blend = mutate(i, (x, y, w, h), dn, dd, disp, blend)
        data += fctl(seq, w, h, x, y, dn, dd, disp, blend); seq += 1
        z = zlib.compress(raw_lines(frame_pixels(fn, w, h), w, h))
        if i == 0:
            data += idat(z)
        else:
            data += fdat(seq, z); seq += 1
    data += chunk(b"IEND", b"")
    return data

apng5 = build_apng(FRAMES)
open(os.path.join(OUT, "apng5.png"), "wb").write(apng5)

# ---------------------------------------------------------------- expected output
def blend_px(dst, src):
    sa = src[3]
    if sa == 255:
        return src
    if sa == 0:
        return dst
    return tuple((src[c] * sa + dst[c] * (255 - sa)) // 255 for c in range(3)) + \
           (sa + dst[3] * (255 - sa) // 255,)

canvas = [[(0, 0, 0, 0)] * W for _ in range(H)]
pend = None  # (dispose, x, y, w, h, saved_rect)
expected = []
for i, (rect, dn, dd, disp, blend, fn) in enumerate(FRAMES):
    x, y, w, h = rect
    # apply previous frame's dispose op first (decoder does this at frame start)
    if pend is not None:
        pd, px, py, pw, ph, saved = pend
        if pd == 1:
            for ry in range(ph):
                for rx in range(pw):
                    canvas[py + ry][px + rx] = (0, 0, 0, 0)
        elif pd == 2:
            for ry in range(ph):
                for rx in range(pw):
                    canvas[py + ry][px + rx] = saved[ry][rx]
        pend = None
    saved = [[canvas[y + ry][x + rx] for rx in range(w)] for ry in range(h)]
    fpx = frame_pixels(fn, w, h)
    for ry in range(h):
        for rx in range(w):
            if blend == 1:
                canvas[y + ry][x + rx] = blend_px(canvas[y + ry][x + rx], fpx[ry][rx])
            else:
                canvas[y + ry][x + rx] = fpx[ry][rx]
    delay_ms = (dn * 1000) // (dd if dd else 100)
    expected.append((delay_ms, [row[:] for row in canvas]))
    pend = (disp, x, y, w, h, saved)

with open(os.path.join(OUT, "expected_frames.txt"), "w") as f:
    for i, (delay, cnv) in enumerate(expected):
        f.write("frame %d delay %d\n" % (i, delay))
        for row in cnv:
            f.write(" ".join("%02x%02x%02x%02x" % px for px in row) + "\n")

# ---------------------------------------------------------------- corrupt variants
# 1. truncated in the middle of F2's fdAT
open(os.path.join(OUT, "apng_trunc.png"), "wb").write(apng5[: len(apng5) * 3 // 5])

# 2. bit flip inside F1's fdAT zlib payload
b = bytearray(apng5)
pos = b.find(b"fdAT")
assert pos > 0
b[pos + 10] ^= 0xFF
open(os.path.join(OUT, "apng_bitflip.png"), "wb").write(bytes(b))

# 3. frame rectangle outside the canvas (F1: x=6, w=4 -> 6+4 > 8)
def mut_badrect(i, rect, dn, dd, disp, blend):
    x, y, w, h = rect
    if i == 1:
        return (6, y, w, h), dn, dd, disp, blend
    return rect, dn, dd, disp, blend
open(os.path.join(OUT, "apng_badrect.png"), "wb").write(build_apng(FRAMES, mut_badrect))

# 4. invalid dispose op (F2: dispose=9)
def mut_baddisp(i, rect, dn, dd, disp, blend):
    x, y, w, h = rect
    if i == 2:
        return (x, y, w, h), dn, dd, 9, blend
    return rect, dn, dd, disp, blend
open(os.path.join(OUT, "apng_baddispose.png"), "wb").write(build_apng(FRAMES, mut_baddisp))

# 5. zero-width frame (F1: w=0)
def mut_zerow(i, rect, dn, dd, disp, blend):
    x, y, w, h = rect
    if i == 1:
        return (x, y, 0, h), dn, dd, disp, blend
    return rect, dn, dd, disp, blend
open(os.path.join(OUT, "apng_zerow.png"), "wb").write(build_apng(FRAMES, mut_zerow))

# 6. F1's zlib stream holds 5 lines although fcTL says height=4
def build_extralines():
    seq = 0
    data = SIG + ihdr(W, H, 8, 6) + actl(len(FRAMES), 0)
    for i, (rect, dn, dd, disp, blend, fn) in enumerate(FRAMES):
        x, y, w, h = rect
        data += fctl(seq, w, h, x, y, dn, dd, disp, blend); seq += 1
        hh = h + 1 if i == 1 else h  # one line too many
        z = zlib.compress(raw_lines(frame_pixels(fn, w, hh), w, hh))
        if i == 0:
            data += idat(z)
        else:
            data += fdat(seq, z); seq += 1
    data += chunk(b"IEND", b"")
    return data
open(os.path.join(OUT, "apng_extralines.png"), "wb").write(build_extralines())

# 7. F1's zlib stream split across two fdAT chunks (must decode identically)
def build_multifdat():
    seq = 0
    data = SIG + ihdr(W, H, 8, 6) + actl(len(FRAMES), 0)
    for i, (rect, dn, dd, disp, blend, fn) in enumerate(FRAMES):
        x, y, w, h = rect
        data += fctl(seq, w, h, x, y, dn, dd, disp, blend); seq += 1
        z = zlib.compress(raw_lines(frame_pixels(fn, w, h), w, h))
        if i == 0:
            data += idat(z)
        elif i == 1:
            half = len(z) // 2
            data += fdat(seq, z[:half]); seq += 1
            data += fdat(seq, z[half:]); seq += 1
        else:
            data += fdat(seq, z); seq += 1
    data += chunk(b"IEND", b"")
    return data
open(os.path.join(OUT, "apng_multifdat.png"), "wb").write(build_multifdat())

# 8. default image (IDAT) is NOT part of the animation: first fcTL comes
#    after the IDAT; all 5 frames live in fdAT chunks. The composited
#    output must be identical to apng5.png.
def build_nodefault():
    seq = 0
    data = SIG + ihdr(W, H, 8, 6) + actl(len(FRAMES), 0)
    # default image: plain white canvas, not part of the animation
    data += idat(zlib.compress(raw_lines([[(255, 255, 255, 255)] * W for _ in range(H)], W, H)))
    for i, (rect, dn, dd, disp, blend, fn) in enumerate(FRAMES):
        x, y, w, h = rect
        data += fctl(seq, w, h, x, y, dn, dd, disp, blend); seq += 1
        z = zlib.compress(raw_lines(frame_pixels(fn, w, h), w, h))
        data += fdat(seq, z); seq += 1
    data += chunk(b"IEND", b"")
    return data
open(os.path.join(OUT, "apng_nodefault.png"), "wb").write(build_nodefault())

# ---------------------------------------------------------------- static PNGs
def static_png(name, w, h, depth, ctype, pixels, plte=None, trns=None):
    data = SIG + ihdr(w, h, depth, ctype)
    if plte is not None:
        data += chunk(b"PLTE", plte)
    if trns is not None:
        data += chunk(b"tRNS", trns)
    data += idat(zlib.compress(raw_lines(pixels, w, h)))
    data += chunk(b"IEND", b"")
    open(os.path.join(OUT, name), "wb").write(data)

# RGBA 8x8, identical pixels to APNG frame 0 (cross-check)
static_png("static_rgba.png", 8, 8, 8, 6, frame_pixels(FRAMES[0][5], 8, 8))
# RGB 7x5
static_png("static_rgb.png", 7, 5, 8, 2,
           [[(x * 30, y * 40, (x * 10 + y * 10) % 256) for x in range(7)] for y in range(5)])
# grayscale 8x3
static_png("static_gray.png", 8, 3, 8, 0,
           [[((x * 31 + y * 17) % 256,) for x in range(8)] for y in range(3)])
# indexed 6x6 with palette + transparency
pal = bytes([255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255])
trns = bytes([255, 128, 0, 255])
static_png("static_idx.png", 6, 6, 8, 3,
           [[((x + y) % 4,) for x in range(6)] for y in range(6)], plte=pal, trns=trns)

print("samples written to", OUT)
for f in sorted(os.listdir(OUT)):
    print("  %-24s %6d bytes" % (f, os.path.getsize(os.path.join(OUT, f))))
