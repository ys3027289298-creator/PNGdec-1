#!/usr/bin/env python3 -I
# Generate 4 test PNGs (gray8 / truecolor8 / indexed8 / rgba8), 1024x1024,
# plus .raw ground-truth pixel files (pitch*height, no filter bytes).
# Row 0 uses filter Sub (prev-independent); rows >= 1 cycle Sub/Up/Avg/Paeth
# to exercise every DeFilter path. Stdlib only.
import zlib, struct, os

W = H = 1024
OUT = os.path.dirname(os.path.abspath(__file__))

seed = 12345
def rnd():
    global seed
    seed = (seed * 1103515245 + 12345) & 0x7fffffff
    return (seed >> 16) & 0xff

def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc: return a
    if pb <= pc: return b
    return c

def filt_row(ftype, row, prev, bpp):
    out = bytearray(len(row))
    for x in range(len(row)):
        a = row[x - bpp] if x >= bpp else 0
        b = prev[x]
        c = prev[x - bpp] if x >= bpp else 0
        if ftype == 0:   p = 0
        elif ftype == 1: p = a
        elif ftype == 2: p = b
        elif ftype == 3: p = (a + b) // 2
        else:            p = paeth(a, b, c)
        out[x] = (row[x] - p) & 0xff
    return bytes(out)

def chunk(tag, data):
    c = struct.pack(">I", len(data)) + tag + data
    return c + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)

def write_png(name, ptype, rows, palette=None):
    # rows: list of raw pixel rows (bytes), len == H
    bpp_f = {0: 1, 2: 3, 3: 1, 6: 4}[ptype]
    stream = bytearray()
    prev = bytes(len(rows[0]))
    for y, row in enumerate(rows):
        ft = 1 if y == 0 else 1 + (y % 4)  # Sub, then cycle Sub/Up/Avg/Paeth
        stream.append(ft)
        stream += filt_row(ft, row, prev, bpp_f)
        prev = row
    comp = zlib.compress(bytes(stream), 6)
    png = bytearray(b"\x89PNG\r\n\x1a\n")
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, ptype, 0, 0, 0))
    if palette is not None:
        png += chunk(b"PLTE", palette)
    for i in range(0, len(comp), 65536):  # multiple IDATs
        png += chunk(b"IDAT", comp[i:i + 65536])
    png += chunk(b"IEND", b"")
    with open(os.path.join(OUT, name + ".png"), "wb") as f:
        f.write(png)
    with open(os.path.join(OUT, name + ".raw"), "wb") as f:
        f.write(b"".join(rows))
    print(f"{name}.png: {len(png)} bytes, raw {sum(len(r) for r in rows)} bytes")

# ---- grayscale 8bpp ----
rows = []
for y in range(H):
    row = bytearray(W)
    for x in range(W):
        base = ((x // 64) * 16 + (y // 64) * 16 + (x + y) // 8) & 0xff
        row[x] = (base + (rnd() & 15)) & 0xff
    rows.append(bytes(row))
write_png("gray8", 0, rows)

# ---- truecolor 8bpp ----
rows = []
for y in range(H):
    row = bytearray(W * 3)
    for x in range(W):
        i = x * 3
        row[i]     = ((x * 3 + y) // 4 + (rnd() & 7)) & 0xff
        row[i + 1] = ((x + y * 5) // 6 + (rnd() & 7)) & 0xff
        row[i + 2] = ((x // 32) * 32 + (rnd() & 15)) & 0xff
    rows.append(bytes(row))
write_png("truecolor8", 2, rows)

# ---- indexed 8bpp ----
palette = bytes(((i * 37) & 0xff, (i * 91) & 0xff, (i * 53) & 0xff)[c]
                for i in range(256) for c in range(3))
rows = []
for y in range(H):
    row = bytearray(W)
    for x in range(W):
        row[x] = (((x // 32) + (y // 32)) * 7 + (rnd() & 3)) & 0xff
    rows.append(bytes(row))
write_png("indexed8", 3, rows, palette)

# ---- truecolor + alpha 8bpp ----
rows = []
for y in range(H):
    row = bytearray(W * 4)
    for x in range(W):
        i = x * 4
        row[i]     = ((x + y * 3) // 5 + (rnd() & 7)) & 0xff
        row[i + 1] = ((x * 7 + y) // 9 + (rnd() & 7)) & 0xff
        row[i + 2] = ((y // 16) * 16 + (rnd() & 15)) & 0xff
        row[i + 3] = 255 if (x // 128 + y // 128) % 2 == 0 else ((x * y) & 0xff)
    rows.append(bytes(row))
write_png("rgba8", 6, rows)
print("done")
