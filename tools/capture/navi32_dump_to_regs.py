"""navi32_dump_to_regs.py: turn RedNavi's DCN pipe dump of this board into Vermilion's absolute
register convention.

RedNavi reads DCN through DMU segments: the dump has "NAME <segment> <offset> <value>", and the
absolute dword index is base[segment] + offset with the DMU block bases of Navi 32
({0: 0x12, 1: 0xC0, 2: 0x34C0, 3: 0x9000}, RedNavi tools/linux_dcn_pipe_dump.py). Vermilion
addresses DCN registers by that same absolute dword index, so the conversion is one addition.

The dump was taken on the development board (RX 7800 XT, DisplayPort on UNIPHY2 -> transmitter E ->
DIG4/DP4, OTG0) at 1080p60, which is why it is the reference for a Navi 32 pipe-0 plan:
Vermilion's own plans are for its reference machine's DIG1/OTG1 path.

usage: navi32_dump_to_regs.py <dump> [--group NAME] [--out regs.txt]
"""
import argparse, re, sys

BASES = {0: 0x12, 1: 0xC0, 2: 0x34C0, 3: 0x9000}
LINE = re.compile(r"^(\w+)\s+(\d+)\s+0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--group", action="append", default=[], help="only these groups (repeatable)")
    ap.add_argument("--out")
    a = ap.parse_args()
    group = None
    out = []
    for line in open(a.dump):
        if line.startswith("# group "):
            group = line.split()[2]
            continue
        m = LINE.match(line)
        if not m:
            continue
        if a.group and group not in a.group:
            continue
        name, seg, off, val = m.group(1), int(m.group(2)), int(m.group(3), 16), int(m.group(4), 16)
        absreg = BASES[seg] + off
        out.append((absreg, val, name, group))
    with open(a.out, "w") if a.out else sys.stdout as f:
        for absreg, val, name, group in out:
            f.write("0x%05X 0x%08x  %s\n" % (absreg, val, name))
    if a.out:
        print("wrote %s (%d registers)" % (a.out, len(out)), file=sys.stderr)


if __name__ == "__main__":
    main()
