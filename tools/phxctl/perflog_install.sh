#!/bin/bash
# SPDX-License-Identifier: MIT
# perflog_install.sh <perflog.sh> [seconds] [interval]: runs perflog.sh once, for this boot only, as its own launchd job
# (root). Meant for a run-once.sh staged on the EFI partition (esp_flags.sh): the hook runs inside the boot daemon's
# job, whose leftover processes launchd kills when the daemon exits, and the EFI partition is unmounted right after
# the hook. So the logger is copied to $DIR first and started with `launchctl bootstrap` from a plist that also lives
# in $DIR, not in /Library/LaunchDaemons: launchd never loads it again at a later boot.
# The log goes to /var/log/vermilion-perf-<date>.log.
#
# Test hooks: PERF_ROOT (prefix for every path, also inside the plist: a fake root), PERF_LAUNCHCTL (the launchctl
# command); PERF_WAIT and PERF_PHXLOG, when set, are passed on to the job (perflog.sh).
set -u
SRC=${1:?usage: perflog_install.sh <perflog.sh> [seconds] [interval]}
DUR=${2:-600}; IV=${3:-10}
R=${PERF_ROOT:-}
LAUNCHCTL=${PERF_LAUNCHCTL:-/bin/launchctl}
DIR=$R/var/db/vermilion
LABEL=org.vermilion.perflog
case "$DUR$IV" in *[!0-9]*) echo "perflog: seconds and interval must be numbers"; exit 2;; esac
[ -f "$SRC" ] || { echo "perflog: $SRC missing"; exit 1; }
mkdir -p "$DIR" "$R/var/log" || exit 1
cp "$SRC" "$DIR/perflog.sh" && chmod 755 "$DIR/perflog.sh" || { echo "perflog: copy failed"; exit 1; }
LOG=$R/var/log/vermilion-perf-$(date +%Y%m%d-%H%M%S).log
PL=$DIR/$LABEL.plist
ENV=""
[ -n "${PERF_WAIT:-}" ] && ENV="$ENV<key>PERF_WAIT</key><string>$PERF_WAIT</string>"
[ -n "${PERF_PHXLOG:-}" ] && ENV="$ENV<key>PERF_PHXLOG</key><string>$PERF_PHXLOG</string>"
[ -n "$ENV" ] && ENV="<key>EnvironmentVariables</key><dict>$ENV</dict>"
cat > "$PL" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>$LABEL</string>
<key>ProgramArguments</key><array><string>/bin/bash</string><string>$DIR/perflog.sh</string><string>$LOG</string><string>$DUR</string><string>$IV</string></array>
<key>RunAtLoad</key><true/>$ENV
<key>StandardOutPath</key><string>$LOG.out</string><key>StandardErrorPath</key><string>$LOG.out</string>
</dict></plist>
EOF
chmod 644 "$PL"
"$LAUNCHCTL" bootout system/$LABEL >/dev/null 2>&1   # a job of that label left from an earlier run this boot
if "$LAUNCHCTL" bootstrap system "$PL" 2>&1; then
  echo "perflog: started ($LABEL, ${DUR}s every ${IV}s) -> $LOG"
else
  echo "perflog: launchctl bootstrap failed; not started"; exit 1
fi
