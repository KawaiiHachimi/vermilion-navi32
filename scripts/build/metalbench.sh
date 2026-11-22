#!/bin/sh
# SPDX-License-Identifier: MIT
# metalbench.sh: the Metal load used for measurements (tools/metaltest/metalbench.m) -> $VERMILION_OUT/metalbench.
# x86_64, deployment target 26.0, Foundation + Metal.
# Not signed, like metaltest.
. "$(dirname "$0")/lib.sh"
xcrun clang -arch x86_64 -mmacosx-version-min=26.0 -fobjc-arc -O2 $PFX -o "$OUT/metalbench" "$ROOT/tools/metaltest/metalbench.m" \
  -framework Foundation -framework Metal
homecheck "$OUT/metalbench"; echo "built $OUT/metalbench"
