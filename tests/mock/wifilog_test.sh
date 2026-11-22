#!/bin/bash
# SPDX-License-Identifier: MIT
# wifilog_test.sh: tools/phxctl/wifilog.sh (the Wi-Fi state logger) against stub macOS tools, and
# tools/phxctl/perflog_install.sh with PERF_NAME=wifi against a fake root and a fake launchctl. No root needed,
# nothing outside a temporary directory is touched. Portable (macOS and Linux).
set -u
HERE=$(cd "$(dirname "$0")" && pwd); W=$HERE/../../tools/phxctl/wifilog.sh; I=$HERE/../../tools/phxctl/perflog_install.sh
T=$(mktemp -d "${TMPDIR:-/tmp}/wifilog.XXXXXX"); PASSN=0; FAILN=0
B=$T/bin; mkdir -p "$B"
stub() { printf '#!/bin/bash\n%s\n' "$2" >"$B/$1"; chmod +x "$B/$1"; }
# $T/state: "up" = driver loaded and associated, "none" = no driver (no Wi-Fi port)
stub networksetup 'case "$(cat '"$T"'/state)" in none|loadednoattach) printf "Hardware Port: Ethernet\nDevice: en0\nEthernet Address: 3c:7c:3f:11:22:33\n"; exit 0;; esac
case "$1" in
  -listallhardwareports) printf "Hardware Port: Ethernet\nDevice: en0\nEthernet Address: 3c:7c:3f:11:22:33\n\nHardware Port: Wi-Fi\nDevice: en1\nEthernet Address: a4:5e:60:aa:bb:cc\n";;
  -getairportpower) echo "Wi-Fi Power ($2): On";;
esac'
stub kmutil 'st=$(cat '"$T"'/state); [ "$st" = none ] && exit 0
if [ "$st" = mtkw ]; then echo "  172    0 0xffffff7f9b000000 0x2f3000   0x2f3000   com.dinovn.MtkWirelessWifi (1.1.1) 2B3C4D5E-0000-0000-0000-000000000000 <38 21 7 6 5 3 1>"
else echo "  171    0 0xffffff7f9a000000 0x29b000   0x29b000   com.flxxxxddd.AirportMtkWlan (2.1.6) 1A2B3C4D-0000-0000-0000-000000000000 <38 21 7 6 5 3 1>"; fi
[ "${1:-}" = showloaded ] && [ "${2:-}" = --list-only ] && echo "  60    3 0xffffff7f88000000 0x1000 0x1000 com.apple.iokit.IO80211Family (1600.1) 3C4D5E6F-0000 <7 6 5 3 1>"'
stub ioclasscount 'st=$(cat '"$T"'/state)   # as macOS 26 prints it: "<no such class>" when no loaded kext defines it
case "$st:$1" in
  up:AirportItlwm|up:IOPCIEDeviceWrapper|mtkw:AirportMtkWlan|mtkw:MtkPCIEDeviceWrapper|mtkw:MtkWirelessWifi) echo "$1 = 1";;
  loadednoattach:AirportItlwm|loadednoattach:IOPCIEDeviceWrapper) echo "$1 = 0";;
  *) echo "$1 = <no such class>";;
esac'
stub ifconfig 'printf "%s: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500\n\tether a4:5e:60:aa:bb:cc\n\tinet 192.168.1.23 netmask 0xffffff00 broadcast 192.168.1.255\n\tstatus: active\n" "$1"'
stub ipconfig 'echo 192.168.1.23'
stub system_profiler 'if [ "$1" = SPPCIDataType ]; then cat <<X
PCI:

    Ethernet Controller:

      Type: Ethernet Controller
      Driver Installed: No
      Vendor ID: 0x8086
      Device ID: 0x15f3

    Network controller:

      Type: Network controller
      Driver Installed: No
      MSI: Yes
      Bus: PCI
      Vendor ID: 0x14c3
      Device ID: 0x0616

    Display:

      Vendor ID: 0x1002
