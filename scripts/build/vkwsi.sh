#!/bin/sh
# SPDX-License-Identifier: MIT
# vkwsi.sh: the Vulkan window-surface probe (tools/metaltest/vkwsi.m) -> $VERMILION_OUT/vkwsi.
#
# It opens RADV directly (there is no Vulkan loader on the target), makes an NSWindow with a CAMetalLayer, creates a
# VK_EXT_metal_surface, a FIFO swapchain, clears and presents every frame, and ends with WSI OK. Needs Vulkan-Headers:
#   VULKAN_HEADERS=<dir with vulkan/vulkan_core.h> sh scripts/build/vkwsi.sh
# Run on the target:  ./vkwsi /Library/GPUBundles/nvmtl/libvulkan_radeon.dylib [frames] [capture.png]
. "$(dirname "$0")/lib.sh"
VKH=${VULKAN_HEADERS:-$ROOT/deps/Vulkan-Headers/include}
[ -f "$VKH/vulkan/vulkan_core.h" ] || { echo "vkwsi: no Vulkan headers at $VKH (set VULKAN_HEADERS)"; exit 1; }
xcrun clang -arch x86_64 -mmacosx-version-min=26.0 -fobjc-arc -O2 $PFX -o "$OUT/vkwsi" "$ROOT/tools/metaltest/vkwsi.m" \
  -I"$VKH" -framework Cocoa -framework QuartzCore -framework IOSurface
homecheck "$OUT/vkwsi"; echo "built $OUT/vkwsi"