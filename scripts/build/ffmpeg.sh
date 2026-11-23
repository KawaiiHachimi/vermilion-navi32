#!/bin/sh
# SPDX-License-Identifier: MIT
# ffmpeg.sh: the FFmpeg libraries the video decoder links (src/video-decoder): FFmpeg n7.1 (b08d7969) + patches/ffmpeg,
# built as shared libavcodec_phx / libavutil_phx (LGPL-2.1-or-later, no GPL or nonfree parts), x86_64, with only
# what the decoder uses: the hevc and av1 decoders, their parsers and the hevc_vulkan / av1_vulkan hwaccels.
# Install names are @loader_path/..., so the dylibs work from the folder they are copied into.
#   FFMPEG_SRC   checkout dir (default deps/ffmpeg; cloned at n7.1 if missing)
#   FFMPEG_PREFIX install prefix (default deps/ffmpeg-lib/prefix), what src/video-decoder/build.sh reads
#   VULKAN_HEADERS Vulkan-Headers include dir (default deps/Vulkan-Headers/include; FFmpeg 7.1 needs >= 1.3.277)
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SRC=${FFMPEG_SRC:-$ROOT/deps/ffmpeg}
PREFIX=${FFMPEG_PREFIX:-$ROOT/deps/ffmpeg-lib/prefix}
VKH=${VULKAN_HEADERS:-$ROOT/deps/Vulkan-Headers/include}
REV=b08d7969c550a804a59511c7b83f2dd8cc0499b8
if [ ! -d "$SRC/.git" ]; then
  git clone --depth 1 --branch n7.1 https://git.ffmpeg.org/ffmpeg.git "$SRC"
fi
[ "$(git -C "$SRC" rev-parse HEAD)" = $REV ] || { echo "ffmpeg.sh: $SRC is not at n7.1 ($REV)"; exit 1; }
if ! git -C "$SRC" diff --quiet; then
  git -C "$SRC" apply --reverse --check "$ROOT"/patches/ffmpeg/*.patch 2>/dev/null \
    || { echo "ffmpeg.sh: $SRC has changes that are not patches/ffmpeg"; exit 1; }
else
  git -C "$SRC" apply "$ROOT"/patches/ffmpeg/*.patch
fi
cd "$SRC"
./configure --prefix="$PREFIX" --install-name-dir='@loader_path' --build-suffix=_phx \
  --arch=x86_64 --cc="xcrun clang -arch x86_64" --extra-cflags="-mmacosx-version-min=13.0 -I$VKH" \
  --extra-ldflags="-mmacosx-version-min=13.0" \
  --enable-shared --disable-static --disable-programs --disable-doc --disable-autodetect \
  --disable-avformat --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample \
  --disable-network --disable-everything \
  --enable-vulkan --enable-decoder=hevc,av1 --enable-parser=hevc,av1 --enable-hwaccel=hevc_vulkan,av1_vulkan
make -j"$(sysctl -n hw.ncpu)"
make install
ls -l "$PREFIX/lib"/libavcodec_phx.61.dylib "$PREFIX/lib"/libavutil_phx.59.dylib
otool -L "$PREFIX/lib/libavcodec_phx.61.dylib" | head -5
