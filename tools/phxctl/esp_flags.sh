#!/bin/bash
# SPDX-License-Identifier: MIT
# esp_flags.sh <G>: phxboot pre-start hook (install it as <G>/phxboot-pre.sh). It takes flag files staged on an EFI
# partition into <G> (the desktop user's ~/gpu), so another OS that cannot write APFS (Linux, on a dual-boot machine
# without a usable macOS desktop) can set the boot daemon's flags for the next macOS boot.
#
# On the EFI partition, in vermilion/flags/:
#   <flag>       copied to <G>/<flag> with its content (empty file = touch), owned by <G>'s owner
#   <flag>.rm    removes <G>/<flag>
# Only names starting with "phxboot-" are taken; anything else is logged and left alone. Every file taken is deleted
# from the EFI partition, so each staged change applies once. The phxboot log of the run gets one line per file.
#
# The EFI partitions are found with `diskutil list`; one that is not mounted is mounted for the run and unmounted
# again. Tested only by the mock harness (tests/mock/espflags_test.sh) so far (unverified on hardware).
set -u
G=${1:?usage: esp_flags.sh <gpu dir>}
DISKUTIL=${ESPF_DISKUTIL:-/usr/sbin/diskutil}
MNT=${ESPF_MNT:-/private/tmp/phxesp.$$}
owner=$(/usr/bin/stat -f '%u:%g' "$G" 2>/dev/null || echo "")

take() {   # take <flags dir>
  local d=$1 f n t
  for f in "$d"/*; do
    [ -f "$f" ] || continue
    n=${f##*/}
    case "$n" in
      phxboot-*.rm) t=${n%.rm}; rm -f "$G/$t"; rm -f "$f"; echo "esp-flags: removed $t";;
      phxboot-*)    cp "$f" "$G/$n.esp.$$" && mv "$G/$n.esp.$$" "$G/$n" && { [ -z "$owner" ] || chown "$owner" "$G/$n"; } && rm -f "$f" \
                      && echo "esp-flags: set $n$([ -s "$G/$n" ] && printf ' = %s' "$(head -c 40 "$G/$n" | tr -d '\n')")";;
      *)            echo "esp-flags: ignored $n (not a phxboot- flag)";;
    esac
  done
}

found=0
for id in $("$DISKUTIL" list 2>/dev/null | awk '$2 == "EFI" { print $NF }'); do
  mp=$("$DISKUTIL" info "$id" 2>/dev/null | awk -F': *' '/^ *Mount Point:/ { print $2 }')
  ours=0
  if [ -z "$mp" ]; then
    mkdir -p "$MNT" && "$DISKUTIL" mount -mountPoint "$MNT" "$id" >/dev/null 2>&1 || { echo "esp-flags: could not mount $id"; continue; }
    mp=$MNT; ours=1
  fi
  if [ -d "$mp/vermilion/flags" ]; then found=1; take "$mp/vermilion/flags"; sync; fi
  [ $ours = 1 ] && { "$DISKUTIL" unmount "$mp" >/dev/null 2>&1 || echo "esp-flags: could not unmount $id"; rmdir "$MNT" 2>/dev/null; }
done
[ $found = 1 ] || echo "esp-flags: no vermilion/flags on any EFI partition"
exit 0
