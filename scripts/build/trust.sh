#!/bin/sh
# SPDX-License-Identifier: MIT
# trust.sh: VermilionTrust.kext (src/vermilion-trust: AMFI library validation stays on, only Vermilion's own cdhashes
# are let through) -> $VERMILION_OUT. Injected by OpenCore after Lilu. The cdhash list comes from the code installed in
# /Library/GPUBundles (or the files given as arguments): rebuild after every plugin / translator / RADV install.
# Needs MKSDK and the Lilu SDK, which ships in Lilu's DEBUG release (fetched here, pinned by sha256).
. "$(dirname "$0")/lib.sh"
: "${MKSDK:?set MKSDK to a MacKernelSDK checkout}"
S="$ROOT/src/vermilion-trust"
LILU_VER=1.7.2
LILU_SHA=e95df95d82e8b151047abf359ecd9c853755c59293e19dfcc3907da8fbd8cf89
LILU_DIR=${LILU_SDK:-$OUT/deps/Lilu-$LILU_VER-DEBUG}
if [ ! -f "$LILU_DIR/Lilu.kext/Contents/Resources/Headers/kern_api.hpp" ]; then
  mkdir -p "$LILU_DIR"
  curl -fsSL -o "$LILU_DIR/lilu.zip" "https://github.com/acidanthera/Lilu/releases/download/$LILU_VER/Lilu-$LILU_VER-DEBUG.zip"
  echo "$LILU_SHA  $LILU_DIR/lilu.zip" | shasum -a 256 -c - >/dev/null || { echo "trust.sh: Lilu SDK hash mismatch" >&2; exit 1; }
  (cd "$LILU_DIR" && unzip -qo lilu.zip && rm lilu.zip)
fi
sh "$S/gen_list.sh" "$@"
(cd "$S" && sh ./build.sh "$MKSDK" "$LILU_DIR/Lilu.kext")
if [ -d "${LINKGATE_EXPORTS:-$ROOT/build/linkgate}" ]; then python3 "$ROOT/scripts/build/linkgate.py" "$S/build/VermilionTrust.kext"
else echo "link gate skipped: no export lists (scripts/build/linkgate_exports.sh)"; fi
rm -rf "$OUT/VermilionTrust.kext"; cp -R "$S/build/VermilionTrust.kext" "$OUT/VermilionTrust.kext"
homecheck "$OUT/VermilionTrust.kext/Contents/MacOS/VermilionTrust"
echo "built $OUT/VermilionTrust.kext (OpenCore EFI/OC/Kexts, after Lilu; stage vermilion-trustlog first)"
