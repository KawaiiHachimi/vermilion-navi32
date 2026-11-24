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
                        val_template=int(m.group(4), 16), name=m.group(6), note=m.group(7).strip()))
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
    ap.add_argument("--dig-shift", type=lambda s: int(s, 0), default=0,
                    help="the DIG/DP block moves by 0x100 per DIG index: 0x300 for the Navi 32 board (DP1 -> DP4)")
    ap.add_argument("--fill-from-template", action="store_true",
                    help="a register the read does not cover keeps the template's value, flagged TODO-CAPTURE (1)")
    ap.add_argument("--derive-scaler-blank", action="store_true",
                    help="DSCL0_OTG_H/V_BLANK: use OTG0's blank even when the read has them (Linux leaves them 0 with no scaling)")
    a = ap.parse_args()
    regs = load_regs(a.regs)
    ent, types = load_template(a.template)
    sym = "kPhxP0_" + a.name
    out, missing = [], []
    todo = 0
    # The DP stream / MSA block is per DIG index (DP1 = 0x056C0..., DP4 = +0x300). The OTG, VTG, DSCL and
    # HUBPREQ blocks are not: the plan drives OTG0 on both boards.
    dpname = "DP%d" % (1 + a.dig_shift // 0x100) if a.dig_shift else "DP1"

    def shifted(r):
        return r + a.dig_shift if a.dig_shift and 0x056C0 <= r < 0x05800 else r

    def dpnamed(n):
        return n.replace("DP1_", dpname + "_") if dpname != "DP1" else n
    for e in ent:
        r, flags, note = shifted(e["reg"]), e["pad"], "[T] " + ("firmware read" if a.from_live else "Linux steady read")
        if r == DTO and not a.from_live:
            v, note = a.pclk * 1000, "[F] pclk in Hz (firmware DTO convention, modulo 716666667)"
        elif r in (DSCL_HB, DSCL_VB) and (a.derive_scaler_blank or r not in regs) and (OTG_HB if r == DSCL_HB else OTG_VB) in regs:
            # DSCL0's blanking follows the OTG's when the scaler is bypassed (Linux leaves them 0 in that case,
            # which reads as "not programmed" to the shell); the reference plans use this rule too.
            v, flags, note = regs[OTG_HB if r == DSCL_HB else OTG_VB], flags | F_DERIVED, "[D] = OTG0 blank"
        elif r in regs:
            v = regs[r]
        elif False:
            v, flags, note = regs[OTG_HB if r == DSCL_HB else OTG_VB], flags | F_DERIVED, "[D] = OTG0 blank (not in the read)"
        elif a.fill_from_template:
            # A register this read does not cover: keep the template's value and flag it, so a plan can be
            # tried on hardware before every group has been captured (the flag counts in phx_p0_info.todo).
            v, flags, note = e["val_template"], flags | 1, "[T] template value (not in this read)"
            todo += 1
        else:
            missing.append(e["name"]); continue
        out.append((e["stage"], flags, r, v & e["mask"], e["mask"], dpnamed(e["name"]), note))
    if missing:
        sys.exit("not in the register read: " + ", ".join(missing))
    if a.depth:
        fmt, pix, msa = DEPTH[a.depth]
        g3 = max(i for i, x in enumerate(out) if x[0] <= 3)
        out.insert(g3 + 1, (3, F_DB, FMT, fmt, 0xFFFFFFFF, "FMT0_FMT_BIT_DEPTH_CONTROL", f"[F] {a.depth} bpc"))
        out.append((5, 0, shifted(PIXFMT), pix, 0x07000000, dpname + "_DP_PIXEL_FORMAT", f"[F] DP_COMPONENT_DEPTH {a.depth} bpc"))
        out.append((5, 0, shifted(MSACOL), msa, 0xFF000000, dpname + "_DP_MSA_COLORIMETRY", f"[F] MSA MISC0 {a.depth} bpc RGB"))
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
    p.write(f'static const struct phx_p0_info {sym}Info = {{{a.hz}, 0x{ht:x}, 0x{vt:x}, {a.pclk}, 0x{a.dentist:x}, 0, 1, {todo}, 0x0, 0x0, "{a.name}"}};\n')


if __name__ == "__main__":
    main()
