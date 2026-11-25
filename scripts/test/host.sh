#!/bin/sh
# SPDX-License-Identifier: MIT
# host.sh: every test in this repo that runs without the card - the mock daemon harness and the kext C++ suites.
#
#   sh scripts/test/host.sh            # all of it
#   sh scripts/test/host.sh --kext     # the C++ suites only
#   sh scripts/test/host.sh --mock     # the daemon harness only
#
# The kext suites have no Makefile: each is one translation unit that includes a header from src/vermilion-boot (or
# src/vermilion-accel/accel), so they are compiled here with those two include paths. Without them every one of them
# fails on its first #include - that is the whole reason this script exists.
#
# Exit 0 only when every suite passes. Requires the Xcode command line tools; nothing else, no GPU.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CD=${CD:-cd}; CC=${CC:-xcrun clang++}; CXXFLAGS=${CXXFLAGS:--std=c++17 -O1 -Wall}
INC="-I$ROOT/src/vermilion-boot -I$ROOT/src/vermilion-accel/accel"
TMP=${TMPDIR:-/tmp}/vermilion-host.$$
mkdir -p "$TMP" || exit 1
trap 'rm -rf "$TMP"' EXIT
bad=0; ran=0
what=${1:-all}

if [ "$what" = all ] || [ "$what" = --mock ]; then
  printf '== tests/mock/bringup_warm_test.sh\n'
  if out=$(bash "$ROOT/tests/mock/bringup_warm_test.sh" 2>&1); then
    echo "$out" | tail -1 | sed 's/^/   /'
    ran=$((ran + 1))
  else
    echo "$out" | tail -12 | sed 's/^/   /'
    echo '   FAILED'; bad=$((bad + 1)); ran=$((ran + 1))
  fi
  printf '== tests/mock/run.sh\n'
  if out=$(sh "$ROOT/tests/mock/run.sh" 2>&1); then
    echo "$out" | tail -2 | sed 's/^/   /'
    ran=$((ran + 1))
  else
    echo "$out" | tail -12 | sed 's/^/   /'
    echo '   FAILED'; bad=$((bad + 1)); ran=$((ran + 1))
  fi
fi

if [ "$what" = all ] || [ "$what" = --kext ]; then
  for t in "$ROOT"/tests/kext/*_test.cpp; do
    [ -f "$t" ] || continue
    n=$(basename "$t" .cpp)
    ran=$((ran + 1))
    if ! $CC $CXXFLAGS $INC -o "$TMP/$n" "$t" 2>"$TMP/$n.err"; then
      printf '   %-22s BUILD-FAIL\n' "$n"; sed -n '1,4p' "$TMP/$n.err" | sed 's/^/      /'; bad=$((bad + 1)); continue
    fi
    if out=$("$TMP/$n" 2>&1); then printf '   %-22s %s\n' "$n" "$(echo "$out" | tail -1)"
    else printf '   %-22s %s\n' "$n" "$(echo "$out" | tail -1)"; bad=$((bad + 1)); fi
  done
fi

echo "host tests: $ran suite(s), $bad failing"
[ $bad = 0 ]