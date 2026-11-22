#!/bin/bash
# SPDX-License-Identifier: MIT
# linux_pci_capture_test.sh: tools/capture/linux_pci_capture.sh against a fake sysfs/procfs and stub lspci/dmesg.
# No root, nothing outside a temporary directory is touched. Portable (macOS and Linux).
set -u
HERE=$(cd "$(dirname "$0")" && pwd); C=$HERE/../../tools/capture/linux_pci_capture.sh
T=$(mktemp -d "${TMPDIR:-/tmp}/lpc.XXXXXX"); PASSN=0; FAILN=0
mkdir -p "$T/bin" "$T/proc" "$T/sys/module/pcie_aspm/parameters" "$T/out"
echo "BOOT_IMAGE=/vmlinuz cryptdevice=UUID=12345678-abcd-ef01-2345-6789abcdef01:root quiet" > "$T/proc/cmdline"
echo "[default] performance powersave powersupersave" > "$T/sys/module/pcie_aspm/parameters/policy"
for d in 0000:06:0b.0 0000:0b:00.0 0000:0a:00.0; do
  p=$T/sys/bus/pci/devices/$d; mkdir -p "$p/link" "$p/power" "$p/firmware_node"
  echo 0x14c3 > "$p/vendor"; echo D0 > "$p/power_state"; echo on > "$p/power/control"; echo active > "$p/power/runtime_status"
  echo 1 > "$p/link/l1_aspm"; echo 0 > "$p/link/l1_2_aspm"; echo '\_SB_.PCI0.GPP7.UP00.DP58.EP00' > "$p/firmware_node/path"
done
mkdir -p "$T/drivers/mt7921e"; ln -s "$T/drivers/mt7921e" "$T/sys/bus/pci/devices/0000:0b:00.0/driver"
cat > "$T/bin/lspci" <<'X'
#!/bin/bash
s=""; for a; do case "$a" in 0000:*) s=$a;; esac; done
case "$*" in
  *-xxxx*) echo "$s config"; echo "00: c3 14 16 06 06 04 10 00";;
  *) echo "$s Network controller [0280]: MEDIATEK MT7922 [14c3:0616]"
     echo "	LnkCtl:	ASPM L1 Enabled; RCB 64 bytes"
     [ "$s" = 0000:0a:00.0 ] && echo "	Capabilities: [140 v1] Device Serial Number 3c-7c-3f-ff-ff-11-22-33"
     echo "	Kernel driver in use: mt7921e";;
esac
X
cat > "$T/bin/dmesg" <<'X'
#!/bin/bash
echo "[    0.5] pci 0000:0b:00.0: [14c3:0616] type 00 class 0x028000"
echo "[    9.1] mt7921e 0000:0b:00.0: ASIC revision: 79220010"
echo "[    9.2] igc 0000:0a:00.0 eno1: MAC: a4:5e:60:aa:bb:cc"
echo "[    9.3] usb 1-1: unrelated line"
X
chmod +x "$T/bin/"*
check() { local name=$1 ok=1 c; shift
  for c in "$@"; do eval "$c" || { ok=0; echo "    failed: $c"; }; done
  if [ $ok = 1 ]; then PASSN=$((PASSN+1)); echo "PASS  $name"; else FAILN=$((FAILN+1)); echo "FAIL  $name"; cat "$T"/out/*.txt "$T/o" 2>/dev/null | sed 's/^/    /'; fi; }
CAP_SYS=$T/sys CAP_PROC=$T/proc CAP_LSPCI=$T/bin/lspci CAP_DMESG=$T/bin/dmesg CAP_NOROOT=1 bash "$C" "$T/out" > "$T/o" 2>&1; rc=$?
F=$(ls "$T"/out/linux-pci-*.txt 2>/dev/null | head -1)
check runs '[ $rc = 0 ]' '[ -n "$F" ]' 'grep -q "^wrote " "$T/o"'
check content 'grep -q "ASPM policy" "$F"' 'grep -q "\[default\] performance" "$F"' 'grep -q "0000:06:0b.0: lspci -vvv" "$F"' \
  'grep -q "LnkCtl:	ASPM L1 Enabled" "$F"' 'grep -q "^link/l1_aspm = 1" "$F"' 'grep -q "^driver = mt7921e" "$F"' \
  'grep -q "0000:00:02.1: not present" "$F"' 'grep -q "0000:0b:00.0: raw config space (lspci -xxxx)" "$F"' \
  'grep -q "ASIC revision" "$F"' '! grep -q "unrelated line" "$F"' 'grep -q "DP58.EP00" "$F"'
check personal_data '! grep -qiE "3c-7c-3f|a4:5e:60|12345678-abcd" "$F"' 'grep -q "Device Serial Number <masked>" "$F"' \
  'grep -q "cryptdevice=UUID=<uuid>:root" "$F"' 'grep -q "MAC: xx:xx:xx:xx:xx:xx" "$F"' \
  '! grep -q "0000:0a:00.0: raw config space (lspci" "$F"'
CAP_SYS=$T/sys CAP_PROC=$T/proc CAP_LSPCI=$T/bin/lspci CAP_DMESG=$T/bin/dmesg bash "$C" "$T/out2" > "$T/o" 2>&1
check needs_root_when_not_root '[ "$(id -u)" = 0 ] || grep -q "run as root" "$T/o"'
rm -rf "$T"; echo "linux_pci_capture_test: $PASSN passed, $FAILN failed"; [ $FAILN = 0 ]
