#!/bin/bash
# Runs the RGBA8888 test suite and the repo's existing tests, all in one go.
# Nothing in the repo build system is modified; test binaries go to /tmp.
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
OUT=/tmp/pngdec_rgba_test
mkdir -p "$OUT"

CXXFLAGS="-D__LINUX__ -ggdb -Wall -O2"
ZLIB_SRC="src/adler32.c src/crc32.c src/infback.c src/inffast.c src/inflate.c src/inftrees.c src/zutil.c"

echo "==================================================================="
echo "1. build the new RGBA8888 test suite"
echo "==================================================================="
g++ $CXXFLAGS test/test_rgba8888.cpp src/PNGdec.cpp $ZLIB_SRC \
    examples/png_comparison/lodepng.cpp -I examples/png_comparison \
    -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
    -o "$OUT/test_rgba8888"

echo "==================================================================="
echo "2. functional tests: consistency / premultiply / row range / errors"
echo "==================================================================="
"$OUT/test_rgba8888"

echo "==================================================================="
echo "3. peak memory (VmHWM) per color type and decode mode, 512x512"
echo "==================================================================="
for ct in 0 4 3 2 6; do
    for mode in stream full crop native; do
        "$OUT/test_rgba8888" mem $ct $mode
    done
done

echo "==================================================================="
echo "4. existing repo build (linux/Makefile object targets, unmodified)"
echo "==================================================================="
make -C linux PNGdec.o adler32.o crc32.o infback.o inffast.o inflate.o inftrees.o zutil.o
make -C linux clean

echo "==================================================================="
echo "5. existing repo test: linux/main.cpp (png_demo)"
echo "==================================================================="
g++ $CXXFLAGS linux/main.cpp src/PNGdec.cpp $ZLIB_SRC -o "$OUT/png_demo"
for img in test_images/homer_2bpp.png perf_small.png; do
    echo "--- $img"
    "$OUT/png_demo" "$img" "$OUT/$(basename $img .png).bmp" || exit 1
    ls -l "$OUT/$(basename $img .png).bmp"
done

echo "==================================================================="
echo "6. existing repo test: linux/examples/png2bmp"
echo "==================================================================="
g++ $CXXFLAGS -I src linux/examples/png2bmp/main.cpp src/PNGdec.cpp $ZLIB_SRC -o "$OUT/png2bmp"
for img in test_images/homer_2bpp.png perf_small.png; do
    echo "--- $img"
    "$OUT/png2bmp" "$img" "$OUT/$(basename $img .png).2bmp.bmp" || exit 1
    ls -l "$OUT/$(basename $img .png).2bmp.bmp"
done

echo "==================================================================="
echo "ALL DONE"
echo "==================================================================="
