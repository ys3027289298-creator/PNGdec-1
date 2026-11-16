#!/bin/bash
# run_tests.sh - one-shot test run for the PNGdec resource-limit feature
#
# 1. Runs the repo's existing test (linux/ build + decode of the repo's
#    test images) against the MODIFIED library.
# 2. Builds the PRISTINE library from git HEAD and verifies the modified
#    library decodes normal files byte-for-byte identically.
# 3. Exercises the four sample classes plus invalid limit parameters.
#
# Nothing here touches the network; samples are generated locally.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=/tmp/pnglim
SAMPLES=$T/samples
rm -rf $T
mkdir -p $SAMPLES $T/mod $T/base
PASS=0; FAIL=0

check() { # <desc> <actual> <expected>
    if [ "$2" = "$3" ]; then
        echo "  PASS: $1"
        PASS=$((PASS+1))
    else
        echo "  FAIL: $1 (got '$2', want '$3')"
        FAIL=$((FAIL+1))
    fi
}

field() { # <line> <key> - extract key=value or reason="..." from a REPORT line
    local line="$1" key="$2"
    if [ "$key" = reason ]; then
        echo "$line" | sed -n 's/.*reason="\([^"]*\)".*/\1/p'
    else
        echo "$line" | sed -n "s/.*$key=\([0-9a-zA-Z_]*\).*/\1/p"
    fi
}

build_lib() { # <srcdir> <outdir>
    for f in adler32 crc32 infback inffast inflate inftrees zutil; do
        gcc -D__LINUX__ -Wall -O2 -c "$1/$f.c" -o "$2/$f.o" || return 1
    done
    g++ -D__LINUX__ -Wall -O2 -c "$1/PNGdec.cpp" -o "$2/PNGdec.o" || return 1
}

echo "================================================================"
echo " 0. Repo existing test: linux/ build (unmodified build script)"
echo "================================================================"
(cd "$ROOT/linux" && make 2>&1 | tail -4)
if [ -f "$ROOT/linux/libpngdec.a" ]; then
    echo "  library archive built: OK (install step needs sudo; skipped, linking directly)"
    g++ -D__LINUX__ -Wall -O2 -c "$ROOT/linux/main.cpp" -o $T/repo_main.o \
        && g++ $T/repo_main.o "$ROOT/linux/libpngdec.a" -o $T/png_demo \
        && echo "  repo test program linked: OK"
else
    echo "  FAIL: library did not build"; FAIL=$((FAIL+1))
fi
echo "-- repo test: decode test_images/homer_2bpp.png --"
(cd $T && ./png_demo "$ROOT/test_images/homer_2bpp.png" $T/homer.bmp)
echo "-- repo test: decode perf_small.png --"
(cd $T && ./png_demo "$ROOT/perf_small.png" $T/perf_small.bmp)
check "repo test: homer_2bpp.png decodes" "$?" "0"

echo "================================================================"
echo " 1. Build modified + pristine (git HEAD) libraries and harness"
echo "================================================================"
build_lib "$ROOT/src" $T/mod || { echo "modified build failed"; exit 1; }
git -C "$ROOT" worktree add --detach $T/base_src HEAD >/dev/null 2>&1 \
    || { echo "git worktree failed"; exit 1; }
