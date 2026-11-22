#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""usbwatch.py: show which controller and root port every USB device sits on, and print a line each time one
appears or disappears. For building a USB map: plug a test device into each physical port in turn.

    python3 tools/usbmap/usbwatch.py            # watch (Ctrl-C to stop)
    python3 tools/usbmap/usbwatch.py --once     # print the current state and exit
    python3 tools/usbmap/usbwatch.py --ports    # every root port of every controller, with what is on it

Read-only: it only runs `ioreg -a`. No root. Serial numbers are never printed. Devices behind a hub are shown
under the hub's root port, which is the port the map needs.
"""
import argparse
import plistlib
import subprocess
import sys
import time

SPEED = {0: "Low", 1: "Full", 2: "High", 3: "Super", 4: "Super+", 5: "Super+x2"}


def controllers():
    # ioreg can fail or print a torn tree while a device is being attached: retry a few times.
    for _ in range(5):
        r = subprocess.run(["ioreg", "-a", "-l", "-p", "IOService", "-r", "-c", "AppleUSBXHCI", "-d", "8"],
                           capture_output=True)
        if r.returncode == 0 and r.stdout.strip():
            try:
                return plistlib.loads(r.stdout)
            except Exception:
                pass
        time.sleep(0.2)
    return None


def walk(entry, ctl, port, rows):
    for child in entry.get("IORegistryEntryChildren", []):
        cls = child.get("IOObjectClass", "")
        if cls.startswith("AppleUSB") and cls.endswith("Port") and port is None:
            walk(child, ctl, child, rows)
        elif "idVendor" in child and "USB Address" in child and port is not None:
            rows.append((ctl, port, child))
            walk(child, ctl, port, rows)
        else:
            walk(child, ctl, port, rows)


def port_label(ctl, port):
    name = port.get("IORegistryEntryName", "?")
    hw = int.from_bytes(port.get("port", b"\0"), "little")    # the xHCI port number (= the ACPI _ADR)
    kind = "USB3" if "30" in port.get("IOObjectClass", "") else "USB2"
    return f"{ctl.get('IORegistryEntryName', '?')}/{name} (#{hw} {kind})"


def devices():
    ctls = controllers()
    if ctls is None:
        return None
    rows = []
    for ctl in ctls:
        walk(ctl, ctl, None, rows)
    result = {}
    for ctl, port, dev in rows:
        loc = dev.get("locationID", 0) & 0xFFFFFFFF
        name = dev.get("USB Product Name") or dev.get("IORegistryEntryName", "?")
        result[loc] = (port_label(ctl, port), SPEED.get(dev.get("Device Speed", dev.get("USBSpeed")), "?"), name,
                       f"{dev.get('idVendor', 0):04x}:{dev.get('idProduct', 0):04x}", loc & 0x000FFFFF != 0)
    return result


def line(sign, d):
    label, speed, name, ids, behind_hub = d
    hub = "  (behind a hub)" if behind_hub else ""
    return f"{time.strftime('%H:%M:%S')} {sign} {label:28} {speed:7} {name!r:32} {ids}{hub}"


def show_ports():
    for ctl in controllers() or []:
        print(ctl.get("IORegistryEntryName", "?"), ctl.get("UsbHostControllerProtocolRevision", ""))
        for port in ctl.get("IORegistryEntryChildren", []):
            for p in [port] + port.get("IORegistryEntryChildren", []):
                cls = p.get("IOObjectClass", "")
                if cls.startswith("AppleUSB") and cls.endswith("Port"):
                    on = [c.get("USB Product Name") or c.get("IORegistryEntryName") for c in
                          p.get("IORegistryEntryChildren", []) if "idVendor" in c and "USB Address" in c]
                    print(f"  {port_label(ctl, p):28} {p.get('UsbConnector', ''):>3}  {', '.join(on)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--once", action="store_true")
    ap.add_argument("--ports", action="store_true")
    ap.add_argument("--interval", type=float, default=0.5)
    a = ap.parse_args()
    if a.ports:
        show_ports()
        return
    prev = devices()
    while prev is None:
        time.sleep(0.5)
        prev = devices()
    for d in sorted(prev.values()):
        print(line("=", d))
    if a.once:
        return
    print("-- watching; Ctrl-C to stop", flush=True)
    try:
        while True:
            time.sleep(a.interval)
            cur = devices()
            if cur is None:
                continue
            for loc in sorted(cur.keys() - prev.keys()):
                print(line("+", cur[loc]), flush=True)
            for loc in sorted(prev.keys() - cur.keys()):
                print(line("-", prev[loc]), flush=True)
            prev = cur
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    sys.exit(main())
