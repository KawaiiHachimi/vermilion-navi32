#!/bin/bash
# SPDX-License-Identifier: MIT
# perflog_test.sh: tools/phxctl/perflog.sh (the display-path counter logger) against a fake sysctl and a fake daemon
# log, and tools/phxctl/perflog_install.sh against a fake root and a fake launchctl. No root needed, nothing outside
# a temporary directory is touched. Portable (macOS and Linux).
set -u
HERE=$(cd "$(dirname "$0")" && pwd); P=$HERE/../../tools/phxctl/perflog.sh; I=$HERE/../../tools/phxctl/perflog_install.sh
T=$(mktemp -d "${TMPDIR:-/tmp}/perflog.XXXXXX"); PASSN=0; FAILN=0
ALLOWED="phxscan_layout phxscan_race_stat phxscan phxscan_latchfix phxfb_flip nvaccel_iop_skipforeign nvaccel_iop_sse
nvaccel_iop_vsync nvaccel_surface_gpu_on nvaccel_iop_ok nvaccel_iop_src_surf nvaccel_iop_copy_us_sum
nvaccel_iop_copy_us_max nvaccel_iop_copy_skip nvaccel_iop_skipped_foreign nvaccel_iop_gap1 nvaccel_iop_gap2
nvaccel_iop_gap3 nvaccel_iop_gap4 nvaccel_iop_late nvaccel_iop_vsync_waits nvaccel_iop_vsync_late
nvaccel_surface_gpu_waits nvaccel_surface_gpu_timeouts phxfb_flips phxfb_flip_waits phxfb_flip_wait_us_sum
phxfb_flip_timeouts"
# fake sysctl: sample k (counted at each nvaccel_iop_ok read) = 600k frames, all copied at 20 ms, all flipped, half of
# them waiting 5 ms for the latch; strings for the two string OIDs; every OID asked is logged; $T/missing lists OIDs
# that do not exist (an older kext)
mkdir -p "$T/bin"
cat >"$T/bin/sysctl" <<EOF
#!/bin/bash
o=\${1#debug.}; echo "\$o" >>"$T/asked"
grep -qx "\$o" "$T/missing" 2>/dev/null && { echo "sysctl: unknown oid '\$1'" >&2; exit 1; }
k=\$(cat "$T/k" 2>/dev/null || echo 0)
case "\$o" in
  nvaccel_iop_ok) k=\$((k + 1)); echo \$k >"$T/k"; echo \$((600 * k));;
  nvaccel_iop_src_surf|phxfb_flips|nvaccel_iop_gap1) echo \$((600 * k));;
  nvaccel_iop_copy_us_sum) echo \$((600 * k * 20000));;
  nvaccel_iop_copy_us_max) echo 31000;;
  phxfb_flip_waits) echo \$((300 * k));;
  phxfb_flip_wait_us_sum) echo \$((300 * k * 5000));;
  phxscan_layout) echo "plane 2560x1440 pitch 2560 px: 14745600 bytes -> large (256/288/320 MiB)";;
  phxscan_race_stat) echo "earliest lag 0, latchfix 0";;
  phxfb_flip) echo 1;;
  *) echo 0;;
esac
EOF
chmod +x "$T/bin/sysctl"
run() {   # run <dur> <iv>
  rm -f "$T/k" "$T/asked" "$T/log"
  PERF_SYSCTL="$T/bin/sysctl" PERF_PHXLOG="$T/phxboot.log" PERF_SLEEP=true PERF_NVLOG="$T/nvmtl.log" PERF_WAIT=${PERF_WAIT:-20} \
    bash "$P" "$T/log" "$1" "$2" >"$T/out" 2>&1; }
check() {   # check <name> <cond> ...
  local name=$1 ok=1 c; shift
  for c in "$@"; do eval "$c" || { ok=0; echo "    failed: $c"; }; done
  if [ $ok = 1 ]; then PASSN=$((PASSN+1)); echo "PASS  $name"; else FAILN=$((FAILN+1)); echo "FAIL  $name"; sed 's/^/    /' "$T/log" "$T/out" 2>/dev/null; fi; }
only_allowed() { local o; for o in $(sort -u "$T/asked"); do echo "$ALLOWED" | tr ' ' '\n' | grep -qx "$o" || { echo "    asked $o"; return 1; }; done; }
samples() { grep -c ' s ' "$T/log"; }

printf '== 2026-11-19 19:59:48 phxboot(navi31, Option 1b)\nsquat boot: 1\nflip on (debug.phxfb_flip=1)\n' >"$T/phxboot.log"
run 30 10
check copy_and_flip 'grep -q "arm: flip on" "$T/log"' '[ "$(samples)" = 3 ]' \
  'grep -q " s 10 60.0 60.0 20000 31000 0.0 600 0 0 0 0 60.0 5000 0 0 0 0$" "$T/log"' \
  'grep -q "start layout: plane 2560x1440 .* large" "$T/log"' 'grep -q "end layout:" "$T/log"' \
  'grep -q "start settings: phxscan=0 .*phxfb_flip=1" "$T/log"' 'grep -q "perflog end" "$T/log"' only_allowed \
  '! grep -qE "phxshot|phxedid" "$T/asked"'

