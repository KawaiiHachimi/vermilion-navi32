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
# vermilion/run-once.sh (2026-11-19), when present, runs once as root right after the flags, with the EFI
# partition's vermilion directory as its argument (so it can install files staged there) and its output in the phxboot
# log. It is moved off the EFI partition to $ESPF_DONE (default /var/db/vermilion) BEFORE it runs, so a script that
# hangs or breaks the boot does not run again. It runs before the daemon reads any flag, so it can set flags in <G>.
# It shares the daemon's time budget for the hook (PHXBOOT_PRE_WAIT, 30 s): keep it short.
# Security: whoever can write the EFI partition can run code as root in macOS through this. On the XTX machine that
# is root on Linux, which can already do anything; do not use this hook on a machine where the EFI partition is
# writable by less trusted users.
#
# The EFI partitions are found with `diskutil list`; one that is not mounted is mounted for the run and unmounted
# again (checked against a real EFI partition on macbox, 2026-11-19).
set -u
G=${1:?usage: esp_flags.sh <gpu dir>}
DISKUTIL=${ESPF_DISKUTIL:-/usr/sbin/diskutil}
MNT=${ESPF_MNT:-/private/tmp/phxesp.$$}; DONE=${ESPF_DONE:-/var/db/vermilion}
STAT=${ESPF_STAT:-/usr/bin/stat}   # ESPF_STAT: a BSD-compatible stat for the mock tests off macOS
owner=$("$STAT" -f '%u:%g' "$G" 2>/dev/null || echo "")

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

# At boot diskarbitrationd may not answer yet: retry the listing and the mount for up to ESPF_TRIES seconds (default 15,
# inside the daemon's 30 s budget for the hook).
TRIES=${ESPF_TRIES:-15}
ids=""; n=0
while [ -z "$ids" ] && [ $n -lt "$TRIES" ]; do
  ids=$("$DISKUTIL" list 2>/dev/null | awk '$2 == "EFI" { print $NF }'); [ -n "$ids" ] || { n=$((n+1)); sleep 1; }
done
[ -n "$ids" ] || echo "esp-flags: no EFI partition listed after ${TRIES}s"
found=0
for id in $ids; do
  mp=$("$DISKUTIL" info "$id" 2>/dev/null | awk -F': *' '/^ *Mount Point:/ { print $2 }')
  [ -n "$mp" ] && [ -d "$mp" ] || mp=""      # "Not mounted" style values count as unmounted
  ours=0
  if [ -z "$mp" ]; then
    mkdir -p "$MNT"; n=0
    until "$DISKUTIL" mount -mountPoint "$MNT" "$id" >/dev/null 2>&1; do
      n=$((n+1)); [ $n -lt "$TRIES" ] || break; sleep 1; done
    [ $n -lt "$TRIES" ] || { echo "esp-flags: could not mount $id"; rmdir "$MNT" 2>/dev/null; continue; }
    mp=$MNT; ours=1
  fi
  if [ -d "$mp/vermilion/flags" ]; then found=1; take "$mp/vermilion/flags"; sync; fi
  if [ -f "$mp/vermilion/run-once.sh" ]; then
    mkdir -p "$DONE"; ro="$DONE/run-once-$(date +%Y%m%d-%H%M%S).sh"
    if mv "$mp/vermilion/run-once.sh" "$ro"; then sync
      echo "esp-flags: run-once: running $ro"
      bash "$ro" "$mp/vermilion" "$G" 2>&1 | sed 's/^/  run-once: /'; echo "esp-flags: run-once: exit ${PIPESTATUS[0]}"
    else echo "esp-flags: run-once: could not move it off the EFI partition: not run"; fi
  fi
  [ $ours = 1 ] && { "$DISKUTIL" unmount "$mp" >/dev/null 2>&1 || echo "esp-flags: could not unmount $id"; rmdir "$MNT" 2>/dev/null; }
done
[ $found = 1 ] || echo "esp-flags: no vermilion/flags on any EFI partition"
exit 0
