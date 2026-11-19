#!/bin/bash
# SPDX-License-Identifier: MIT
# espflags_test.sh: tools/phxctl/esp_flags.sh (the phxboot pre-hook that takes flags staged on the EFI partition)
# against a fake diskutil and fake EFI partitions. No root, no disks touched. Runs on macOS (BSD stat).
set -u
HERE=$(cd "$(dirname "$0")" && pwd); S=$HERE/../../tools/phxctl/esp_flags.sh
T=$(mktemp -d "${TMPDIR:-/tmp}/espflags.XXXXXX"); PASSN=0; FAILN=0
fake() {   # fake <mounted 0|1>: one EFI partition disk9s1, its contents in $T/esp; mount = a symlink
  mkdir -p "$T/bin" "$T/esp/vermilion/flags" "$T/gpu"; rm -f "$T/mounted" "$T/calls"; [ "$1" = 1 ] && echo "$T/esp" >"$T/mounted"
  cat >"$T/bin/diskutil" <<EOF
#!/bin/bash
echo "\$*" >>"$T/calls"
case "\$1" in
  list) printf '   0:      GUID_partition_scheme                        *1.0 TB     disk9\n   1:                        EFI NO NAME                 2.1 GB     disk9s1\n   2:                 Apple_APFS Container disk10        83.8 GB    disk9s3\n';;
  info) [ -e "$T/mounted" ] && echo "   Mount Point:               \$(cat "$T/mounted")" || echo "   Mount Point:               "; ;;
  mount) [ "\$2" = -mountPoint ] && { rmdir "\$3" 2>/dev/null; ln -s "$T/esp" "\$3"; echo "\$3" >"$T/mounted"; };;
  unmount) rm -f "\$2" "$T/mounted";;
esac
EOF
  chmod +x "$T/bin/diskutil"; }
run() { ESPF_DISKUTIL="$T/bin/diskutil" ESPF_MNT="$T/mnt" bash "$S" "$T/gpu" >"$T/out" 2>&1; }
check() {   # check <name> <cond> ...: every cond (a shell test) must hold
  local name=$1 ok=1 c; shift
  for c in "$@"; do eval "$c" || { ok=0; echo "    failed: $c"; }; done
  if [ $ok = 1 ]; then PASSN=$((PASSN+1)); echo "PASS  $name"; else FAILN=$((FAILN+1)); echo "FAIL  $name"; sed 's/^/    /' "$T/out"; fi
  rm -rf "$T/esp" "$T/gpu" "$T/mnt"; }

fake 0; : >"$T/esp/vermilion/flags/phxboot-desktop"; echo 456 >"$T/esp/vermilion/flags/phxboot-uclk"; run
check set_unmounted '[ -e "$T/gpu/phxboot-desktop" ]' '[ "$(cat "$T/gpu/phxboot-uclk")" = 456 ]' \
  '[ -z "$(ls "$T/esp/vermilion/flags")" ]' 'grep -q "set phxboot-uclk = 456" "$T/out"' 'grep -q "^unmount" "$T/calls"' '[ ! -e "$T/mnt" ]'

fake 1; touch "$T/gpu/phxboot-off"; : >"$T/esp/vermilion/flags/phxboot-off.rm"; run
check rm_mounted '[ ! -e "$T/gpu/phxboot-off" ]' 'grep -q "removed phxboot-off" "$T/out"' '! grep -q "^mount" "$T/calls"' '! grep -q "^unmount" "$T/calls"'

fake 0; : >"$T/esp/vermilion/flags/evil"; : >"$T/esp/vermilion/flags/phxboot-sdma"; run
check ignore_other '[ ! -e "$T/gpu/evil" ]' '[ -e "$T/esp/vermilion/flags/evil" ]' '[ -e "$T/gpu/phxboot-sdma" ]' 'grep -q "ignored evil" "$T/out"'

fake 0; rmdir "$T/esp/vermilion/flags"; run
check no_flags_dir 'grep -q "no vermilion/flags" "$T/out"' 'grep -q "^unmount" "$T/calls"' '[ -z "$(ls -A "$T/gpu")" ]'

echo "RESULT: $PASSN passed, $FAILN failed"; rm -rf "$T"; [ $FAILN = 0 ]
