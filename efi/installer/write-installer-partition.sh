#!/bin/bash
# SPDX-License-Identifier: MIT
# Run on Linux: write a macOS installer image (made by make-installer-image.sh on a Mac reachable over SSH) onto a
# local partition, streaming it compressed, then verify it byte for byte.
#
#   write-installer-partition.sh <ssh-host> <image path on that host> <partition, e.g. /dev/nvme0n1p4> --yes
#
# Safety: the partition must exist, be a partition (not a whole disk), not be mounted or in use, carry the GPT
# name "macOS Installer", and be at least as large as the image. Nothing is written without --yes.
set -euo pipefail
host=${1:?usage: write-installer-partition.sh <ssh-host> <image> <partition> --yes}
img=${2:?image path on the Mac}
part=${3:?partition}
[ "${4:-}" = "--yes" ] || { echo "refusing to write without --yes"; exit 1; }
# SUDO: how to run root commands locally (default sudo; e.g. SUDO="sudo -A" with SUDO_ASKPASS for scripted runs)
if [ "$(id -u)" = 0 ]; then SUDO=""; else SUDO=${SUDO:-sudo}; fi

say() { echo "== $*"; }

[ -b "$part" ] || { echo "$part is not a block device"; exit 1; }
[ "$(lsblk -dno TYPE "$part")" = part ] || { echo "$part is not a partition"; exit 1; }
label=$(lsblk -dno PARTLABEL "$part")
[ "$label" = "macOS Installer" ] || { echo "$part has GPT name '$label', expected 'macOS Installer'"; exit 1; }
[ -z "$(lsblk -no MOUNTPOINTS "$part" | tr -d '[:space:]')" ] || { echo "$part is mounted"; exit 1; }
[ -z "$(lsblk -no NAME "$part" | tail -n +2)" ] || { echo "$part is in use (has holders)"; exit 1; }
part_bytes=$(lsblk -bdno SIZE "$part")

say "attaching the image on $host (read-only, not mounted)"
dev=$(ssh "$host" "hdiutil attach -nomount -readonly '$img' | awk '/\\/dev\\/disk/{print \$1; exit}'")
[ -n "$dev" ] || { echo "could not attach $img on $host"; exit 1; }
trap 'ssh "$host" "hdiutil detach $dev -force >/dev/null 2>&1" || true' EXIT
img_bytes=$(ssh "$host" "diskutil info -plist $dev | plutil -extract TotalSize raw -o - -")
echo "image $img_bytes bytes, partition $part_bytes bytes"
[ "$img_bytes" -le "$part_bytes" ] || { echo "image does not fit"; exit 1; }
rdev=${dev/disk/rdisk}

say "streaming $host:$rdev -> $part"
ssh "$host" "sudo dd if=$rdev bs=4m 2>/dev/null | gzip -1" | gunzip | $SUDO dd of="$part" bs=4M conv=fsync status=progress
sync

say "verifying (SHA-256 of $img_bytes bytes on both sides)"
remote=$(ssh "$host" "sudo dd if=$rdev bs=4m 2>/dev/null | shasum -a 256 | cut -d' ' -f1")
local=$($SUDO head -c "$img_bytes" "$part" | sha256sum | cut -d' ' -f1)
echo "mac:   $remote"
echo "linux: $local"
[ "$remote" = "$local" ] || { echo "MISMATCH"; exit 1; }
echo "ok: $part holds the installer"