# only the daemon's last run counts: an old "flip on" before a new header that has not armed yet
printf '== 2026-11-18 19:00:00 phxboot(navi31, Option 1b)\nflip on (debug.phxfb_flip=1)\n== 2026-11-19 20:00:00 phxboot(navi31, Option 1b)\nsquat boot: 1\n' >"$T/phxboot.log"
PERF_WAIT=10 run 10 10
check last_run_only 'grep -q "arm: not seen after 10s, sampling anyway" "$T/log"' '[ "$(samples)" = 1 ]'

# GPU present boot: the daemon's line, frames skipped by the copy
printf '== 2026-11-19 20:00:00 phxboot(navi31, Option 1b)\ngpu present allowed (debug.phxscan=1)\nphxfb flip left off (GPU present owns HUBP0)\n' >"$T/phxboot.log"
run 10 10
check gpu_present 'grep -q "arm: phxfb flip left off" "$T/log"'

# an older kext without the b126 OID, and counters missing: "-" in their place, no crash
printf 'phxscan_layout\nphxfb_flip_waits\nphxfb_flip_wait_us_sum\n' >"$T/missing"
run 10 10
check missing_oids 'grep -q "start layout: -$" "$T/log"' 'grep -q " s 10 60.0 60.0 20000 31000 0.0 600 0 0 0 0 60.0 - 0 0 0 0$" "$T/log"'
rm -f "$T/missing"

# the plugin's log is quoted at the end when it exists
echo "pid 1 NVMTL: ws gpu present: ON - 2560x1440, console MC 0x8000000000, three VRAM slots registered" >"$T/nvmtl.log"
run 10 10
check plugin_log 'grep -q "  pid 1 NVMTL: ws gpu present: ON - 2560x1440" "$T/log"'

# perflog_install.sh: copies the logger, writes the plist outside /Library/LaunchDaemons, bootstraps it
R=$T/root; mkdir -p "$R"
cat >"$T/bin/launchctl" <<EOF
#!/bin/bash
echo "\$*" >>"$T/lc"; [ "\$1" = bootstrap ] && [ -e "$T/lcfail" ] && { echo "Bootstrap failed: 5: Input/output error"; exit 5; }; exit 0
EOF
chmod +x "$T/bin/launchctl"
inst() { rm -f "$T/lc"; PERF_ROOT="$R" PERF_LAUNCHCTL="$T/bin/launchctl" bash "$I" "$@" >"$T/out" 2>&1; echo $? >"$T/rc"; : >"$T/log"; }
inst "$P" 600 10
PL=$R/var/db/vermilion/org.vermilion.perflog.plist
check install_ok '[ "$(cat "$T/rc")" = 0 ]' 'cmp -s "$P" "$R/var/db/vermilion/perflog.sh"' '[ -f "$PL" ]' \
  'grep -q "<string>$R/var/db/vermilion/perflog.sh</string><string>$R/var/log/vermilion-perf-[0-9-]*.log</string><string>600</string><string>10</string>" "$PL"' \
  'grep -q "^bootout system/org.vermilion.perflog" "$T/lc"' 'grep -q "^bootstrap system $PL$" "$T/lc"' \
  '[ ! -e "$R/Library/LaunchDaemons" ]' 'grep -q "perflog: started" "$T/out"' \
  '! command -v plutil >/dev/null || plutil -lint "$PL" >/dev/null' '! grep -q EnvironmentVariables "$PL"'
PERF_WAIT=7 PERF_PHXLOG=/x/phxboot.log inst "$P" 20 5
check install_env '[ "$(cat "$T/rc")" = 0 ]' \
  'grep -q "<key>EnvironmentVariables</key><dict><key>PERF_WAIT</key><string>7</string><key>PERF_PHXLOG</key><string>/x/phxboot.log</string></dict>" "$PL"' \
  '! command -v plutil >/dev/null || plutil -lint "$PL" >/dev/null'
touch "$T/lcfail"; inst "$P"
check install_bootstrap_fails '[ "$(cat "$T/rc")" = 1 ]' 'grep -q "bootstrap failed; not started" "$T/out"'
rm -f "$T/lcfail"; inst "$P" 60s 10
check install_bad_args '[ "$(cat "$T/rc")" = 2 ]' '[ ! -e "$T/lc" ]'
inst "$T/nonexistent"
check install_no_src '[ "$(cat "$T/rc")" = 1 ]' 'grep -q "missing" "$T/out"'

rm -rf "$T"
echo "perflog_test: $PASSN passed, $FAILN failed"
[ $FAILN = 0 ]
