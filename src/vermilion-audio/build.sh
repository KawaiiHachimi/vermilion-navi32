#!/bin/sh
# SPDX-License-Identifier: MIT
# Builds VermilionAudio.kext natively on macOS with the Command Line Tools.
# Usage: ./build.sh <MacKernelSDK> <Lilu.kext from a DEBUG release (its Contents/Resources holds the SDK)>
set -eu
MKSDK=${1:?usage: build.sh <MacKernelSDK> <Lilu DEBUG kext>}
LILU=${2:?usage: build.sh <MacKernelSDK> <Lilu DEBUG kext>}/Contents/Resources
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
PFX="-ffile-prefix-map=$ROOT=/vermilion -fdebug-prefix-map=$ROOT=/vermilion -ffile-prefix-map=$HOME=/build -fdebug-prefix-map=$HOME=/build"
OUT="$HERE/build/VermilionAudio.kext"
VER=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$HERE/Info.plist")
[ -f "$LILU/Headers/kern_api.hpp" ] || { echo "no Lilu SDK in $LILU" >&2; exit 1; }
rm -rf "$HERE/build"
mkdir -p "$OUT/Contents/MacOS"

CFLAGS="-arch x86_64 -mmacosx-version-min=11.0 -mkernel -fno-builtin -fno-common -nostdinc
  -I$MKSDK/Headers -I$LILU -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT -Os -Wall $PFX
  -DPRODUCT_NAME=VermilionAudio -DLILU_KEXTPATCH_SUPPORT -DMODULE_VERSION=$VER"
CXXFLAGS="$CFLAGS -std=c++17 -fapple-kext -fno-rtti -fno-exceptions -Wno-unused-function"

clang -x c $CFLAGS -c "$HERE/kmod_info.c" -o "$HERE/build/kmod_info.o"
clang++ $CXXFLAGS -c "$LILU/Library/plugin_start.cpp" -o "$HERE/build/plugin_start.o"
clang++ $CXXFLAGS -c "$HERE/VermilionAudio.cpp" -o "$HERE/build/VermilionAudio.o"
clang++ -arch x86_64 -mmacosx-version-min=11.0 -nostdlib -Xlinker -kext \
  -L"$MKSDK/Library/x86_64" -lkmod -lcc_kext \
  "$HERE/build/kmod_info.o" "$HERE/build/plugin_start.o" "$HERE/build/VermilionAudio.o" \
  -o "$OUT/Contents/MacOS/VermilionAudio"
cp "$HERE/Info.plist" "$OUT/Contents/Info.plist"
echo "built $OUT ($VER)"