X
exit 0; fi
cat <<X
Wi-Fi:
      Software Versions:
          CoreWLAN: 16.0 (1657)
      Interfaces:
        en1:
          Card Type: Wi-Fi  (0x14C3, 0x616)
          Firmware Version: mwx 2.1.6
          MAC Address: a4:5e:60:aa:bb:cc
          Locale: FCC
          Country Code: DE
          Supported PHY Modes: 802.11 a/b/g/n/ac/ax
          Status: Connected
          Current Network Information:
            HomeNet-5G:
              PHY Mode: 802.11ax
              BSSID: 11:22:33:44:55:66
              Channel: 36 (5GHz, 80MHz)
              Security: WPA2 Personal
              Signal / Noise: -48 dBm / -92 dBm
              Transmit Rate: 864
          Other Local Wi-Fi Networks:
            Neighbour:
              PHY Mode: 802.11n
              Channel: 6 (2GHz, 20MHz)
              Security: WPA2 Personal
X'
stub ioreg 'case "$*" in
  *"-n DP58"*) printf "+-o DP58@B  <class IOPCIDevice, registered>\n  | {\n  |   \"IOMACAddress\" = <a45e60aabbcc>\n  |   \"vendor-id\" = <c3140000>\n  | }\n  +-o EP00@0  <class IOPCIDevice, registered, matched>\n"; exit 0;;
  *"-c MtkWirelessWifi"*) printf "+-o MtkWirelessWifi  <class MtkWirelessWifi>\n    \"RustSelfTestFailed\" = 0\n"; exit 0;;
esac
cat <<X
    | |   |   "IOPCIPrimaryMatch" = "0x061614c3"
    | |   |   "MtkDebugLog" = "[1.2] mwx0: attach hardware type=MT7922\n[3.4] awdl0 MAC 02:11:22:33:44:55\n"
    | |   |   "mac-address" = <a45e60aabbcc>
    | |   |   "IOName" = "pci14c3,616"
X'
stub dmesg 'echo "IOPCIEDeviceWrapper::super start succeed"; echo "MtkWirelessWifi: RustCore self-test ok, mac a4:5e:60:aa:bb:cc"; echo "unrelated line"'
run() {   # run <dur> <iv>
  rm -f "$T/log" "$T/log.ioreg"
  WIFI_PATH="$B:$PATH" WIFI_SLEEP=true bash "$W" "$T/log" "$1" "$2" >"$T/out" 2>&1; echo $? >"$T/rc"; }
check() {   # check <name> <cond> ...
  local name=$1 ok=1 c; shift
  for c in "$@"; do eval "$c" || { ok=0; echo "    failed: $c"; }; done
  if [ $ok = 1 ]; then PASSN=$((PASSN+1)); echo "PASS  $name"; else FAILN=$((FAILN+1)); echo "FAIL  $name"; sed 's/^/    /' "$T/log" "$T/out" 2>/dev/null; fi; }
lines() { grep -c ' loaded=' "$T/log"; }

echo up >"$T/state"; run 300 60
check associated '[ "$(cat "$T/rc")" = 0 ]' '[ "$(lines)" = 5 ]' \
  'grep -q " loaded=com.flxxxxddd.AirportMtkWlan (2.1.6) drivers=1 port=en1 power=On link=active ipv4=yes$" "$T/log"' \
  'grep -q "^  Card Type: Wi-Fi  (0x14C3, 0x616)$" "$T/log"' 'grep -q "^  Firmware Version: mwx 2.1.6$" "$T/log"' \
  'grep -q "^  Status: Connected$" "$T/log"' 'grep -q "^  Channel: 36 (5GHz, 80MHz)$" "$T/log"' \
  '[ "$(grep -c "^  Channel:" "$T/log")" = 2 ]' 'grep -q "attach hardware type=MT7922" "$T/log"' \
  'grep -q "start system_profiler" "$T/log"' 'grep -q "end system_profiler" "$T/log"' 'grep -q "wifilog end" "$T/log"' \
  '[ "$(grep -c " classes:" "$T/log")" = 3 ]' 'grep -q "third classes:" "$T/log"' \
  'grep -q "start classes: AirportItlwm=1 IOPCIEDeviceWrapper=1 AirportMtkWlan=<no such class>" "$T/log"' \
  'grep -q "^ *Vendor ID: 0x14c3$" "$T/log"' '! grep -q "0x8086\|0x1002" "$T/log"' 'grep -q "^ *Driver Installed: No$" "$T/log"' \
  'grep -q "com.apple.iokit.IO80211Family" "$T/log"' 'grep -q "super start succeed" "$T/log"' '! grep -q "unrelated line" "$T/log"' \
  'grep -q "^+-o DP58@B" "$T/log.ioreg"' 'grep -q "vendor-id" "$T/log.ioreg"' 'grep -q "RustSelfTestFailed" "$T/log.ioreg"' \
  '[ "$(grep -c "^== .*ioreg -l -w0 -r -n DP58" "$T/log.ioreg")" = 3 ]'
