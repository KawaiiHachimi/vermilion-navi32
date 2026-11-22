#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# p0plan_from_regs.py: a pipe-0 plan (src/vermilion-boot/phx_p0_plan_<mode>.inc, PhxPipe0.h) from a register read.
#
# The register list, groups, masks and flags come from an existing plan (the template, upstream's
# phx_p0_plan_1080p144.inc); every value from a register read of the target mode:
#   - Linux's steady state after a mode set (tools/capture/linux_display_capture.py <step>/regs.txt: "0xREG 0xVAL [name]")
#   - or the firmware's live state on macOS (`phxctl regs ...` output: "0xreg: 0xval"), for the mode the GOP lights.
# Plans are in the firmware's DTO convention (conv 0, as upstream's): DP_DTO0_PHASE = the pixel clock in Hz (modulo stays
# the firmware's 716,666,667), so Linux's 2-pixel-per-cycle registers (DTO phase / 2, DP_VID_TIMING N_MUL,
# DP_PIXEL_FORMAT bit 30) are never copied. DSCL0_OTG_H/V_BLANK, when the read lacks them, = OTG0 H/V blank (as Linux
# programs them; flag DERIVED). --depth adds the output colour depth (b128): FMT0_FMT_BIT_DEPTH_CONTROL (group 3,
# double-buffered), DP1_DP_PIXEL_FORMAT DP_COMPONENT_DEPTH and DP1_DP_MSA_COLORIMETRY MISC0 (group 5), with Linux's values
# for 8 / 10 bpc on this card (capture 2026-11-20) or the firmware's for 12.
#
#   python3 tools/capture/p0plan_from_regs.py --regs <file> --name 1440p120 --hz 120 --pclk 497750 --dentist 0x22 \
#       --depth 10 --source "Linux 7.2.15, LG ULTRAGEAR+ CTA DTD 2: 497750 kHz 2720x1525" > src/vermilion-boot/phx_p0_plan_1440p120.inc
import argparse, re, sys

DEPTH = {   # bpc: (FMT0_FMT_BIT_DEPTH_CONTROL, DP_COMPONENT_DEPTH << 24, MISC0 << 24)
    8: (0x0000C900, 0x01000000, 0x20000000),    # Linux: spatial dither to 8 bpc (RGB + high-pass random)
    10: (0x0000D100, 0x02000000, 0x40000000),   # Linux: spatial dither to 10 bpc
    12: (0x00000000, 0x03000000, 0x60000000),   # the firmware on the XTX machine (macOS 2026-11-19): no FMT reduction
}
FMT, PIXFMT, MSACOL = 0x04D01, 0x056C9, 0x056CA
DTO, OTG_HB, OTG_VB, DSCL_HB, DSCL_VB, HT, VT = 0x00141, 0x04FEB, 0x04FF6, 0x041CF, 0x041D0, 0x04FEA, 0x04FEF
F_DERIVED, F_DB = 2, 4


def load_regs(path):
    d = {}
    for line in open(path):
        m = re.match(r"\s*0x([0-9A-Fa-f]+):?\s+0x([0-9A-Fa-f]+)", line)
        if m:
            d[int(m.group(1), 16)] = int(m.group(2), 16)
    return d


def load_template(path):
    txt = open(path).read()
    ent = []
    for m in re.finditer(r'\{PHX_S2_W, (\d+), (\d+), (0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]+), \{0\}, "(\w+)"\},\s*/\* ([^*]*)\*/', txt):
        ent.append(dict(stage=int(m.group(1)), pad=int(m.group(2)), reg=int(m.group(3), 16), mask=int(m.group(5), 16),
                        name=m.group(6), note=m.group(7).strip()))
    head = txt[:txt.index("static const struct phx_s2_step")]
    types = head[head.index("#ifndef PHX_S2_TYPES"):]
    return ent, types


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--template", default="src/vermilion-boot/phx_p0_plan_1080p144.inc")
    ap.add_argument("--regs", required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--hz", type=int, required=True)
    ap.add_argument("--pclk", type=int, required=True, help="kHz")
    ap.add_argument("--dentist", type=lambda s: int(s, 0), default=0, help="DENTIST divider max (0 none)")
    ap.add_argument("--depth", type=int, choices=sorted(DEPTH), help="add the colour-depth entries (b128)")
    ap.add_argument("--source", required=True, help="one line: where the values come from")
    ap.add_argument("--from-live", action="store_true", help="--regs is the firmware's live state: the DTO phase is copied, not computed")
    a = ap.parse_args()
    regs = load_regs(a.regs)
    ent, types = load_template(a.template)
    sym = "kPhxP0_" + a.name
    out, missing = [], []
    for e in ent:
        r, flags, note = e["reg"], e["pad"], "[T] " + ("firmware read" if a.from_live else "Linux steady read")
        if r == DTO and not a.from_live:
            v, note = a.pclk * 1000, "[F] pclk in Hz (firmware DTO convention, modulo 716666667)"
        elif r in regs:
            v = regs[r]
        elif r in (DSCL_HB, DSCL_VB) and (OTG_HB if r == DSCL_HB else OTG_VB) in regs:
            v, flags, note = regs[OTG_HB if r == DSCL_HB else OTG_VB], flags | F_DERIVED, "[D] = OTG0 blank (not in the read)"
        else:
            missing.append(e["name"]); continue
        out.append((e["stage"], flags, r, v & e["mask"], e["mask"], e["name"], note))
    if missing:
        sys.exit("not in the register read: " + ", ".join(missing))
    if a.depth:
        fmt, pix, msa = DEPTH[a.depth]
        g3 = max(i for i, x in enumerate(out) if x[0] <= 3)
        out.insert(g3 + 1, (3, F_DB, FMT, fmt, 0xFFFFFFFF, "FMT0_FMT_BIT_DEPTH_CONTROL", f"[F] {a.depth} bpc"))
        out.append((5, 0, PIXFMT, pix, 0x07000000, "DP1_DP_PIXEL_FORMAT", f"[F] DP_COMPONENT_DEPTH {a.depth} bpc"))
        out.append((5, 0, MSACOL, msa, 0xFF000000, "DP1_DP_MSA_COLORIMETRY", f"[F] MSA MISC0 {a.depth} bpc RGB"))
    ht = next(x[3] for x in out if x[2] == HT)
    vt = next(x[3] for x in out if x[2] == VT)
    p = sys.stdout
    p.write("// SPDX-License-Identifier: MIT\n")
    p.write(f"/* mode {a.name}, generated by tools/capture/p0plan_from_regs.py from a register read: do not edit */\n")
    p.write(f"/* values: {a.source} */\n")
    p.write(f"/* template (registers, groups, masks): {a.template.split('/')[-1]}{'; colour depth ' + str(a.depth) + ' bpc (b128)' if a.depth else ''} */\n")
    p.write(types.rstrip() + "\n")
    p.write(f"static const struct phx_s2_step {sym}[] = {{\n")
    for st, fl, r, v, m, n, note in out:
        p.write(f'  {{PHX_S2_W, {st}, {fl}, 0x{r:05X}, 0x{v:08x}, 0x{m:08x}, {{0}}, "{n}"}},  /* {note} */\n')
    p.write("};\n")
    p.write(f'static const struct phx_p0_info {sym}Info = {{{a.hz}, 0x{ht:x}, 0x{vt:x}, {a.pclk}, 0x{a.dentist:x}, 0, 1, 0, 0x0, 0x0, "{a.name}"}};\n')


if __name__ == "__main__":
    main()
