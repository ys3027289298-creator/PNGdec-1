#!/usr/bin/env bash
# Build the unmodified (baseline) and modified library, run the full
# harness against both, and byte-compare the 8-bit regression dumps.
# No third-party tools, no network: g++/gcc only.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
T="$ROOT/test_16bit"
BASE="$T/baseline"
MOD="$T/modified"
mkdir -p "$BASE" "$MOD" "$T/dump_base" "$T/dump_mod"

ZLIB_SRC="adler32 crc32 infback inffast inflate inftrees zutil"
CFLAGS="-D__LINUX__ -ggdb -Wall -Wextra -O2"

build_variant() {
    local variant="$1"   # baseline | modified
    local out="$T/$variant"
    local pnginl
    if [ "$variant" = "baseline" ]; then
        pnginl="$T/png_baseline.inl"
    else
        pnginl="$ROOT/src/png.inl"
    fi
    mkdir -p "$out/inc"
    cp "$ROOT/src/PNGdec.h" "$ROOT/src/zutil.h" "$ROOT/src/inftrees.h" \
       "$ROOT/src/inflate.h" "$ROOT/src/zconf.h" "$ROOT/src/zlib.h" \
       "$out/inc/" 2>/dev/null || true
    # Compile the zlib C units (identical in both variants).
    for u in $ZLIB_SRC; do
        gcc $CFLAGS -c "$ROOT/src/$u.c" -o "$out/$u.o"
    done
    # Compile PNGdec.cpp with a chosen png.inl via an isolated include dir.
    mkdir -p "$out/shim"
    cp "$ROOT/src/PNGdec.cpp" "$out/shim/PNGdec.cpp"
    cp "$pnginl" "$out/shim/png.inl"
    # PNGdec.cpp includes "PNGdec.h" and "png.inl"; point the include path
    # at shim first so our swapped png.inl is picked up, headers from inc.
    g++ $CFLAGS -I"$out/shim" -I"$out/inc" -c "$out/shim/PNGdec.cpp" \
        -o "$out/PNGdec.o"
    ar -rc "$out/libpngdec.a" "$out"/PNGdec.o $(for u in $ZLIB_SRC; do echo "$out/$u.o"; done)
}

# Recover the pristine png.inl from git for the baseline build.
git -C "$ROOT" show HEAD:src/png.inl > "$T/png_baseline.inl"

echo "== Building baseline (unmodified library) =="
build_variant baseline
echo "== Building modified library =="
build_variant modified

echo
echo "== Compiling harness against each variant =="
g++ $CFLAGS -I"$BASE/inc" "$T/test_harness.cpp" "$T/baseline_stub.cpp" \
    "$BASE/libpngdec.a" -o "$T/harness_base"
g++ $CFLAGS -I"$MOD/inc" "$T/test_harness.cpp" "$MOD/libpngdec.a" \
    -o "$T/harness_mod"

echo
echo "== Running baseline harness (16-bit must be rejected) =="
set +e
"$T/harness_base" "$T/data" "$T/dump_base" baseline
RC_BASE=$?
set -e

echo
echo "== Running modified harness (full 16-bit path) =="
set +e
"$T/harness_mod" "$T/data" "$T/dump_mod" modified
RC_MOD=$?
set -e

echo
echo "== Byte-for-byte 8-bit regression comparison =="
DIFF_OK=1
for f in "$T"/dump_mod/*.buf; do
    name="$(basename "$f")"
    if cmp -s "$f" "$T/dump_base/$name"; then
        echo "  identical: $name"
    else
        echo "  DIFFERS:   $name"
        DIFF_OK=0
    fi
done

echo
if [ "$RC_BASE" -ne 0 ] || [ "$RC_MOD" -ne 0 ] || [ "$DIFF_OK" -ne 1 ]; then
    echo "RESULT: FAILURE (baseline rc=$RC_BASE, modified rc=$RC_MOD, dumps identical=$DIFF_OK)"
    exit 1
fi
echo "RESULT: SUCCESS (baseline rejects 16-bit as before; modified all green; 8-bit output unchanged)"
