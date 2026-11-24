#!/bin/sh
# SPDX-License-Identifier: MIT
# kext.sh: VermilionBoot.kext (src/vermilion-boot; formerly PhxBringup.kext, docs/NAMING.md) -> $VERMILION_OUT/VermilionBoot.kext. Needs MKSDK and the generated
# src/vermilion-boot/phx_c3_image.inc (made here from $PHX_FW/dcn_3_2_0_dmcub.bin when missing; tools/firmware/c3_image.py).
. "$(dirname "$0")/lib.sh"
: "${MKSDK:?set MKSDK to a MacKernelSDK checkout}"
S="$ROOT/src/vermilion-boot"
# ASIC (default navi31): selects the firmware directory and the pipe path the plans drive.
#   navi32 = RX 7800 XT / RX 7700 XT: firmware in ~/gpu/fw/navi32, monitor on OTG0 + DIG4 + DP4
#   (PHX_PIPE_DIG=4), plan phx_p0_plan_1080p60n32.inc built from a Linux capture of that board.
ASIC=${ASIC:-navi31}
case "$ASIC" in
  navi32) PHX_PIPE_DIG=${PHX_PIPE_DIG:-4}; PHX_PIPE_HPD=${PHX_PIPE_HPD:-4} ;;
  *)      PHX_PIPE_DIG=${PHX_PIPE_DIG:-1} ;;
esac
export PHX_PIPE_DIG
export PHX_PIPE_HPD
FWD=${PHX_FW:-$HOME/gpu/fw/$ASIC}
[ -f "$S/phx_c3_image.inc" ] || python3 "$ROOT/tools/firmware/c3_image.py" --fw "$FWD/dcn_3_2_0_dmcub.bin"
# PHX_PIPE_DIG reaches the compiler through build.sh (which appends it to CFLAGS)
(cd "$S" && PHX_PIPE_DIG="$PHX_PIPE_DIG" PHX_PIPE_HPD="${PHX_PIPE_HPD:-}" sh ./build.sh "$MKSDK")
rm -rf "$OUT/VermilionBoot.kext"; cp -R "$S/build/VermilionBoot.kext" "$OUT/VermilionBoot.kext"
homecheck "$OUT/VermilionBoot.kext/Contents/MacOS/VermilionBoot"
echo "built $OUT/VermilionBoot.kext (OpenCore EFI/OC/Kexts; docs/INSTALL.md)"
