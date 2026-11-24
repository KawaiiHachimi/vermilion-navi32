#!/bin/sh
# SPDX-License-Identifier: MIT
# navi32_decoder_build.sh: build the VideoToolbox decoder pieces ON THE TARGET Mac.
#
# Why on the target: the lab build host is an arm64 Mac without Rosetta, so FFmpeg's configure cannot run the
# x86_64 test programs it builds for `-arch x86_64` ("C compiler test failed" / Bad CPU type in executable).
# The target is the Intel Mac itself, where x86_64 is native.
#
#   sudo sh navi32_decoder_build.sh [workdir]        default workdir: /Users/cool/gpu/decbuild
#
# It clones FFmpeg n7.1, applies patches/ffmpeg, configures it as libavcodec_phx/libavutil_phx, builds installs to
# <workdir>/prefix, then builds src/video-decoder against it and ad-hoc signs every Mach-O. Nothing is installed
# into /Library; see docs/INSTALL.md section 7 for where the bundle goes.
set -eu
W=${1:-${DECBUILD:-/Users/cool/gpu/decbuild}}
SRC=$W/vermilion
mkdir -p "$W"

say(){ echo "== $*"; }

say 'toolchain check'
command -v git >/dev/null || { echo 'git is required'; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo 'Xcode command line tools are required'; exit 1; }
echo "  clang: $(clang --version | head -1)"
echo "  git:   $(git --version)"

say 'fetch the tree (needed for patches/ and src/video-decoder)'
if [ -d "$SRC/.git" ]; then ( cd "$SRC" && git fetch -q origin && git checkout -q navi32 2>/dev/null || true );
else git clone -q https://github.com/ownback/vermilion-xtx "$SRC"; fi
echo "  tree: $SRC"

say 'FFmpeg n7.1 + patches'
export FFMPEG_SRC=$W/ffmpeg FFMPEG_PREFIX=$W/prefix
VULKAN_HEADERS=${VULKAN_HEADERS:-$W/Vulkan-Headers/include}
if [ ! -f "$VULKAN_HEADERS/vulkan/vulkan_core.h" ]; then
  ( cd "$W" && git clone -q --depth 1 https://github.com/KhronosGroup/Vulkan-Headers )
fi
( cd "$SRC" && VULKAN_HEADERS=$VULKAN_HEADERS sh scripts/build/ffmpeg.sh )
say 'decoder library'
( cd "$SRC" && FFMPEG_PREFIX=$FFMPEG_PREFIX VULKAN_HEADERS=$VULKAN_HEADERS sh src/video-decoder/build.sh )
say "built: $SRC/src/video-decoder/build"
ls -l "$SRC/src/video-decoder/build" | sed 's/^/  /'

say 'next step (docs/INSTALL.md section 7)'
echo '  install libphxdec_hevc.dylib + the two libav*.dylib next to PhxVideoDecoder.bundle,'
echo '  chown root:wheel, chmod 755, and codesign -fs - every Mach-O.'