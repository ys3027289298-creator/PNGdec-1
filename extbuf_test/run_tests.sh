#!/usr/bin/env bash
# One-shot build + test run for PNGdec external buffer mode.
# Uses only the toolchain already required by linux/Makefile; no new
# third-party code and no network access.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BUILD="$HERE/build"
rm -rf "$BUILD"
mkdir -p "$BUILD"
CFLAGS="-D__LINUX__ -ggdb -Wall -O2 -I$ROOT/src -I$HERE"
ZLIB_OBJS="adler32.o crc32.o infback.o inffast.o inflate.o inftrees.o zutil.o"

python3 -I "$HERE/gen_images.py" >/dev/null

# compile every library translation unit exactly as linux/Makefile does
for c in adler32 crc32 infback inffast inflate inftrees zutil; do
    gcc $CFLAGS -c "$ROOT/src/$c.c" -o "$BUILD/$c.o"
done
g++ $CFLAGS -c "$ROOT/src/PNGdec.cpp" -o "$BUILD/PNGdec.o"

# C++ harness: --wrap makes any in-decode heap allocation visible
g++ $CFLAGS -c "$HERE/test_extbuf.cpp" -o "$BUILD/test_extbuf.o"
g++ $(for o in $ZLIB_OBJS; do echo "$BUILD/$o"; done) \
    "$BUILD/PNGdec.o" "$BUILD/test_extbuf.o" \
    -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
    -o "$BUILD/test_extbuf"

# plain-C API smoke test. In the library's C build model the PNG_* functions
# are static and png.inl is included directly, so this validates the C path by
# compiling + linking a full binary in one translation unit (static buffers)
gcc $CFLAGS "$HERE/c_smoke.c" $(for o in $ZLIB_OBJS; do echo "$BUILD/$o"; done) \
    -o "$BUILD/c_smoke"

"$BUILD/test_extbuf" "$HERE/data"
echo
echo "--- C API smoke (small indexed interlaced image, static buffers) ---"
"$BUILD/c_smoke" "$HERE/data/small_idx2_i7.png"
echo
echo "--- C API smoke (RGBA non-interlaced) ---"
"$BUILD/c_smoke" "$HERE/data/alpha_rgba.png"
