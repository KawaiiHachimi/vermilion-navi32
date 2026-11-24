#!/usr/bin/env python3
"""navi32_linux_capture.py: read the DCN registers a Vermilion plan needs, on Linux.

The plan for a board (src/vermilion-boot/phx_p0_plan_*.inc) is built from a register read of the mode it
drives, and 13 HUBPREQ0 (prefetch/watermark) registers of the Navi 32 board are still filled from the
reference machine. This script produces that read: for each register of a list it preads 4 bytes from the DC
debugfs node at absolute dword index * 4 - the same addressing Vermilion uses, so no DMU-segment arithmetic -
and writes "0xREG 0xVAL NAME", the format p0plan_from_regs.py and navi32_plan_check.py --abs read.

    sudo python3 navi32_linux_capture.py --reglist navi32-dcn-reglist.txt > regs.txt

Run it with the monitor at the mode you want to capture (the plan is steady state, so the mode has to be the
one that will be planned). Over SSH use navi32_linux_capture_ssh.sh, which copies this file and the list to the
machine and brings the read back.

Four consecutive 0xffffffff reads abort: a powered-down block reads that, and the value is meaningless
(RedNavi: GC/DCN blocks read 0 in GFXOFF). Registers with side effects are never in the list.
"""
import argparse, glob, os, struct, sys


def find_node(explicit):
    if explicit:
        return explicit
    nodes = sorted(glob.glob("/sys/kernel/debug/dri/*/amdgpu_regs"))
    if not nodes:
        sys.exit("no /sys/kernel/debug/dri/*/amdgpu_regs: run as root on a machine where amdgpu bound the GPU, "
                 "or pass --node")
    return nodes[0]


def capture(node, reglist, out):
    regs = []
    for line in open(reglist):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        regs.append((int(parts[0], 16), parts[1] if len(parts) > 1 else ""))
    fd = os.open(node, os.O_RDONLY)
    ones = 0
    try:
        out.write("# navi32 dcn capture: %d registers from %s\n" % (len(regs), node))
        for reg, name in regs:
            v, = struct.unpack("<I", os.pread(fd, 4, reg * 4))
            out.write("0x%05X 0x%08x  %s\n" % (reg, v, name))
            ones = ones + 1 if v == 0xFFFFFFFF else 0
            if ones >= 4:
                out.flush()
                sys.exit("the GPU stopped answering after %s (4 reads of 0xffffffff in a row)" % name)
        out.flush()
    finally:
        os.close(fd)
    return len(regs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reglist", required=True)
    ap.add_argument("--out", help="write here instead of stdout")
    ap.add_argument("--node", help="the amdgpu_regs node (default: the first under /sys/kernel/debug/dri)")
    a = ap.parse_args()
    node = find_node(a.node)
    if a.out:
        with open(a.out, "w") as f:
            n = capture(node, a.reglist, f)
        print("wrote %s (%d registers)" % (a.out, n), file=sys.stderr)
    else:
        capture(node, a.reglist, sys.stdout)


if __name__ == "__main__":
    main()
