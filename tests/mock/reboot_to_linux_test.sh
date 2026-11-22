#!/bin/bash
# SPDX-License-Identifier: MIT
# reboot_to_linux_test.sh: tools/linux/reboot_to_linux.sh against a fake efibootmgr. No root, no firmware access.
set -u
HERE=$(cd "$(dirname "$0")" && pwd); S=$HERE/../../tools/linux/reboot_to_linux.sh
T=$(mktemp -d "${TMPDIR:-/tmp}/rtl.XXXXXX"); PASSN=0; FAILN=0
cat >"$T/efibootmgr" <<X
#!/bin/bash
if [ "\${1:-}" = --bootnext ]; then echo "\$2" >"$T/next"; [ -e "$T/fail" ] && exit 5; exit 0; fi
cat "$T/list"
X
chmod +x "$T/efibootmgr"
run() { rm -f "$T/next"; RG_EFIBOOTMGR="$T/efibootmgr" sh "$S" "$@" >"$T/out" 2>&1; echo $? >"$T/rc"; }
check() { local name=$1 ok=1 c; shift
  for c in "$@"; do eval "$c" || { ok=0; echo "    failed: $c"; }; done
  if [ $ok = 1 ]; then PASSN=$((PASSN+1)); echo "PASS  $name"; else FAILN=$((FAILN+1)); echo "FAIL  $name"; sed 's/^/    /' "$T/out"; fi; }
printf 'BootCurrent: 0000\nTimeout: 1 seconds\nBootOrder: 0006,0000,0001,0002\nBoot0000* Limine\tHD(1,GPT,x,0x800,0x400000)/\\EFI\\limine\\limine_x64.efi\nBoot0001* UEFI OS\tHD(1,GPT,x)/\\EFI\\BOOT\\BOOTX64.EFI\nBoot0006* OpenCore\tHD(1,GPT,x)/\\EFI\\OC\\OpenCore.efi\nBoot000A  Limine Fallback\tHD(1,GPT,x)\n' >"$T/list"
run;           check limine_default '[ "$(cat "$T/rc")" = 0 ]' '[ "$(cat "$T/next")" = 0000 ]' 'grep -q "BootNext=0000 (Limine)" "$T/out"'
run OpenCore;  check other_label '[ "$(cat "$T/rc")" = 0 ]' '[ "$(cat "$T/next")" = 0006 ]'
run "Limine Fallback"; check inactive_entry_with_spaces '[ "$(cat "$T/next")" = 000A ]'
run Limin;     check no_prefix_match '[ "$(cat "$T/rc")" = 1 ]' '[ ! -e "$T/next" ]' 'grep -q "no boot entry" "$T/out"'
printf 'Boot0000* Limine\tA\nBoot0003* Limine\tB\n' >"$T/list"
run;           check duplicate '[ "$(cat "$T/rc")" = 1 ]' '[ ! -e "$T/next" ]' 'grep -q "several entries" "$T/out"'
printf 'Boot0000* Limine\tA\n' >"$T/list"; touch "$T/fail"
run;           check efibootmgr_fails '[ "$(cat "$T/rc")" = 1 ]' 'grep -q "failed" "$T/out"'
rm -rf "$T"
echo "reboot_to_linux_test: $PASSN passed, $FAILN failed"
[ $FAILN = 0 ]
