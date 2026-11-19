#!/bin/bash
# SPDX-License-Identifier: MIT
# daemon_install.sh <dir> <G>: installs <dir>/phxboot-navi31.sh as <G>/phxboot-navi31.sh (the boot daemon launchd runs),
# for a run-once.sh on the EFI partition (tools/phxctl/esp_flags.sh), so a daemon update reaches macOS through the EFI.
#
# Checks before it touches anything: <dir>/phxboot-navi31.sh.sha256 holds the file's SHA-256 (the first word counts),
# and `bash -n` parses it. Then the installed daemon is copied to <G>/phxboot-navi31.sh.prev (the rollback copy) and the
# new one is moved into place, owned by <G>'s owner, mode 755. It is moved, never written in place: the daemon that runs
# this hook (through phxboot-pre.sh) is the installed file, and bash keeps reading the file it opened, so a rename
# leaves that run on the old daemon; the new one runs from the next boot.
# Rollback: mv <G>/phxboot-navi31.sh.prev <G>/phxboot-navi31.sh (from a run-once.sh, or by hand).
# Exit 0 = installed or already the same, 1 = refused (nothing changed). Bash 3.2.
set -u
D=${1:?usage: daemon_install.sh <dir> <gpu dir>}; G=${2:?usage: daemon_install.sh <dir> <gpu dir>}
SRC=$D/phxboot-navi31.sh; DST=$G/phxboot-navi31.sh
SHASUM=${DI_SHASUM:-/usr/bin/shasum}
say() { echo "daemon-install: $*"; }
[ -f "$SRC" ] || { say "no $SRC: nothing changed"; exit 1; }
want=$(awk '{ print $1; exit }' "$SRC.sha256" 2>/dev/null)
have=$("$SHASUM" -a 256 "$SRC" 2>/dev/null | awk '{ print $1 }')
[ -n "$want" ] && [ "$want" = "$have" ] || { say "SHA-256 mismatch (want ${want:-none}, have ${have:-none}): nothing changed"; exit 1; }
bash -n "$SRC" 2>/dev/null || { say "$SRC does not parse: nothing changed"; exit 1; }
[ -d "$G" ] || { say "no $G: nothing changed"; exit 1; }
if [ -f "$DST" ] && cmp -s "$SRC" "$DST"; then say "already installed (sha256 $have)"; exit 0; fi
owner=$(/usr/bin/stat -f '%u:%g' "$G" 2>/dev/null || echo "")
if [ -f "$DST" ]; then
  cp -p "$DST" "$DST.prev" || { say "could not keep $DST.prev: nothing changed"; exit 1; }
fi
tmp=$G/.phxboot-navi31.sh.new.$$
cp "$SRC" "$tmp" && chmod 755 "$tmp" && { [ -z "$owner" ] || chown "$owner" "$tmp"; } && mv -f "$tmp" "$DST" \
  || { rm -f "$tmp"; say "install failed: $DST unchanged"; exit 1; }
say "installed $DST (sha256 $have)$([ -f "$DST.prev" ] && echo "; rollback copy $DST.prev")"
exit 0