mkdir -p $T/base
build_lib "$T/base_src/src" $T/base || { echo "baseline build failed"; exit 1; }
g++ -D__LINUX__ -Wall -O2 -I"$ROOT/src" "$ROOT/tests/limit_test.cpp" $T/mod/*.o -o $T/limit_test_mod \
    || { echo "harness (mod) build failed"; exit 1; }
g++ -D__LINUX__ -Wall -O2 -I"$T/base_src/src" "$ROOT/tests/limit_test.cpp" $T/base/*.o -o $T/limit_test_base \
    || { echo "harness (base) build failed"; exit 1; }
echo "  builds OK"

echo "================================================================"
echo " 2. Generate samples (local, tiny)"
echo "================================================================"
python3 -I "$ROOT/tests/make_samples.py" $SAMPLES

echo "================================================================"
echo " 3. Class 1: normal file - decodes, pixels byte-identical"
echo "================================================================"
R=$( $T/limit_test_mod  $SAMPLES/normal.png $T/normal_mod.raw      | grep RESULT)
echo "  modified, limits on : $R"
check "normal: decode succeeds (limits on)" "$(field "$R" err)" "0"
R=$(PNG_MAX_PIXELS=0 PNG_MAX_MEM=0 PNG_MAX_RATIO=0 $T/limit_test_mod $SAMPLES/normal.png $T/normal_mod0.raw | grep RESULT)
echo "  modified, limits off: $R"
check "normal: decode succeeds (limits off)" "$(field "$R" err)" "0"
R=$( $T/limit_test_base $SAMPLES/normal.png $T/normal_base.raw     | grep RESULT)
echo "  pristine HEAD       : $R"
cmp -s $T/normal_mod.raw $T/normal_base.raw
check "normal: pixels identical to pristine library" "$?" "0"
cmp -s $T/normal_mod.raw $T/normal_mod0.raw
check "normal: pixels identical with limits on/off" "$?" "0"
# same check on a repo test image (2bpp palette, real content)
$T/limit_test_mod  "$ROOT/test_images/homer_2bpp.png" $T/homer_mod.raw  >/dev/null
$T/limit_test_base "$ROOT/test_images/homer_2bpp.png" $T/homer_base.raw >/dev/null
cmp -s $T/homer_mod.raw $T/homer_base.raw
check "repo image homer_2bpp.png: pixels identical to pristine" "$?" "0"

echo "================================================================"
echo " 4. Class 2: oversized dimensions - rejected at open"
echo "================================================================"
R=$( $T/limit_test_mod $SAMPLES/huge_dims.png | grep RESULT)
echo "  $R"
check "huge_dims: rejected" "$(field "$R" err)" "7"
check "huge_dims: stage" "$(field "$R" stage)" "open"
check "huge_dims: reason" "$(field "$R" reason)" "LIMIT: pixels 409600000 > max 16777216"

echo "================================================================"
echo " 5. Class 3: high-ratio bomb - rejected mid-decode (ratio)"
echo "================================================================"
R=$(PNG_MAX_RATIO=100 $T/limit_test_mod $SAMPLES/ratio_bomb.png | grep RESULT)
echo "  $R"
check "ratio_bomb: rejected" "$(field "$R" err)" "7"
check "ratio_bomb: stage" "$(field "$R" stage)" "decode"
echo "$R" | grep -q 'reason="LIMIT: IDAT chunk inflate ratio [0-9]* > max 100"'
check "ratio_bomb: reason mentions chunk ratio" "$?" "0"
# sanity: with the default ratio limit this file is below the default and
# its dimensions pass, so default limits let it through (configurability)
R=$( $T/limit_test_mod $SAMPLES/ratio_bomb.png | grep RESULT)
check "ratio_bomb: default ratio limit accepts it" "$(field "$R" err)" "0"

echo "================================================================"
echo " 6. Class 4: chunk-accumulated overflow - rejected mid-decode (mem)"
echo "================================================================"
# the file declares 1024x1024 but its 32 IDAT chunks inflate 1.5x that;
# first learn the open-time estimate, then set the memory limit to exactly
# that value so only the decode-time re-check can catch the excess
EST=$( $T/limit_test_mod $SAMPLES/chunk_accum.png 2>/dev/null | sed -n 's/.*est_mem=\([0-9]*\).*/\1/p')
echo "  open-time estimate: $EST bytes"
R=$(PNG_MAX_MEM=$EST $T/limit_test_mod $SAMPLES/chunk_accum.png | grep RESULT)
echo "  $R"
check "chunk_accum: rejected" "$(field "$R" err)" "3"
check "chunk_accum: stage" "$(field "$R" stage)" "decode"
echo "$R" | grep -q 'reason="LIMIT: inflated memory [0-9]* > max [0-9]*"'
check "chunk_accum: reason mentions inflated memory" "$?" "0"
# same file with the memory check disabled: the declared-size hard cap fires
R=$(PNG_MAX_MEM=0 $T/limit_test_mod $SAMPLES/chunk_accum.png | grep RESULT)
echo "  $R"
check "chunk_accum (mem check off): declared-size cap rejects" "$(field "$R" err)" "2"
echo "$R" | grep -q 'reason="LIMIT: inflated data exceeds declared image size'
check "chunk_accum (mem check off): reason" "$?" "0"

echo "================================================================"
echo " 7. Invalid limit parameters"
echo "================================================================"
R=$(PNG_MAX_PIXELS=abc $T/limit_test_mod $SAMPLES/normal.png | grep RESULT)
echo "  $R"
check "PNG_MAX_PIXELS=abc: invalid parameter" "$(field "$R" err)" "1"
check "PNG_MAX_PIXELS=abc: reason" "$(field "$R" reason)" "LIMIT: invalid parameter PNG_MAX_PIXELS"
R=$(PNG_MAX_MEM=-5 $T/limit_test_mod $SAMPLES/normal.png | grep RESULT)
check "PNG_MAX_MEM=-5: invalid parameter" "$(field "$R" err)" "1"
R=$(PNG_MAX_RATIO=10x $T/limit_test_mod $SAMPLES/normal.png | grep RESULT)
check "PNG_MAX_RATIO=10x: invalid parameter" "$(field "$R" err)" "1"

echo "================================================================"
echo " 8. Repo test images under default limits"
echo "================================================================"
for img in "$ROOT/test_images/homer_2bpp.png" "$ROOT/perf_small.png"; do
    R=$( $T/limit_test_mod "$img" | grep RESULT)
    echo "  $R"
    check "$(basename $img): decodes under default limits" "$(field "$R" err)" "0"
done

git -C "$ROOT" worktree remove --force $T/base_src >/dev/null 2>&1

echo "================================================================"
echo " SUMMARY: $PASS passed, $FAIL failed"
echo "================================================================"
[ $FAIL -eq 0 ]
