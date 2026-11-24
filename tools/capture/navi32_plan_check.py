"""navi32_plan_check.py: check a generated pipe-0 plan against the register capture it came from.

Only the entries the generator took straight from the capture (note "[T] ... read") are compared. Everything
else is expected to differ by design and is counted by reason:
  - "[T] template value (not in this read)"  (flag TODO-CAPTURE): the capture does not cover that register
  - "[F] ..."                                 computed: the DTO phase from the pixel clock, the colour depth
  - "[D] ..."                                 derived: DSCL blank from the OTG blank
This is the hardware-free gate for phx_p0_plan_1080p60n32.inc: it catches a wrong --dig-shift, a template
register that moved, or a plan regenerated from a different capture.

usage: navi32_plan_check.py <plan.inc> <dump | converted regs> [--abs]
"""
import argparse, re, sys

BASES = {0: 0x12, 1: 0xC0, 2: 0x34C0, 3: 0x9000}
DUMP = re.compile(r"^(\w+)\s+(\d+)\s+0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)")
ABS = re.compile(r"^0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)")
STEP = re.compile(r"\{PHX_S2_W, (\d+), (\d+), 0x([0-9A-Fa-f]+), 0x([0-9A-Fa-f]+), 0x([0-9A-Fa-f]+), \{0\}, \"(\w+)\"\},\s*/\* ([^*]*)\*/")
INFO = re.compile(r"struct phx_p0_info \w+ = \{(\d+), 0x([0-9A-Fa-f]+), 0x([0-9A-Fa-f]+), (\d+), 0x([0-9A-Fa-f]+), (\d+), (\d+), (\d+)")


def load_dump(path, absolute):
    regs = {}
    for line in open(path):
        if line.startswith("#"):
            continue
        m = ABS.match(line) if absolute else DUMP.match(line)
        if not m:
            continue
        if absolute:
            regs[int(m.group(1), 16)] = int(m.group(2), 16)
        else:
            regs[BASES[int(m.group(2))] + int(m.group(3), 16)] = int(m.group(4), 16)
    return regs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("plan")
    ap.add_argument("dump")
    ap.add_argument("--abs", action="store_true", help="the dump is already absolute (navi32_dump_to_regs.py)")
    a = ap.parse_args()
    regs = load_dump(a.dump, a.abs)
    plan = open(a.plan).read()
    verified = bad = 0
    reasons = {}
    for m in STEP.finditer(plan):
        pad, reg, val, mask, name, note = int(m.group(2)), int(m.group(3), 16), int(m.group(4), 16), int(m.group(5), 16), m.group(6), m.group(7).strip()
        if pad & 1:
            reasons["template (not in capture)"] = reasons.get("template (not in capture)", 0) + 1
            continue
        if note.startswith("[F]"):
            reasons["computed"] = reasons.get("computed", 0) + 1
            continue
        if note.startswith("[D]"):
            reasons["derived"] = reasons.get("derived", 0) + 1
            continue
        if reg not in regs:
            print("MISSING  %-28s 0x%05X not in the capture" % (name, reg))
            bad += 1
            continue
        if (regs[reg] & mask) != (val & mask):
            print("MISMATCH %-28s 0x%05X plan 0x%08x capture 0x%08x (mask 0x%08x)" % (name, reg, val, regs[reg], mask))
            bad += 1
        verified += 1
    mi = INFO.search(plan)
    summary = ", ".join("%d %s" % (v, k) for k, v in sorted(reasons.items()))
    print("plan %s: %d entries match the capture, %d mismatch%s" % (a.plan.split("/")[-1], verified, bad, ("; skipped: " + summary) if summary else ""))
    if mi:
        print("  info: %s Hz, H_TOTAL 0x%s V_TOTAL 0x%s, pclk %s kHz, todo %s" % (mi.group(1), mi.group(2), mi.group(3), mi.group(4), mi.group(8)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
