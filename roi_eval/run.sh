#!/usr/bin/env bash
#
# One-shot build+run for the ROI tests. The repository's own Makefiles
# and build scripts are untouched; this only compiles a standalone driver
# in roi_eval/.
#
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
GEN="$HERE/gen"
BUILD="$HERE/build"
PNGDIR="$HERE/pngs"
mkdir -p "$BUILD" "$PNGDIR"

# -I : isolated Python mode (ignore cwd/script-dir injected modules)
python3 -I "$GEN/gen_pngs.py" "$PNGDIR"

g++ -Wall -Wextra -O2 -g \
    -fsanitize=address,undefined \
    "$HERE/roi_test.cpp" -o "$BUILD/roi_test" -lm

"$BUILD/roi_test" "$PNGDIR"
