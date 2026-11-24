#!/bin/sh
# SPDX-License-Identifier: MIT
# fetch_firmware.sh [--lock FILE] [dest]: download the AMD microcode listed in the lock file from linux-firmware (amdgpu/) and
# check each file's md5 against fw.lock. Default dest: $HOME/gpu/fw/navi31, the directory the box scripts read.
# The files are AMD's, redistributable in binary form under LICENSE.amdgpu (LICENSES/LICENSE.amdgpu.txt); this
# repository does not ship them. The files come from the linux-firmware commit pinned in fw.lock
# (`linux-firmware-commit:`); LINUX_FIRMWARE_REF overrides it with another commit. A file whose md5 differs from
# fw.lock is rejected, since the bring-up offsets and sizes were measured on those revisions.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
# usage: fetch_firmware.sh [--lock fw-navi32.lock] [dest]
#   the lock file selects the ASIC: fw.lock (Navi 31, default) or fw-navi32.lock (Navi 32). The destination
#   defaults to ~/gpu/fw/<asic>.
LOCK=$HERE/fw.lock
DEST=""
while [ $# -gt 0 ]; do
  case "$1" in
    --lock) LOCK=${2:?--lock needs a file}; shift 2 ;;
    *) DEST=$1; shift ;;
  esac
done
# a bare file name means the lock file of this directory; a path is kept as given
case "$LOCK" in
  /*) ;;
  */*) LOCK=$(cd "$(dirname "$LOCK")" && pwd)/$(basename "$LOCK") ;;
  *)   LOCK=$HERE/$LOCK ;;
esac
[ -f "$LOCK" ] || { echo "no such lock file: $LOCK" >&2; exit 1; }
case "$(basename "$LOCK")" in
  fw-navi32.lock) ASIC=navi32 ;;
  *)              ASIC=navi31 ;;
esac
DEST=${DEST:-$HOME/gpu/fw/$ASIC}
PIN=$(sed -n 's/^# linux-firmware-commit: *\([0-9a-f]\{40\}\).*/\1/p' "$LOCK")
REF=${LINUX_FIRMWARE_REF:-$PIN}
[ -n "$REF" ] || { echo "$LOCK has no linux-firmware-commit line" >&2; exit 1; }
BASES=${LINUX_FIRMWARE_URL:-"https://gitlab.com/kernel-firmware/linux-firmware/-/raw/$REF/amdgpu https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/amdgpu?id=$REF"}
mkdir -p "$DEST"
grep -v '^#' "$LOCK" | while read -r f sum size; do
  [ -n "$f" ] || continue
  if [ -f "$DEST/$f" ] && [ "$(md5 -q "$DEST/$f")" = "$sum" ]; then echo "ok      $f"; continue; fi
  got=0
  for b in $BASES; do
    case $b in *\?id=*) url="${b%%\?id=*}/$f?id=${b#*\?id=}";; *) url="$b/$f";; esac
    if curl -fsSL --retry 3 -o "$DEST/$f.tmp" "$url" && [ "$(md5 -q "$DEST/$f.tmp")" = "$sum" ]; then
      mv "$DEST/$f.tmp" "$DEST/$f"; echo "fetched $f"; got=1; break
    fi
    rm -f "$DEST/$f.tmp"
  done
  [ $got = 1 ] || { echo "FAILED  $f: no copy with md5 $sum at ref $REF (the pinned commit is in $LOCK)" >&2; exit 1; }
done
echo "firmware in $DEST matches $LOCK"
