#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""cpusensors.py: CPU package temperature and power on the XTX machine, from the VirtualSMC keys that
SMCProcessorAMD publishes (TC0P = Tctl, PCPR = package power). Read-only, no root.

    python3 tools/sensors/cpusensors.py              # one line
    python3 tools/sensors/cpusensors.py -n 60 -i 1   # 60 samples, 1 s apart, then min / median / max

Needs VirtualSMC's `smcread` (VirtualSMC release zip, Tools/smcread): on PATH, in ~/.local/bin, or --smcread.
Single power spikes can be counter wraps (see .agents/findings/2026-11-22-cpu-sensors.md): look at the median.
"""
import argparse
import os
import shutil
import statistics
import subprocess
import sys
import time

KEYS = {"TC0P": ("C", 256), "PCPR": ("W", 64)}      # sp78 = 8.8 fixed point, sp96 = 9.6 fixed point


def find_smcread(given):
    for p in (given, shutil.which("smcread"), os.path.expanduser("~/.local/bin/smcread")):
        if p and os.access(p, os.X_OK):
            return p
    sys.exit("smcread not found (VirtualSMC release zip, Tools/smcread); pass --smcread")


def sample(smcread):
    out = subprocess.run([smcread, "-s"], capture_output=True, text=True).stdout
    vals = {}
    for line in out.splitlines():
        parts = line.split()
        if parts and parts[0].strip("[]") in KEYS:
            key = parts[0].strip("[]")
            raw = int(parts[-1], 16)
            raw = raw - 65536 if raw > 32767 else raw
            vals[key] = raw / KEYS[key][1]
    return vals


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-n", type=int, default=1, help="number of samples")
    ap.add_argument("-i", type=float, default=1.0, help="seconds between samples")
    ap.add_argument("--smcread")
    a = ap.parse_args()
    smcread = find_smcread(a.smcread)
    series = {k: [] for k in KEYS}
    for n in range(a.n):
        v = sample(smcread)
        if not v:
            sys.exit("no CPU keys: is SMCProcessorAMD loaded? (kextstat | grep SMCProcessorAMD)")
        print(time.strftime("%H:%M:%S"), "  ".join(f"{k} {v[k]:6.1f} {KEYS[k][0]}" for k in KEYS if k in v), flush=True)
        for k in v:
            series[k].append(v[k])
        if n + 1 < a.n:
            time.sleep(a.i)
    if a.n > 1:
        for k, s in series.items():
            if s:
                print(f"{k}: min {min(s):.1f}  median {statistics.median(s):.1f}  max {max(s):.1f} {KEYS[k][0]}  ({len(s)} samples)")


if __name__ == "__main__":
    main()
