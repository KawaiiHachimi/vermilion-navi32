#!/bin/bash
# SPDX-License-Identifier: MIT
# bootcheck.sh: one read-only report on the current boot, for the restart / sleep regression runs (thread 07).
# Changes nothing. Run any time after the desktop is up:
#
#   bash tools/validation/bootcheck.sh          # most checks
#   sudo bash tools/validation/bootcheck.sh     # also the NVMe's SMART "Unsafe Shutdowns" (smartctl)
#
# Compare two runs line by line: a new report file, a cause other than 5, a FAIL from the daemon, a Sleep Count above
# zero or a higher Unsafe Shutdowns count is a regression to look into.
LOG=/usr/bin/log            # zsh has a `log` builtin; always call the binary
REPORTS=/Library/Logs/DiagnosticReports
PHXLOG=/var/log/phxboot.log

boot=$(sysctl -n kern.boottime | sed -E 's/^\{ sec = ([0-9]+),.*/\1/')
echo "== bootcheck $(date '+%F %T')"
echo "macOS $(sw_vers -productVersion) ($(sw_vers -buildVersion)); booted $(date -r "$boot" '+%F %T'); up $(( ($(date +%s) - boot) / 60 )) min"
echo "boot-args: $(sysctl -n kern.bootargs)"

cause=$($LOG show --style compact --info --start "$(date -r $((boot - 120)) '+%F %T')" \
        --predicate 'eventMessage CONTAINS "Previous shutdown cause"' 2>/dev/null | grep -o -E 'cause: -?[0-9]+' | head -1)
echo "previous shutdown: ${cause:-not found in the log} (5 = normal, 3 = power button held, 0 = power lost; others: a panic or a watchdog)"

echo "reports written since the previous boot started:"
found=0
for f in "$REPORTS"/*; do
    [ -f "$f" ] || continue
    # date a report by the time in its name (analytics rewrites the files later, so mtimes lie), else its birth time
    ts=$(basename "$f" | grep -o -E '20[0-9]{2}-[0-9]{2}-[0-9]{2}-[0-9]{6}' | head -1)
    if [ -n "$ts" ]; then m=$(date -j -f '%Y-%m-%d-%H%M%S' "$ts" +%s 2>/dev/null); else m=$(stat -f %B "$f"); fi
    [ -n "$m" ] || continue
    # the previous boot's shutdown reports are written just before this boot, so look back 10 minutes
    if [ "$m" -ge $((boot - 600)) ]; then echo "  $(date -r "$m" '+%F %T') $(basename "$f")"; found=1; fi
done
[ $found = 0 ] && echo "  none"
ls "$REPORTS" 2>/dev/null | grep -q -i -E '\.panic$|^Kernel|panic' && echo "  ! a panic report exists: ls $REPORTS"
echo "ESP panic logs (OpenCore ApplePanic): $(ls /Volumes/*/panic-*.txt 2>/dev/null | wc -l | tr -d ' ')"

if [ -r "$PHXLOG" ]; then
    hdr=$(grep -n '^== .*phxboot' "$PHXLOG" | tail -1 | cut -d: -f1)
    res=$(tail -n +"${hdr:-1}" "$PHXLOG" | grep -E 'SUCCESS|FAIL|safe desktop|GOP' | tail -1)
    echo "boot daemon: $(sed -n "${hdr:-1}p" "$PHXLOG" | cut -c1-60) -> ${res:-no result yet}"
fi

pm=$(pmset -g)
echo "pmset: sleep $(awk '$1=="sleep"{print $2}' <<<"$pm"), displaysleep $(awk '$1=="displaysleep"{print $2}' <<<"$pm"), hibernatemode $(awk '$1=="hibernatemode"{print $2}' <<<"$pm"), SleepDisabled $(ioreg -rd1 -c IOPMrootDomain | awk -F'= ' '/"SleepDisabled"/{print $2}')"
echo "pmset stats: $(pmset -g stats 2>/dev/null | tr '\n' ' ')"
echo "display: $(system_profiler SPDisplaysDataType 2>/dev/null | awk -F': ' '/UI Looks like|Framebuffer Depth/{printf "%s; ", $2}')"

SMC=$(command -v smcread || echo ~/.local/bin/smcread)
if [ -x "$SMC" ]; then
    py=$(dirname "$0")/../sensors/cpusensors.py
    [ -f "$py" ] && echo "CPU: $(python3 "$py" 2>/dev/null)"
fi
if [ "$(id -u)" = 0 ]; then
    SC=$(command -v smartctl || ls /Users/*/.local/sbin/smartctl /Users/*/.local/bin/smartctl 2>/dev/null | head -1)
    [ -n "$SC" ] && echo "NVMe: $("$SC" -A disk0 2>/dev/null | awk -F': *' '/Unsafe Shutdowns|Power Cycles|^Temperature:/{printf "%s %s; ", $1, $2}')"
else
    echo "NVMe: run with sudo for SMART Unsafe Shutdowns"
fi
