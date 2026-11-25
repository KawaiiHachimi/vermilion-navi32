#!/bin/bash
# SPDX-License-Identifier: MIT
# bringup_warm_test.sh: MOCK test of the cold-GPU check in tools/phxctl/bringup_navi31.sh, run through the real
# tools/phxctl/navi31_up.sh. NO hardware: a fake phxctl keeps the registers in files and logs every command; a fake
# `sudo` first on PATH runs only that fake phxctl and logs everything else (the watchdog's reboot timer and its cancel),
# so nothing reaches the machine. Refuses to run as root (the script then calls the reboot timer without sudo).
# Scenarios:
#   warm       C2PMSG_81 != 0: "refused: warm GPU" as the last line, exit 3, no write to the GPU, no watchdog;
#              navi31_up.sh reports "bring-up failed" (exit 1) and shows the refusal line (the daemon matches it)
#   readerr    C2PMSG_81 read fails ("(error) 0x00000000"): refused as unreadable, nothing written
#   openfail   phxctl cannot open PhxBringup (its own message, no register line): refused as unreadable
#   cold       C2PMSG_81 = 0 with STOP_AFTER=sos: the check passes, the watchdog is armed, the PSP bootloader runs
#              and the sOS comes up (the cold path is unchanged up to there); "sOS check: ... (cold)" is printed
set -u
HERE=$(cd "$(dirname "$0")" && pwd); TOOLS=$HERE/../../tools/phxctl
. "$HERE/lib/portable.sh"
[ "$(id -u)" = 0 ] && { echo "bringup_warm_test.sh: do not run as root (the script would call the real reboot timer)"; exit 2; }
PASSN=0; FAILN=0
mk() {   # mk <C2PMSG_81 value> [error|openfail]: a fresh fake home with ~/gpu/phxctl, the firmware names and the fakes
  W=$(mktemp -d "$MOCKTMP/bringupwarm.XXXXXX"); R=$W/regs; mkdir -p "$R" "$W/home/gpu/phxctl" "$W/home/gpu/fw/navi31" "$W/bin"
  cp "$TOOLS/bringup_navi31.sh" "$TOOLS/navi31_up.sh" "$W/home/gpu/phxctl/"
  for f in psp_13_0_0_sos.bin smu_13_0_0.bin sdma_6_0_0.bin gc_11_0_0_pfp.bin gc_11_0_0_me.bin gc_11_0_0_mec.bin \
           gc_11_0_0_mes_2.bin gc_11_0_0_mes1.bin gc_11_0_0_imu.bin gc_11_0_0_rlc.bin; do : > "$W/home/gpu/fw/navi31/$f"; done
  echo "$1" > "$R/0x16091"; echo 0x80000000 > "$R/0x16063"; echo 0x00000000 > "$R/0x1607a"
  [ "${2:-}" = error ] && : > "$R/error"
  [ "${2:-}" = openfail ] && : > "$R/openfail"
  cat > "$W/bin/phxctl" <<EOS
#!/bin/bash
R=$R; echo "\$*" >> "$W/phxctl.log"
case "\$1" in
  regs) [ -e "\$R/openfail" ] && { echo "PhxBringup service not found" >&2; exit 1; }
        shift; for a in "\$@"; do a=\$(printf 0x%05x \$((a))); v=\$(cat "\$R/\$a" 2>/dev/null || echo 0x00000000)
          if [ -e "\$R/error" ]; then echo "\$a: (error) 0x00000000"; else echo "\$a: \$v"; fi; done ;;
  wreg) [ "\$2" = 0x16063 ] && [ "\$((\$3))" = \$((0x20000)) ] && echo 0x00310035 > "\$R/0x16091" ;;   # the SOS command starts the sOS
esac
exit 0
EOS
  cat > "$W/bin/sudo" <<EOS
