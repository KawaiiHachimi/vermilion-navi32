#!/bin/sh
# navi32_linux_capture_ssh.sh: run navi32_linux_capture.py on a Linux machine over SSH and bring the read back.
#
#   tools/capture/navi32_linux_capture_ssh.sh user@host [out.txt] [sudo]
#
# Copies the script and the register list to /tmp on the machine, runs them under sudo there, and writes the
# read to out.txt (default: navi32-regs-<date>.txt). The machine needs python3 and a debugfs mounted
# (/sys/kernel/debug), with amdgpu bound and the monitor already at the mode to capture.
set -eu
HOST=${1:?usage: navi32_linux_capture_ssh.sh user@host [out.txt] [sudo]}
OUT=${2:-navi32-regs-$(date +%Y%m%d-%H%M).txt}
SUDO=${3:-sudo}
HERE=$(cd "$(dirname "$0")" && pwd)
LIST=$HERE/navi32-dcn-reglist.txt
[ -f "$LIST" ] || { echo "missing $LIST" >&2; exit 1; }
ssh "$HOST" "mkdir -p /tmp/navi32" < /dev/null
scp -q "$HERE/navi32_linux_capture.py" "$LIST" "$HOST:/tmp/navi32/"
ssh "$HOST" "$SUDO python3 /tmp/navi32/navi32_linux_capture.py --reglist /tmp/navi32/navi32-dcn-reglist.txt" > "$OUT"
echo "wrote $OUT ($(wc -l < "$OUT" | tr -d " ") lines)" >&2