check no_personal_data '! grep -qiE "a4:5e:60|3c:7c:3f|11:22:33:44|02:11:22" "$T/log" "$T/log.ioreg"' '! grep -qi "a45e60" "$T/log.ioreg"' '! grep -q "192\.168" "$T/log"' \
  '! grep -qE "HomeNet|Neighbour|BSSID" "$T/log"' '! grep -q "mac-address" "$T/log"' 'grep -q "xx:xx:xx:xx:xx:xx" "$T/log"'

echo none >"$T/state"; run 60 60
check no_driver '[ "$(cat "$T/rc")" = 0 ]' '[ "$(lines)" = 1 ]' \
  'grep -q " loaded=no drivers=0 port=none power=- link=- ipv4=-$" "$T/log"'

# boot 1 (2026-11-19): AirportMtkWlan loaded, its classes exist, no instance, no Wi-Fi port
echo loadednoattach >"$T/state"; run 60 60
check loaded_not_attached '[ "$(cat "$T/rc")" = 0 ]' \
  'grep -q " loaded=com.flxxxxddd.AirportMtkWlan (2.1.6) drivers=0 port=none power=- link=- ipv4=-$" "$T/log"' \
  'grep -q "start classes: AirportItlwm=0 IOPCIEDeviceWrapper=0 AirportMtkWlan=<no such class>" "$T/log"'

# MtkWirelessWifi attached
echo mtkw >"$T/state"; run 120 60
check mtkwireless '[ "$(cat "$T/rc")" = 0 ]' \
  'grep -q " loaded=com.dinovn.MtkWirelessWifi (1.1.1) drivers=1 port=en1 power=On link=active ipv4=yes$" "$T/log"' \
  'grep -q "MtkPCIEDeviceWrapper=1 MtkWirelessWifi=1" "$T/log"' 'grep -q "RustCore self-test ok, mac xx:xx:xx:xx:xx:xx" "$T/log"'

run 30 60
check shorter_than_interval '[ "$(cat "$T/rc")" = 0 ]' '[ "$(lines)" = 1 ]' 'grep -q "wifilog end" "$T/log"'

# perflog_install.sh with PERF_NAME=wifi: its own script, label and log name; the default stays perflog
R=$T/root; mkdir -p "$R"
cat >"$B/launchctl" <<X
#!/bin/bash
echo "\$*" >>"$T/lc"; exit 0
X
chmod +x "$B/launchctl"
inst() { rm -f "$T/lc"; PERF_ROOT="$R" PERF_LAUNCHCTL="$B/launchctl" bash "$I" "$@" >"$T/out" 2>&1; echo $? >"$T/rc"; : >"$T/log"; }
PERF_NAME=wifi inst "$W" 600 60
PL=$R/var/db/vermilion/org.vermilion.wifilog.plist
check install_wifi '[ "$(cat "$T/rc")" = 0 ]' 'cmp -s "$W" "$R/var/db/vermilion/wifilog.sh"' '[ -f "$PL" ]' \
  'grep -q "<string>$R/var/db/vermilion/wifilog.sh</string><string>$R/var/log/vermilion-wifi-[0-9-]*.log</string><string>600</string><string>60</string>" "$PL"' \
  'grep -q "^bootstrap system $PL$" "$T/lc"' '[ ! -e "$R/var/db/vermilion/perflog.sh" ]' \
  '! command -v plutil >/dev/null || plutil -lint "$PL" >/dev/null'
PERF_NAME=Wifi1 inst "$W"
check install_bad_name '[ "$(cat "$T/rc")" = 2 ]' '[ ! -e "$T/lc" ]'

rm -rf "$T"
echo "wifilog_test: $PASSN passed, $FAILN failed"
[ $FAILN = 0 ]