#!/bin/bash
[ "\${1:-}" = -n ] && shift
if [ "\${1:-}" = "$W/bin/phxctl" ]; then exec "\$@"; fi
echo "\$*" >> "$W/sudo.log"; exit 0
EOS
  chmod +x "$W/bin/phxctl" "$W/bin/sudo"
}
run_up() {   # run navi31_up.sh the way the daemon does, in the fake home
  OUT=$(cd "$W/home/gpu/phxctl" && HOME=$W/home PATH="$W/bin:$PATH" PHXCTL=$W/bin/phxctl bash "$W/home/gpu/phxctl/navi31_up.sh" 2>&1); RC=$?
}
run_bringup() {   # run bringup_navi31.sh alone
  OUT=$(cd "$W/home/gpu/phxctl" && HOME=$W/home PATH="$W/bin:$PATH" PHXCTL=$W/bin/phxctl "$@" bash "$W/home/gpu/phxctl/bringup_navi31.sh" 2>&1); RC=$?
}
res() {   # res <name> <ok> <why>
  if [ "$2" = 1 ]; then PASSN=$((PASSN+1)); echo "PASS  $1"; rm -rf "$W"
  else FAILN=$((FAILN+1)); echo "FAIL  $1:$3"; echo "      kept $W"; fi
}
nowrite() {   # nothing but register reads reached the GPU, and sudo ran nothing else (no reboot timer)
  ! grep -qv '^regs ' "$W/phxctl.log" 2>/dev/null && [ ! -s "$W/sudo.log" ]
}

sc_warm() {
  mk 0x00310035; run_bringup env; ok=1; why=""
  [ "$RC" = 3 ] || { ok=0; why="$why bringup rc $RC!=3;"; }
  [ "$(echo "$OUT" | tail -1 | grep -c '^refused: warm GPU (PSP sOS running, C2PMSG_81=0x00310035)')" = 1 ] || { ok=0; why="$why refusal not the last line;"; }
  nowrite || { ok=0; why="$why wrote to the GPU or armed the watchdog;"; }
  run_up
  [ "$RC" = 1 ] || { ok=0; why="$why navi31_up rc $RC!=1;"; }
  echo "$OUT" | grep -q 'refused: warm GPU' || { ok=0; why="$why navi31_up does not show the refusal;"; }
  echo "$OUT" | grep -q '^bring-up failed$' || { ok=0; why="$why navi31_up did not report bring-up failed;"; }
  echo "$OUT" | grep -q 'navi31 up:\|^sOS check:' && { ok=0; why="$why navi31_up claims the GPU is up or cold;"; }
  nowrite || { ok=0; why="$why navi31_up wrote to the GPU;"; }
  res warm $ok "$why"
}
sc_readerr() {
  mk 0x00000000 error; run_bringup env; ok=1; why=""
  [ "$RC" = 3 ] || { ok=0; why="$why rc $RC!=3;"; }
  echo "$OUT" | tail -1 | grep -q '^refused: warm GPU check failed: C2PMSG_81 unreadable (0x16091: (error)' || { ok=0; why="$why no unreadable refusal;"; }
  nowrite || { ok=0; why="$why wrote to the GPU or armed the watchdog;"; }
  res readerr $ok "$why"
}
sc_openfail() {
  mk 0x00000000 openfail; run_bringup env; ok=1; why=""
  [ "$RC" = 3 ] || { ok=0; why="$why rc $RC!=3;"; }
  echo "$OUT" | tail -1 | grep -q '^refused: warm GPU check failed: C2PMSG_81 unreadable (PhxBringup service not found)' || { ok=0; why="$why no unreadable refusal;"; }
  nowrite || { ok=0; why="$why wrote to the GPU or armed the watchdog;"; }
  res openfail $ok "$why"
}
sc_cold() {
  mk 0x00000000; run_bringup env STOP_AFTER=sos; ok=1; why=""
  [ "$RC" = 0 ] || { ok=0; why="$why rc $RC!=0;"; }
  echo "$OUT" | grep -q 'refused' && { ok=0; why="$why refused a cold GPU;"; }
  grep -q 'shutdown -r +3' "$W/sudo.log" 2>/dev/null || { ok=0; why="$why watchdog not armed;"; }
  grep -q '^wreg 0x16063 0x00020000' "$W/phxctl.log" || { ok=0; why="$why SOS not loaded;"; }
  echo "$OUT" | grep -q 'sOS up' || { ok=0; why="$why sOS did not come up;"; }
  echo "$OUT" | grep -qx 'sOS check: C2PMSG_81=0x00000000 (cold)' || { ok=0; why="$why no sOS check line;"; }
  res cold $ok "$why"
}
sc_warm; sc_readerr; sc_openfail; sc_cold
echo "RESULT: $PASSN passed, $FAILN failed"
[ $FAILN = 0 ]
