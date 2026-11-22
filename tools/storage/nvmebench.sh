#!/bin/bash
# SPDX-License-Identifier: MIT
# Storage baseline for the macOS NVMe: records the conditions, then runs
#   1. raw-read.fio  read-only on a raw partition (fio --readonly), if RAWDEV is set
#   2. apfs.fio      file test in a scratch folder on the boot volume (42 GiB of data)
#   3. fsynclat      fsync / F_BARRIERFSYNC / F_FULLFSYNC latency
# while sampling the SSD temperature every 2 s; prints SMART before / after (incl. the data written).
#
# Usage (writes about 60-80 GB to the SSD; don't run it often):
# RAWDEV=/dev/rdisk0s2 tools/storage/nvmebench.sh [outdir]
#   Root is needed for smartctl and the raw device. sudo asks once in a terminal; without one, set
#   SUDO='sudo -A' and SUDO_ASKPASS.
#   RAWDEV: a partition with real data (here the Linux LUKS partition). Leave unset to skip the raw test.
#   SUSTAINED_GIB=N: also write N GiB sequentially (the SLC cache of the 1 TB MP700 PRO lasted about 190 GB in one run; this wears
#   the SSD, so it is off by default).
# Needs fio built with --disable-shm (macOS SysV shm is too small) and smartctl; see README.md.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$PWD/nvmebench-$(date +%Y%m%d-%H%M%S)}
FIO=${FIO:-$(command -v fio || echo "$HOME/.local/bin/fio")}
SMARTCTL=${SMARTCTL:-$(command -v smartctl || echo "$HOME/.local/sbin/smartctl")}
DISK=${DISK:-disk0}
SUDO=${SUDO:-sudo}
scratch=${SCRATCH:-$HOME/.nvmebench-scratch}
mkdir -p "$out" "$scratch"
stop_mon() { if [ -n "${mon:-}" ]; then kill "$mon" 2>/dev/null || true; wait "$mon" 2>/dev/null || true; mon=; fi; }
trap 'stop_mon; rm -rf "$scratch"' EXIT

smart() { $SUDO "$SMARTCTL" -A "$DISK" | grep -E '^(Temperature|Percentage Used|Data Units (Read|Written)|Unsafe Shutdowns|Media and Data|Warning|Critical)'; }

{
  echo "date: $(date)"; echo "uptime: $(uptime)"
  echo "macOS: $(sw_vers -productVersion) $(sw_vers -buildVersion)"
  echo "boot-args: $(sysctl -n kern.bootargs)"
  echo "fio: $("$FIO" --version)"
  diskutil info "$DISK" | grep -E 'Media Name|Protocol|Device Location'
  ioreg -r -c IONVMeController -d1 | grep -E '"(apst|Firmware Revision)"'
  echo "background I/O (iostat, 3 s):"; iostat -d -w 1 -c 3 "$DISK"
} > "$out/conditions.txt"
smart > "$out/smart-before.txt"

( while :; do t=$($SUDO "$SMARTCTL" -A "$DISK" | awk '/^Temperature:/{print $2}'); echo "$(date +%s) $t"; sleep 2; done ) > "$out/temps.txt" &
mon=$!

if [ -n "${RAWDEV:-}" ]; then
  $SUDO env FIODEV="$RAWDEV" "$FIO" --readonly --thread "$here/raw-read.fio" --output-format=json --output="$out/raw.json"
  $SUDO chown "$(id -u)" "$out/raw.json"
fi
FIODIR="$scratch" "$FIO" --thread "$here/apfs.fio" --output-format=json --output="$out/apfs.json"
if [ -n "${SUSTAINED_GIB:-}" ]; then
  "$FIO" --thread --name=sustained --directory="$scratch" --filename_format='f.$jobnum' --size=2g --rw=write --bs=1m \
    --numjobs=8 --io_size=$((SUSTAINED_GIB / 8))g --direct=1 --ioengine=psync --group_reporting \
    --write_bw_log="$out/sustained" --log_avg_msec=5000 --output-format=json --output="$out/sustained.json"
fi
cc=${CC:-clang}; "$cc" -O2 -o "$out/fsynclat" "$here/fsynclat.c"
"$out/fsynclat" "$scratch/sync.bin" 3000 > "$out/fsync.txt"

stop_mon; smart > "$out/smart-after.txt"
python3 "$here/fiosum.py" "$out"/*.json | tee "$out/summary.txt"
cat "$out/fsync.txt" | tee -a "$out/summary.txt"
awk '{print $2}' "$out/temps.txt" | sort -n | sed -n '1p;$p' | paste -sd' ' - | sed 's/^/SSD temperature min max (C): /' | tee -a "$out/summary.txt"
paste "$out/smart-before.txt" "$out/smart-after.txt" | tee -a "$out/summary.txt"
