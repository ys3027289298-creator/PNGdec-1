# External (caller-supplied) buffer mode for PNGdec

Embedded targets often have no heap. PNGdec already keeps its zlib state inside
the `PNGIMAGE` struct, but the default decode path still relies on the fixed
internal line/work buffers and rejects images wider than those buffers and all
Adam7-interlaced images.

This change adds an **external buffer mode** in which the caller supplies every
working buffer. The decoder then performs no internal allocation at all, can
decode arbitrarily wide images, and gains Adam7 interlace support. Only
`src/png.inl` changes; no public function signature changes and no new
third-party dependency is introduced.

## How it works

Pass a `PNGEXTERNALBUFFERS` descriptor as the `pUser` argument of `decode()`,
with `u32Magic == PNG_EXT_BUFFER_MAGIC`. The descriptor holds two caller
buffers:

| buffer      | contents carved out by the decoder                          |
|-------------|-------------------------------------------------------------|
| line buffer | two 16-byte-aligned de-filter lines + 512 B fast palette    |
| workspace   | inflate state + 32 KiB window + 1 KiB palette + 2 KiB chunk read buffer + 128 B comment |

The minimum sizes for the currently open image are returned by
`PNG_getExternalBufferSizes()` (declared in `png_extbuf.h` for the tests; the
implementation lives in `png.inl` and is linkable from both C and C++).

Interlaced images are assembled into the full-image buffer set with the
existing `setBuffer()`; line-by-line callback mode stays available for
progressive images.

Error reporting when buffers are missing or too small uses the existing
`PNG_MEM_ERROR` / `PNG_NO_BUFFER` codes, retrievable through `getLastError()`.

## What is tested (`run_tests.sh`, one command, no network)

`gen_images.py` (Python standard library only) generates 17 small deterministic
PNGs plus byte-exact reference output, covering four image classes — small,
large (wider than the internal buffer), alpha-bearing, and Adam7-interlaced —
including images that mix all five PNG filter types.

The C++ harness verifies, per image:

- minimum line/work/image buffer sizes from the query function;
- decode success in external mode (and the unchanged default-mode rejection of
  interlace/over-size where applicable);
- byte-for-byte equality with the reference pixels;
- pixel-identical output between default and external mode for every
  progressive image, and between interlaced/non-interlaced twins;
- **zero heap traffic** during decode (linker `--wrap` on malloc/calloc/realloc);
- `PNG_MEM_ERROR` / `PNG_NO_BUFFER` on short or missing buffers;
- the `PNG_FAST_PALETTE` option and the line-by-line draw callback.

A separate plain-C smoke binary (`c_smoke.c`) drives the C API using only
static storage. A separate comparison build against the original `png.inl`
proves default-mode output is byte-identical (see the review notes).

Run everything:

```sh
./extbuf_test/run_tests.sh
```

Expected tail: `checks: 316, failures: 0 / ALL TESTS PASSED`.
