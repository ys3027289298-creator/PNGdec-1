#!/bin/bash
#
# One-shot APNG test run:
#   1. generate the sample images (python3, stdlib only)
#   2. build the test against the current (APNG-enabled) library, with sanitizers
#   3. build the same test against the pre-APNG library (git HEAD) for comparison
#   4. run both, diff static-PNG results (must be identical) and the
#      5-frame APNG output (must match the python-computed expectation)
#
set -e
cd "$(dirname "$0")"
REPO_ROOT=../..
OLD=/tmp/pngdec_before_apng

echo "=== 1. generating samples ==="
python3 gen_samples.py

echo "=== 2. building test (new library, ASan+UBSan) ==="
# (-fno-sanitize=alignment silences the zlib code's intentional unaligned word access)
g++ -D__LINUX__ -Wall -O1 -g -fsanitize=address,undefined -fno-sanitize=alignment -fno-omit-frame-pointer \
    -I$REPO_ROOT/src main.cpp $REPO_ROOT/src/PNGdec.cpp $REPO_ROOT/src/adler32.c \
    $REPO_ROOT/src/crc32.c $REPO_ROOT/src/infback.c $REPO_ROOT/src/inffast.c \
    $REPO_ROOT/src/inflate.c $REPO_ROOT/src/inftrees.c $REPO_ROOT/src/zutil.c -o test_new

echo "=== 3. building test (old library from git HEAD) ==="
mkdir -p $OLD
git -C $REPO_ROOT archive HEAD src | tar -x -C $OLD
g++ -D__LINUX__ -DSTATIC_ONLY -Wall -O1 -g -fsanitize=address,undefined -fno-sanitize=alignment -fno-omit-frame-pointer \
    -I$OLD/src main.cpp $OLD/src/PNGdec.cpp $OLD/src/adler32.c \
    $OLD/src/crc32.c $OLD/src/infback.c $OLD/src/inffast.c \
    $OLD/src/inflate.c $OLD/src/inftrees.c $OLD/src/zutil.c -o test_old

echo "=== 4. running tests ==="
./test_old samples $REPO_ROOT > out_old.txt 2>&1
./test_new samples $REPO_ROOT > out_new.txt 2>&1

echo "--- old (pre-APNG) results ---"
cat out_old.txt
echo "--- new (with APNG) results ---"
cat out_new.txt

echo "=== 5. static PNG regression: old vs new (must be identical) ==="
if diff <(grep -E '^(STATIC|PERF)' out_old.txt | grep -v per_decode) \
        <(grep -E '^(STATIC|PERF)' out_new.txt | grep -v per_decode); then
    echo "STATIC REGRESSION: PASS (pixel-identical output)"
else
    echo "STATIC REGRESSION: FAIL"
    exit 1
fi

echo "=== 6. APNG 5-frame output vs expected ==="
if diff samples/expected_frames.txt apng_out.txt; then
    echo "APNG FRAMES: PASS"
else
    echo "APNG FRAMES: FAIL"
    exit 1
fi
for v in multifdat nodefault; do
    if diff -q samples/expected_frames.txt apng_out_$v.txt > /dev/null; then
        echo "APNG VARIANT $v: PASS"
    else
        echo "APNG VARIANT $v: FAIL"
        exit 1
    fi
done

echo "=== 7. corrupt-file handling ==="
grep ^CORRUPT out_new.txt
if grep -q "FAIL" <(grep ^CORRUPT out_new.txt); then
    echo "CORRUPT HANDLING: FAIL"
    exit 1
else
    echo "CORRUPT HANDLING: PASS (all rejected with errors, no sanitizer reports)"
fi
echo "ALL TESTS DONE"
