#!/bin/sh
# SPDX-License-Identifier: MIT
# reboot_to_linux.sh [label]: sets the firmware's BootNext to the boot entry called <label> (default Limine), so the
# reboot that is under way starts Linux again. Run by vermilion-reboot-to-linux.service on reboot only (never on
# power-off). Why: when OpenCore is first in the boot order, a warm Linux reboot would otherwise start macOS on a GPU
# that Linux's amdgpu left initialised, and Vermilion's bring-up needs a cold GPU (CLAUDE.md, hard rule 1). A
# power-off, then power-on, still starts OpenCore.
#
# Test hook: RG_EFIBOOTMGR (the efibootmgr command).
set -u
LABEL=${1:-Limine}
EFIBOOTMGR=${RG_EFIBOOTMGR:-efibootmgr}
# "Boot0000* Limine<TAB>HD(...)": the label is what follows "BootXXXX* " (or "BootXXXX  " when inactive) up to the first tab
n=$($EFIBOOTMGR 2>/dev/null | awk -F '\t' -v l="$LABEL" '
  $1 ~ /^Boot[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][* ] / {
    x = $1; sub(/^Boot[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][* ] /, "", x)
    if (x == l) print substr($1, 5, 4) }')
case "$n" in
  "") echo "reboot-to-linux: no boot entry called '$LABEL'; BootNext unchanged"; exit 1;;
  *[!0-9A-Fa-f]*) echo "reboot-to-linux: several entries called '$LABEL' ($(echo $n)); BootNext unchanged"; exit 1;;
esac
if $EFIBOOTMGR --bootnext "$n" >/dev/null 2>&1; then echo "reboot-to-linux: BootNext=$n ($LABEL)"
else echo "reboot-to-linux: efibootmgr --bootnext $n failed"; exit 1; fi
