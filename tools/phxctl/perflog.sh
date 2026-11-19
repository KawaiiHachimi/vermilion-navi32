#!/bin/bash
# SPDX-License-Identifier: MIT
# perflog.sh <log> [seconds] [interval]: samples the display path's counters (read-only sysctls) into <log>, for
# measuring a boot from another OS afterwards (the XTX machine reads it from Linux with apfs-fuse).
#
# It waits (up to PERF_WAIT seconds, default 300) until the boot daemon's log says the desktop was armed or reverted,
# then writes one line every [interval] seconds (default 10) for [seconds] (default 600): frames presented, how they
# were shown (CPU copy, phxfb flip, GPU present), the CPU copy time per frame, the frame pacing buckets, and at the
# start and end the GPU-present slot layout (debug.phxscan_layout, VermilionBoot b126+) and its latch counters.
# Only named OIDs are read, never a whole tree: some debug OIDs act on the hardware when read (debug.phxedid starts
# a DDC/AUX transfer, debug.phxshot reads the scanned frame through the BAR).
#
# Bash 3.2 (macOS /bin/bash). Test hooks: PERF_SYSCTL (command printing one OID's value), PERF_PHXLOG (the daemon log),
# PERF_SLEEP (sleep command), PERF_NVLOG (the plugin's log, copied at the end when it exists).
set -u
LOG=${1:?usage: perflog.sh <log> [seconds] [interval]}
DUR=${2:-600}; IV=${3:-10}
SYSCTL=${PERF_SYSCTL:-/usr/sbin/sysctl -n}
PHXLOG=${PERF_PHXLOG:-/var/log/phxboot.log}
SLEEP=${PERF_SLEEP:-sleep}
NVLOG=${PERF_NVLOG:-/tmp/nvmtl.log}
WAIT=${PERF_WAIT:-300}

say() { echo "$(date '+%F %T') $*" >> "$LOG"; }
get() { local v; v=$($SYSCTL "$1" 2>/dev/null) || v=""; echo "${v:--}"; }

# the counters sampled every interval, in this order (names without the "debug." prefix)
CNT="nvaccel_iop_ok nvaccel_iop_src_surf nvaccel_iop_copy_us_sum nvaccel_iop_copy_us_max nvaccel_iop_copy_skip
nvaccel_iop_skipped_foreign nvaccel_iop_gap1 nvaccel_iop_gap2 nvaccel_iop_gap3 nvaccel_iop_gap4 nvaccel_iop_late
nvaccel_iop_vsync_waits nvaccel_iop_vsync_late nvaccel_surface_gpu_waits nvaccel_surface_gpu_timeouts
phxfb_flips phxfb_flip_waits phxfb_flip_wait_us_sum phxfb_flip_timeouts"
SETTINGS="phxscan phxscan_latchfix phxfb_flip nvaccel_iop_skipforeign nvaccel_iop_sse nvaccel_iop_vsync nvaccel_surface_gpu_on"

snap_strings() {   # $1 = label
  say "$1 layout: $(get debug.phxscan_layout)"
  say "$1 race: $(get debug.phxscan_race_stat)"
  local s out=""
  for s in $SETTINGS; do out="$out $s=$(get debug.$s)"; done
  say "$1 settings:$out"
}

say "perflog start (pid $$): ${DUR}s every ${IV}s; waiting up to ${WAIT}s for the desktop arm in $PHXLOG"
t=0; armed=""
while [ $t -lt "$WAIT" ]; do
  # the last daemon run in the log: the lines after its "== <date> phxboot(" header
  armed=$(awk '/^== .*phxboot\(/ { buf = "" } { buf = buf "\n" $0 } END { print buf }' "$PHXLOG" 2>/dev/null \
          | grep -E 'flip on|phxfb flip left off|SUCCESS|no desktop arm|reverting|m2_fail|FAIL' | head -1)
  [ -n "$armed" ] && break
  $SLEEP 5; t=$((t + 5))
done
say "arm: ${armed:-not seen after ${WAIT}s, sampling anyway}"
$SLEEP "$IV"
snap_strings "start"
say "columns: t_s frames/s copied/s copy_us_avg copy_us_max skipped_foreign/s gap<1.5f gap<2.5f gap<3.5f gap>=3.5f late flips/s flip_wait_us_avg flip_timeouts gpu_waits gpu_timeouts copy_skip"

prev=""; e=0
while [ $e -le "$DUR" ]; do
  cur=""
  for c in $CNT; do cur="$cur $(get debug.$c)"; done
  if [ -n "$prev" ]; then
    # fields 1..19 = $CNT's order; deltas over the interval, "-" where a counter is missing
    line=$(awk -v iv="$IV" -v e="$e" -v P="$prev" -v C="$cur" '
      function num(x) { return x ~ /^-?[0-9]+$/ }
      function d(i) { return (num(c[i]) && num(p[i])) ? c[i] - p[i] : "-" }
      function r(x) { return (x == "-") ? "-" : sprintf("%.1f", x / iv) }
      function avg(a, b) { return (a != "-" && b != "-" && b > 0) ? int(a / b) : "-" }
      BEGIN {
        split(P, p, " "); split(C, c, " ")
        printf "%d %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s", e, r(d(1)), r(d(2)), avg(d(3), d(2)), c[4], r(d(6)),
               d(7), d(8), d(9), d(10), d(11), r(d(16)), avg(d(18), d(17)), c[19], c[14], c[15], c[5]
      }')
    say "s $line"
  fi
  prev=$cur
  $SLEEP "$IV"; e=$((e + IV))
done
snap_strings "end"
if [ -f "$NVLOG" ]; then
  say "plugin log $NVLOG (gpu present / scan lines):"
  grep -E 'gpu present|scanout|head map' "$NVLOG" 2>/dev/null | tail -40 | sed 's/^/  /' >> "$LOG"
fi
say "perflog end"
