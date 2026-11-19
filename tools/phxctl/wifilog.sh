#!/bin/bash
# SPDX-License-Identifier: MIT
# wifilog.sh <log> [seconds] [interval]: snapshots the Wi-Fi driver's state into <log>, for reading a boot from
# another OS afterwards (the XTX machine reads it from Linux with apfs-fuse). Started for one boot by
# perflog_install.sh with PERF_NAME=wifi, from a run-once.sh on the EFI partition (esp_flags.sh).
#
# Every [interval] seconds (default 60) for [seconds] (default 600): which MediaTek Wi-Fi kext is loaded
# (AirportMtkWlan or MtkWirelessWifi), how many controller objects exist (AirportItlwm, AirportMtkWlan), which
# device is the Wi-Fi port, its power, link status and whether it has an IPv4 address (yes/no only). At the start and the end: the card's fields from system_profiler (card type, firmware, status, country,
# PHY mode, channel, rate, signal/noise, security), AirportMtkWlan's MtkDebugLog ring, and a deep snapshot (also
# after the third interval): raw class counts, the card in SPPCIDataType ("Driver Installed"), the drivers' dmesg
# lines, and the IORegistry of the card's port (DP58) and of MtkWirelessWifi into <log>.ioreg. Every MAC
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

DRIVERS='com\.(flxxxxddd\.AirportMtkWlan|dinovn\.MtkWirelessWifi) \([^)]*\)'
CLASSES="AirportItlwm IOPCIEDeviceWrapper AirportMtkWlan MtkPCIEDeviceWrapper MtkWirelessWifi"

count() {   # instances of class $1; "<no such class>" (class not in the kernel) = 0
  ioclasscount "$1" 2>/dev/null | awk '$NF ~ /^[0-9]+$/ {print $NF; exit}'
}

status() {   # one line
  local loaded a b dev pw st ip
  loaded=$(kmutil showloaded 2>/dev/null | grep -oE "$DRIVERS" | tr '\n' ' ' | sed 's/ $//')
  a=$(count AirportItlwm); b=$(count AirportMtkWlan)   # the controller class of AirportMtkWlan.kext / MtkWirelessWifi
  dev=$(wifidev)
  if [ -n "$dev" ]; then
    pw=$(networksetup -getairportpower "$dev" 2>/dev/null | awk '{print $NF}')
    st=$(ifconfig "$dev" 2>/dev/null | awk '/status:/ {print $2}')
    if [ -n "$(ipconfig getifaddr "$dev" 2>/dev/null)" ]; then ip=yes; else ip=no; fi
  fi
  say "loaded=${loaded:-no} drivers=$(( ${a:-0} + ${b:-0} )) port=${dev:-none} power=${pw:--} link=${st:--} ipv4=${ip:--}"
}

card() {   # $1 = label
  say "$1 system_profiler (card fields only):"
  system_profiler SPAirPortDataType 2>/dev/null | awk '
    /Other Local Wi-Fi Networks:/ { exit }
    /^ *(Card Type|Firmware Version|Status|Country Code|PHY Mode|Channel|Transmit Rate|Signal \/ Noise|Security|Supported PHY Modes|Locale):/ {
      sub(/^ */, ""); print }' | quote
}

mtklog() {   # AirportMtkWlan's MtkDebugLog property (as that driver's tools/mtklog.sh reads it)
  say "MtkDebugLog:"
  ioreg -l -w0 -r -c IOPCIDevice 2>/dev/null | awk '/"MtkDebugLog" = "/ { f = 1 }
    f { if (n++ && /^[ |]*"[A-Za-z-]+" = /) exit; print }' | grep -vE '"(IOMACAddress|mac-address|ssid|bssid)"' | quote
}

deep() {   # $1 = label: why a driver did or did not attach. The IORegistry part goes to $LOG.ioreg.
  local c out=""
  for c in $CLASSES; do out="$out $c=$(ioclasscount "$c" 2>/dev/null | sed 's/^[^=]*= *//')"; done
  say "$1 classes:$out"
  say "$1 kmutil showloaded:"; kmutil showloaded --list-only 2>/dev/null | grep -iE 'mtk|80211|skywalk' | quote
  say "$1 system_profiler SPPCIDataType (the MediaTek card):"
  system_profiler SPPCIDataType 2>/dev/null | awk '
    /^    [^ ].*:$/ { if (keep) printf "%s", buf; buf = ""; keep = 0 }
    { buf = buf $0 "\n" } /Vendor ID: 0x14c3/ { keep = 1 }
    END { if (keep) printf "%s", buf }' | quote
  say "$1 dmesg (driver lines):"
  dmesg 2>/dev/null | grep -iE 'mtk|mwx|itlwm|80211|wrapper|14c3|rustcore|rustself' | tail -80 | quote
  { echo "== $(date '+%F %T') $1: ioreg -l -w0 -r -n DP58 (the Wi-Fi card's port; MAC/SSID keys dropped)"
    ioreg -l -w0 -r -n DP58 2>/dev/null
    echo "== $1: ioreg -l -w0 -r -c MtkWirelessWifi"
    ioreg -l -w0 -r -c MtkWirelessWifi 2>/dev/null
  } | grep -vE '"(IOMACAddress|mac-address|ssid|bssid|SSID|BSSID)"' | mask >> "$LOG.ioreg"
  say "$1 IORegistry -> $LOG.ioreg"
}

say "wifilog start (pid $$): ${DUR}s every ${IV}s"
$SLEEP "$IV"
status; card start; mtklog; deep start
e=$IV; k=1
while [ $((e + IV)) -le "$DUR" ]; do
  $SLEEP "$IV"; e=$((e + IV)); k=$((k + 1))
  status
  [ $k = 3 ] && deep third   # the card may attach late (it was published 21 s into boot 1)
done
card end; mtklog; deep end
say "wifilog end"
