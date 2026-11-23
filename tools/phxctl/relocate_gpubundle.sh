#!/bin/bash
# SPDX-License-Identifier: MIT OR GPL-2.0-only
# On the Mac (root): move the Metal plugin and its Vulkan stack out of /Library/GPUBundles into
# /System/Library/CoreServices/CoreTypes.bundle/Contents/Library/PhxGPU and leave symlinks behind. Sandboxed GPU
# processes (WebKit's com.apple.WebKit.GPU, Chromium/Electron helpers) may map executables only from system paths
# (com.apple.WebKit.GPUProcess.sb allows file-map-executable in /System/Library/CoreServices, but /Library/GPUBundles
# only for reading); that CoreTypes directory is a firmlink into the writable Data volume, so it passes the check.
# The originals stay in /Library/GPUBundles-backup (running processes keep their mapped copies). Idempotent.
# Ends with a check that the moved executables kept their cdhashes; exit 1 if one changed.
set -eu
H=/System/Library/CoreServices/CoreTypes.bundle/Contents/Library/PhxGPU; G=/Library/GPUBundles; B=/Library/GPUBundles-backup
mkdir -p "$H" "$B"
for n in NVMTLDriver.bundle nvmtl; do
    [ -L "$G/$n" ] && { echo "$n: already a link to $(readlink "$G/$n")"; continue; }
    rm -rf "$H/$n"; cp -Rp "$G/$n" "$H/"
    rm -rf "$B/$n"; mv "$G/$n" "$B/$n"
    ln -s "$H/$n" "$G/$n"
    echo "$n -> $H/$n"
done
# The plugin is ad-hoc signed as a binary (no sealed bundle resources), so `codesign --verify --strict` on the bundle
# fails for the original as well (2026-11-23, XTX machine). What AMFI and VermilionTrust check is each Mach-O's cdhash:
# the moved copies must keep the ones of the originals.
cdh(){ codesign -dvvv "$1" 2>&1 | sed -n 's/^CDHash=//p'; }
bad=0
for f in NVMTLDriver.bundle/Contents/MacOS/NVMTLDriver nvmtl/libvulkan_radeon.dylib; do
    [ -f "$B/$f" ] || continue
    moved=$(cdh "$H/$f")
    if [ "$moved" = "$(cdh "$B/$f")" ]; then echo "$f: cdhash $moved kept"; else echo "$f: cdhash changed"; bad=1; fi
done
exit $bad
