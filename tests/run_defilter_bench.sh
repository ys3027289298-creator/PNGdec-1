#!/bin/sh
# Build and run the DeFilter SIMD correctness + performance test in one go.
# Usage: tests/run_defilter_bench.sh [output_dir_for_sample_pngs]
# No changes to the repo build files are needed; this compiles the test
# directly against the library sources.
set -e
cd "$(dirname "$0")/.."
OUT="${1:-/tmp/defilter_samples}"
mkdir -p "$OUT"
g++ -O2 -Wall -D__LINUX__ -I src tests/defilter_bench.cpp \
    src/adler32.c src/crc32.c src/inflate.c src/inffast.c \
    src/inftrees.c src/infback.c src/zutil.c -o /tmp/defilter_bench
/tmp/defilter_bench "$OUT"
