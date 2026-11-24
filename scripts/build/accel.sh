#!/bin/sh
# SPDX-License-Identifier: MIT
# accel.sh: VermilionAccel.kext (src/vermilion-accel, formerly PhxAccel.kext: PhxAMDFB framebuffer + NMXAccel accelerator + PhxAGDC) -> $VERMILION_OUT,
# ad-hoc signed by its build.sh. When build/linkgate holds export lists (scripts/build/linkgate_exports.sh), the link gate
# runs too: every imported symbol must be exported by a library in its OSBundleLibraries, or kmutil refuses the load.
. "$(dirname "$0")/lib.sh"
: "${MKSDK:?set MKSDK to a MacKernelSDK checkout}"
S="$ROOT/src/vermilion-accel"
# ASIC (default navi31) has to match the boot kext: PHX_PIPE_DIG selects the 1440p modes the accelerator offers,
# and the boot kext plans the same pipe. ASIC=navi32 = RX 7800 XT / RX 7700 XT (DIG4/DP4, HPD4).
ASIC=${ASIC:-navi31}
case "$ASIC" in
  navi32) PHX_PIPE_DIG=${PHX_PIPE_DIG:-4}; PHX_PIPE_HPD=${PHX_PIPE_HPD:-4} ;;
  *)      PHX_PIPE_DIG=${PHX_PIPE_DIG:-1} ;;
esac
export PHX_PIPE_DIG PHX_PIPE_HPD
(cd "$S" && PHX_PIPE_DIG="$PHX_PIPE_DIG" PHX_PIPE_HPD="${PHX_PIPE_HPD:-}" sh ./build.sh "$MKSDK")
if [ -d "${LINKGATE_EXPORTS:-$ROOT/build/linkgate}" ]; then python3 "$ROOT/scripts/build/linkgate.py" "$S/build/VermilionAccel.kext"
else echo "link gate skipped: no export lists (scripts/build/linkgate_exports.sh)"; fi
rm -rf "$OUT/VermilionAccel.kext"; cp -R "$S/build/VermilionAccel.kext" "$OUT/VermilionAccel.kext"
homecheck "$OUT/VermilionAccel.kext/Contents/MacOS/VermilionAccel"
echo "built $OUT/VermilionAccel.kext (/Library/Extensions + Auxiliary KC; docs/INSTALL.md)"
