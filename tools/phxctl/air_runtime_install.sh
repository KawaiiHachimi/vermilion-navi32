#!/bin/sh
# SPDX-License-Identifier: MIT
# air_runtime_install.sh <src dir> [root]: install the plugin's in-memory AIR bitcode reader, <src>/libLLVM.dylib and
# <src>/libzstd.1.dylib (+ their license files, if present), into
# <root>/Library/GPUBundles/NVMTLDriver.bundle/Contents/MacOS/air-runtime (licenses in Contents/Resources/air-runtime).
# NVMTLDriver dlopens air-runtime/libLLVM.dylib next to itself (src/metal-plugin/NVMTLLibraryLoad.m). Without it,
# and without air-opt, it cannot read any metallib: WindowServer's Metal compositor gets no shaders from QuartzCore's
# default.metallib and aborts (XTX machine, 2026-11-19). Vermilion does not ship this LLVM build; the reference
# machine had it inside NullMoth's bundle (docs/INSTALL.md 5.2). <src>/MD5SUMS (md5 path), when present, is checked
# first. Exit 0 only when both libraries are in place.
set -eu
S=${1:?usage: air_runtime_install.sh <src dir> [root]}; R=${2:-}
B=$R/Library/GPUBundles/NVMTLDriver.bundle/Contents
[ -f "$S/libLLVM.dylib" ] && [ -f "$S/libzstd.1.dylib" ] || { echo "air-runtime: $S lacks libLLVM.dylib or libzstd.1.dylib"; exit 1; }
[ -d "$B/MacOS" ] || { echo "air-runtime: $B/MacOS missing (install the plugin first)"; exit 1; }
md5of() { if command -v md5 >/dev/null 2>&1; then md5 -q "$1"; else md5sum "$1" | cut -d' ' -f1; fi; }
if [ -f "$S/MD5SUMS" ]; then
  while read -r sum f; do [ -n "$f" ] || continue
    [ "$(md5of "$S/$f")" = "$sum" ] || { echo "air-runtime: $f does not match MD5SUMS: not installed"; exit 1; }
  done <"$S/MD5SUMS"
fi
mkdir -p "$B/MacOS/air-runtime" "$B/Resources/air-runtime"
for f in libLLVM.dylib libzstd.1.dylib; do
  cp "$S/$f" "$B/MacOS/air-runtime/.$f.new"; chmod 755 "$B/MacOS/air-runtime/.$f.new"
  [ "${VERMILION_NOCHOWN:-0}" = 1 ] || chown 0:0 "$B/MacOS/air-runtime/.$f.new"
  mv "$B/MacOS/air-runtime/.$f.new" "$B/MacOS/air-runtime/$f"
done
for f in "$S"/*LICENSE* "$S"/*COPYING*; do [ -f "$f" ] && cp "$f" "$B/Resources/air-runtime/" && chmod 644 "$B/Resources/air-runtime/${f##*/}"; done
[ "${VERMILION_NOCHOWN:-0}" = 1 ] || chown -R 0:0 "$B/MacOS/air-runtime" "$B/Resources/air-runtime"
chmod 755 "$B/MacOS/air-runtime" "$B/Resources/air-runtime"
echo "air-runtime: installed $(ls "$B/MacOS/air-runtime" | tr '\n' ' ')into $B/MacOS/air-runtime"
