#!/bin/bash
# SPDX-License-Identifier: MIT
# wifilog.sh <log> [seconds] [interval]: snapshots the Wi-Fi driver's state into <log>, for reading a boot from
# another OS afterwards (the XTX machine reads it from Linux with apfs-fuse). Started for one boot by
# perflog_install.sh with PERF_NAME=wifi, from a run-once.sh on the EFI partition (esp_flags.sh).
#
# Every [interval] seconds (default 60) for [seconds] (default 600): is AirportMtkWlan loaded, how many driver
# objects exist, which device is the Wi-Fi port, its power, link status and whether it has an IPv4 address (yes/no
# only). At the start and the end: the card's fields from system_profiler (card type, firmware, status, country,
# PHY mode, channel, rate, signal/noise, security), and the driver's own MtkDebugLog ring (IORegistry). Every MAC
# address is masked, and this script writes no SSID, BSSID or IP address of its own; the driver's ring may still name
# networks, so the log stays on the machine (quote it redacted).
#
# Bash 3.2 (macOS /bin/bash). Test hooks: WIFI_PATH (PATH for the tools: stubs first), WIFI_SLEEP (sleep command).
set -u
LOG=${1:?usage: wifilog.sh <log> [seconds] [interval]}
DUR=${2:-600}; IV=${3:-60}
PATH=${WIFI_PATH:-/usr/bin:/bin:/usr/sbin:/sbin}; export PATH
SLEEP=${WIFI_SLEEP:-sleep}

mask() { sed -E 's/[0-9a-fA-F]{1,2}(:[0-9a-fA-F]{1,2}){5}/xx:xx:xx:xx:xx:xx/g'; }
say() { echo "$(date '+%F %T') $*" | mask >> "$LOG"; }
quote() { mask | sed 's/^/  /' >> "$LOG"; }

wifidev() {   # the device of the "Wi-Fi" hardware port (en0, en1, ...), empty if none
  networksetup -listallhardwareports 2>/dev/null | awk '/^Hardware Port: (Wi-Fi|AirPort)$/ { f = 1; next }
    f && /^Device: / { print $2; exit }'
}

status() {   # one line
  local loaded n dev pw st ip
  loaded=$(kmutil showloaded 2>/dev/null | grep -oE 'com\.flxxxxddd\.AirportMtkWlan \([^)]*\)' | head -1)
  n=$(ioclasscount AirportItlwm 2>/dev/null | awk '$NF ~ /^[0-9]+$/ {print $NF; exit}')   # "<no such class>" = 0
  dev=$(wifidev)
  if [ -n "$dev" ]; then
    pw=$(networksetup -getairportpower "$dev" 2>/dev/null | awk '{print $NF}')
    st=$(ifconfig "$dev" 2>/dev/null | awk '/status:/ {print $2}')
    if [ -n "$(ipconfig getifaddr "$dev" 2>/dev/null)" ]; then ip=yes; else ip=no; fi
  fi
  say "loaded=${loaded:-no} AirportItlwm=${n:-0} port=${dev:-none} power=${pw:--} link=${st:--} ipv4=${ip:--}"
}

card() {   # $1 = label
  say "$1 system_profiler (card fields only):"
  system_profiler SPAirPortDataType 2>/dev/null | awk '
    /Other Local Wi-Fi Networks:/ { exit }
    /^ *(Card Type|Firmware Version|Status|Country Code|PHY Mode|Channel|Transmit Rate|Signal \/ Noise|Security|Supported PHY Modes|Locale):/ {
      sub(/^ */, ""); print }' | quote
}

mtklog() {   # the driver's MtkDebugLog property (as the driver's tools/mtklog.sh reads it)
  say "MtkDebugLog:"
  ioreg -l -w0 -r -c IOPCIDevice 2>/dev/null | awk '/"MtkDebugLog" = "/ { f = 1 } f { if (n++ && /^ *"[A-Za-z-]+" = /) exit; print }' \
    | grep -vE '"(IOMACAddress|mac-address|ssid|bssid)"' | quote
}

say "wifilog start (pid $$): ${DUR}s every ${IV}s"
$SLEEP "$IV"
status; card start; mtklog
e=$IV
while [ $((e + IV)) -le "$DUR" ]; do
  $SLEEP "$IV"; e=$((e + IV))
  status
done
card end; mtklog
say "wifilog end"
