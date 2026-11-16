#!/usr/bin/env python3
# Generate the four sample PNG classes used by the resource-limit tests.
# Uses only the Python standard library (zlib/struct). No network access.
#
# usage: make_samples.py <outdir>
import struct
import sys
import zlib


def chunk(tag, payload):
    return (struct.pack(">I", len(payload)) + tag + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xffffffff))


def ihdr(w, h, depth, ctype):
    return chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, 0))


def png(w, h, depth, ctype, raw, idat_split=1, level=9, extra_lines=b""):
    """Build a PNG. `raw` is the uncompressed image data (filter byte per
    line included). extra_lines appends additional inflated data beyond
    what the declared dimensions imply (dishonest stream)."""
    data = zlib.compressobj(level)
    body = data.compress(raw + extra_lines) + data.flush()
    out = b"\x89PNG\r\n\x1a\n" + ihdr(w, h, depth, ctype)
    if idat_split <= 1:
        out += chunk(b"IDAT", body)
    else:
        n = (len(body) + idat_split - 1) // idat_split
        for i in range(0, len(body), n):
            out += chunk(b"IDAT", body[i:i + n])
    return out + chunk(b"IEND", b"")


def main(outdir):
    # 1. normal.png - 64x48 8-bit truecolor gradient, nothing suspicious
    w, h = 64, 48
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            row += bytes(((x * 4) & 0xff, (y * 5) & 0xff, ((x + y) * 3) & 0xff))
        rows.append(b"\x00" + bytes(row))
    raw = b"".join(rows)
    open(outdir + "/normal.png", "wb").write(png(w, h, 8, 2, raw, level=6))

    # 2. huge_dims.png - valid IHDR declaring 4096x100000 (409.6M pixels),
    #    far beyond the default 16M pixel limit; rejected at open time
    open(outdir + "/huge_dims.png", "wb").write(
        b"\x89PNG\r\n\x1a\n" + ihdr(4096, 100000, 8, 0)
        + chunk(b"IDAT", zlib.compress(b"\x00" + b"\x00" * 4096))
        + chunk(b"IEND", b""))

    # 3. ratio_bomb.png - 512x512 8-bit grayscale of zeros in a single IDAT.
    #    Dimensions/memory pass the default limits, but the chunk inflates
    #    ~500x its compressed size; rejected by the per-chunk ratio check
    raw = b"".join(b"\x00" + b"\x00" * 512 for _ in range(512))
    open(outdir + "/ratio_bomb.png", "wb").write(png(512, 512, 8, 0, raw))

    # 4. chunk_accum.png - declares 1024x1024 8-bit grayscale (passes the
    #    open-time estimates), but its 32 IDAT chunks each look innocent
    #    (normal ratio) while cumulatively inflating 50% MORE lines than
    #    the header declares; rejected by the decode-time memory re-check
    w, h = 1024, 1024
    rows = []
    for y in range(h + h // 2):  # 1.5x the declared number of lines
        row = bytes(((x % 16) ^ (y & 0xff)) & 0xff for x in range(w))
        rows.append(b"\x00" + row)
    raw = b"".join(rows)
    open(outdir + "/chunk_accum.png", "wb").write(
        png(w, h, 8, 0, raw, idat_split=32, level=6))

    for name in ("normal", "huge_dims", "ratio_bomb", "chunk_accum"):
        import os
        print("%-14s %8d bytes" % (name + ".png",
              os.path.getsize(outdir + "/" + name + ".png")))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
