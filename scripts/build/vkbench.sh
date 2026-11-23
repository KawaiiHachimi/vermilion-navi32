#!/bin/sh
# SPDX-License-Identifier: MIT
# vkbench.sh: the Vulkan twin of metalbench (tools/vkbench/vkbench.c) -> $VERMILION_OUT/vkbench. x86_64, deployment
# target 26.0; it opens RADV directly, no loader. Needs VULKAN_HEADERS. Not signed beyond ad hoc, like metalbench.
# On Linux: cc -O2 -I<Vulkan-Headers>/include -o vkbench tools/vkbench/vkbench.c -ldl -lm
. "$(dirname "$0")/lib.sh"
VKH=${VULKAN_HEADERS:-$ROOT/deps/Vulkan-Headers/include}
xcrun clang -arch x86_64 -mmacosx-version-min=26.0 -O2 -Wall $PFX -I"$VKH" -o "$OUT/vkbench" "$ROOT/tools/vkbench/vkbench.c"
codesign -fs - "$OUT/vkbench" 2>/dev/null
homecheck "$OUT/vkbench"; echo "built $OUT/vkbench"
