#!/bin/bash
# SPDX-License-Identifier: MIT
# linux_pci_capture.sh <outdir>: Linux side of the "chipset endpoints go silent in macOS" question (XTX machine,
# findings/2026-11-19-wifi-mt7922.md). Run as root on Linux booted directly (Limine, not through OpenCore). It only
# reads: lspci -vvv of the MT7922 / I225-V paths and their neighbours, raw config space of the bridges and the
# Wi-Fi card, the ASPM policy and per-link ASPM/L1SS state from sysfs, runtime-PM state, and the kernel log's PCI,
# ASPM, mt7921e and igc lines. Writes <outdir>/linux-pci-<date>.txt (use /boot/vermilion/capture: macOS reads the
# EFI partition).
#
# Personal data: the PCIe Device Serial Number capability (the I225-V's is derived from its MAC) and MAC addresses
# are masked, the kernel command line has its UUIDs masked, and devices that carry a serial number get no raw hex
# dump. No SSIDs: no Wi-Fi tool is run.
#
# Test hooks: CAP_SYS (sysfs root, default /sys), CAP_PROC (default /proc), CAP_DMESG (command, default dmesg),
# CAP_LSPCI (default lspci), CAP_NOROOT=1 skips the root check.
set -u
OUT=${1:?usage: linux_pci_capture.sh <outdir>}
SYS=${CAP_SYS:-/sys}; PROC=${CAP_PROC:-/proc}
DMESG=${CAP_DMESG:-dmesg}; LSPCI=${CAP_LSPCI:-lspci}
[ -n "${CAP_NOROOT:-}" ] || [ "$(id -u)" = 0 ] || { echo "run as root (sudo)"; exit 1; }
mkdir -p "$OUT" || exit 1
F=$OUT/linux-pci-$(date +%Y%m%d-%H%M%S).txt

# root port GPP7, chipset upstream port, the ports of Ethernet / Wi-Fi / USB / SATA, and the endpoints behind them
DEVS="0000:00:02.1 0000:05:00.0 0000:06:0a.0 0000:06:0b.0 0000:06:0c.0 0000:06:0d.0 0000:0a:00.0 0000:0b:00.0 0000:0c:00.0 0000:0d:00.0"

mask() {
  sed -E -e 's/(Device Serial Number) .*/\1 <masked>/' \
         -e 's/([0-9a-fA-F]{2}[:-]){5}[0-9a-fA-F]{2}/xx:xx:xx:xx:xx:xx/g' \
         -e 's/[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}/<uuid>/g'
}
sec() { echo; echo "===== $*"; }

{
  echo "linux_pci_capture $(date -u +%FT%TZ), kernel $(uname -r)"
  sec "kernel command line (UUIDs masked)"; cat "$PROC/cmdline" 2>/dev/null; echo
  sec "ASPM policy"; cat "$SYS/module/pcie_aspm/parameters/policy" 2>/dev/null || echo "(no pcie_aspm module parameters)"

  for d in $DEVS; do
    p=$SYS/bus/pci/devices/$d
    [ -e "$p" ] || { sec "$d: not present"; continue; }
    sec "$d: lspci -vvv"
    $LSPCI -vvv -nn -s "$d" 2>&1
    sec "$d: sysfs"
    for k in vendor device subsystem_vendor subsystem_device class current_link_speed current_link_width \
             max_link_speed max_link_width power_state d3cold_allowed power/control power/runtime_status \
             power/runtime_suspended_time power/wakeup; do
      [ -r "$p/$k" ] && echo "$k = $(cat "$p/$k" 2>/dev/null)"
    done
    for k in "$p"/link/*; do [ -r "$k" ] && echo "link/$(basename "$k") = $(cat "$k" 2>/dev/null)"; done
    echo "driver = $(basename "$(readlink "$p/driver" 2>/dev/null)" 2>/dev/null)"
    echo "firmware_node = $(cat "$p/firmware_node/path" 2>/dev/null)"
  done 2>/dev/null

  for d in 0000:00:02.1 0000:05:00.0 0000:06:0a.0 0000:06:0b.0 0000:06:0c.0 0000:0b:00.0; do
    [ -e "$SYS/bus/pci/devices/$d" ] || continue
    if $LSPCI -vvv -s "$d" 2>/dev/null | grep -q 'Device Serial Number'; then
      sec "$d: raw config space skipped (it carries a Device Serial Number)"; continue
    fi
    sec "$d: raw config space (lspci -xxxx)"
    $LSPCI -xxxx -s "$d" 2>&1
  done

  sec "kernel log: PCI / ASPM / mt7921e / igc / btusb (this boot)"
  $DMESG 2>/dev/null | grep -iE 'aspm|l1ss|0000:0[0-6]:|0000:0[a-d]:|mt79|mt76|igc|btusb|bluetooth: hci|pcieport|AER|bridge window|BAR [0-9]' | head -600
} 2>&1 | mask > "$F"

echo "wrote $F ($(wc -l < "$F") lines)"
