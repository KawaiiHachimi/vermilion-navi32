#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# p0odm_from_trace.py: a pipe-0 ODM 2:1 mode-set plan (src/vermilion-boot/phx_p0odm_<name>.inc) from a Linux display capture
# (tools/capture/linux_display_capture.py v2: <step>/trace-modeset.txt with amdgpu_dc_wreg lines and phxcap/dmubcmd lines).
#
# Linux's mode set is replayed in Linux's order, cut down to what a 1440p ODM switch on the XTX machine needs:
#   literal  Linux's value: OTG / ODM / OPP / FMT / DSC / DSCRM / VPG / DIG / DP stream, HUBP DLG / TTU and viewports, DSCL
#            split, MPCC tree structure, DET / arbiter / watermarks, DPP clocks and power gating;
#   copy     pipe 1's surface and colour registers (surface config / tiling / pitch / DCC / address / VMID, CNVC format, CM,
#            MCM, OGAM gamut, MPC_OUT CSC, MPCC blend / gains / background) = pipe 0's live value at run time, so the right
#            half scans the same macOS surface through the same colour path as the left half;
#   dropped  Linux's own surface and colour values for pipe 0 (macOS's stay), the per-frame OTG interrupt / static-screen /
#            sync writes, flips, cursor, audio, the AUX engine (DPCD goes through the engine's own AUX path), DAL messages
#            other than SetHardMinByFreq (9) for DISPCLK (8) / DPPCLK (9), pipe 0's power gating (DOMAIN0: Linux gates and
#            ungates HUBP0 / DPP0 around the switch, which loses their registers; macOS's surface and colour values are
#            not replayed, so pipe 0 stays powered and keeps them), DENTIST (the PMFW moves it after the hard minimums,
#            as the 120 / 144 plans), DMCUB GPINT, every DMUB command but TRANSMITTER_CONTROL, the DPCD source OUI writes;
#   link     Linux's link-off window (D3 .. FEC_READY off) and link-up window (DP_CONFIG .. FEC on) become LINKOFF / LINKUP
#            steps run by the engine's trainer (PhxDpLink.h, the bytes Linux sent; adaptive CR / EQ as Linux's).
# Linux's REG_WAITs are not in a write trace; they are inserted after the writes that start them (rules in WAIT_RULES,
# from Linux v6.8 optc1/optc32, dcn32 power gating, hubp2_set_blank, enc1/enc32 stream encoder).
#
#   python3 tools/capture/p0odm_from_trace.py --trace <cap>/240/trace-modeset.txt --until 246.5262 --name 1440p240 \
#       --source "Linux 7.2.15, capture 2026-11-20 065140, 144 -> 240" > src/vermilion-boot/phx_p0odm_1440p240.inc
import argparse, re, sys

SEG = [0x12, 0xC0, 0x34C0, 0x9000]
OPS = {"W": 1, "COPY": 2, "WAIT": 3, "DAL": 4, "DPCDW": 5, "LINKOFF": 6, "LINKUP": 7, "DELAY": 8}


def names(hdr):
    off, idx = {}, {}
    for l in open(hdr):
        m = re.match(r"#define (reg\w+?)(_BASE_IDX)?\s+(0x[0-9a-fA-F]+|\d+)", l)
        if m:
            (idx if m.group(2) else off)[m.group(1)] = int(m.group(3), 0)
    n = {}
    for k, v in off.items():
        if k in idx and idx[k] < 4:
            n.setdefault(v + SEG[idx[k]], k[3:])
    return n


DROP = re.compile(r"VERTICAL_INTERRUPT|GLOBAL_SYNC_STATUS|STATIC_SCREEN|TRIGA_MANUAL_TRIG|DC_GPIO|DC_GENERICA|_AUX_|AFMT|^AZ|"
                  r"AUDIO|SEC_AUD|SEC_TIMESTAMP|DENTIST|OTG_STEREO|3D_STRUCTURE|OPPBUF|DMCUB|CURSOR|^CUR_|CNVC_CUR|"
                  r"UCLK_PSTATE_FORCE|SURFACE_FLIP_INT|MPC_OUT_CSC_COEF_FORMAT|^DOMAIN0_PG_CONFIG$")
COPY = re.compile(r"DCSURF_SURFACE_CONFIG|DCSURF_TILING_CONFIG|DCSURF_ADDR_CONFIG|DCSURF_SURFACE_PITCH|DCSURF_SURFACE_CONTROL|"
                  r"DCSURF_FLIP_CONTROL|SURFACE_ADDRESS|VMID_SETTINGS|DCHUBP_REQ_SIZE_CONFIG|HUBPRET_CONTROL|DCN_EXPANSION_MODE|"
                  r"CNVC_SURFACE_PIXEL_FORMAT|CNVC_CFG\d_FORMAT_CONTROL|PRE_DEALPHA|PRE_REALPHA|PRE_DEGAM|CM_CONTROL|"
                  r"CM_POST_CSC|GAMUT_REMAP|GAMCOR|HDR_MULT|MCM_|OGAM_CONTROL|MPC_OUT\d_CSC|MOVABLE_CM|MPCC_CONTROL|_GAIN|"
                  r"MPCC_BG_")
PIPE1 = re.compile(r"^(HUBP1|HUBPREQ1|HUBPRET1|CNVC_CFG1|CM1|DSCL1|DPP_TOP1|MPCC1|MPCC_OGAM1|MPCC_MCM1|MPC_OUT1)_")
# pipe 1 name -> pipe 0 name (same field layout, one instance down)
P1TO0 = [("HUBPREQ1_", "HUBPREQ0_"), ("HUBPRET1_", "HUBPRET0_"), ("HUBP1_", "HUBP0_"), ("CNVC_CFG1_", "CNVC_CFG0_"),
         ("CM1_", "CM0_"), ("MPCC_OGAM1_", "MPCC_OGAM0_"), ("MPCC_MCM1_", "MPCC_MCM0_"), ("MPCC1_", "MPCC0_"),
         ("MPC_OUT1_", "MPC_OUT0_")]
SHARED_MASK = {"DPPCLK_DTO_CTRL": 0x00000033}   # DPP0 / DPP1 fields only (DPP2-5 untouched)
LINK_OFF_END = ("DP1_DP_DPHY_CNTL", 0x00000000)   # after DPCD 0x120 = 0: FEC_READY shadow off
LINK_UP_END = ("DP1_DP_DPHY_CNTL", 0x00000030)    # FEC_EN | FEC_READY_SHADOW after training


def wait_rules(n, v, prev, nm2a, nextv=0):
    """(reg, mask, want, timeout_us) waits Linux runs after this write."""
    w = []
    def A(name):
        return nm2a[name]
    m = re.match(r"OTG(\d)_OTG_CONTROL$", n)
    if m and (prev.get(n, 1) & 1) and not (v & 1):          # master disable -> OTG_BUSY clear (optc1_disable_crtc)
        w.append((A(f"OTG{m.group(1)}_OTG_CLOCK_CONTROL"), 0x10000, 0, 100000))
    m = re.match(r"OTG(\d)_OTG_CLOCK_CONTROL$", n)
    if m and (v & 1):                                       # clock enable -> CLOCK_ON
        w.append((A(n), 0x100, 0x100, 1000))
    m = re.match(r"ODM(\d)_OPTC_INPUT_CLOCK_CONTROL$", n)
    if m and (v & 2):                                       # input clock enable -> INPUT_CLK_ON
        w.append((A(n), 0x4, 0x4, 1000))
    m = re.match(r"DOMAIN(\d+)_PG_CONFIG$", n)
    if m:                                                   # power gate / ungate -> PGFSM status (2 = off, 0 = on)
        w.append((A(n) + 1, 0xC0000000, 0x80000000 if v & 0x100 else 0, 1000000))
    m = re.match(r"OTG(\d)_OTG_MASTER_UPDATE_LOCK$", n)
    if m and (v & 1):                                       # lock -> UPDATE_LOCK_STATUS
        w.append((A(n), 0x100, 0x100, 100000))
    m = re.match(r"HUBP(\d)_DCHUBP_CNTL$", n)
    if m and (v & 1) and not (prev.get(n, 0) & 1):          # blank -> no outstanding request (hubp2_set_blank)
        w.append((A(n), 0x2, 0x2, 200))
    m = re.match(r"DIG(\d)_DIG_FIFO_CTRL0$", n)
    # enc32_reset_fifo waits; the setup pulses (link still off) do not. Linux's next write carries RESET_DONE (0x100000)
    # exactly where it had waited for it (box 2026-11-20: a wait after a setup pulse never completed).
    if m and (v & 2) and (nextv & 0x100000):                # FIFO reset -> RESET_DONE 1
        w.append((A(n), 0x100000, 0x100000, 1000))
    elif m and (v & 0x100000) and not (v & 2):              # release (written with the DONE it read) -> RESET_DONE 0
        w.append((A(n), 0x100000, 0, 1000))
    m = re.match(r"DP(\d)_DP_VID_STREAM_CNTL$", n)
    if m and (prev.get(n, 1) & 1) and not (v & 1):          # stream off -> STATUS 0 (enc1_stream_encoder_dp_blank)
        w.append((A(n), 0x10000, 0, 102000))
    return w


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trace", required=True)
    ap.add_argument("--until", type=float, required=True, help="trace time (s) after which nothing belongs to the mode set")
    ap.add_argument("--name", required=True)
    ap.add_argument("--source", required=True)
    ap.add_argument("--rate", default="0x1e", help="LINKUP link rate (DPCD 0x100 value)")
    ap.add_argument("--hdr", default="/Users/dnanu/vermilion-captures/linux-hdr/dcn_3_2_0_offset.h")
    a = ap.parse_args()
    nm = names(a.hdr)
    nm2a = {v: k for k, v in nm.items()}
    nm.update({0x1628A: "DAL_MSG", 0x16273: "DAL_ARG", 0x16274: "DAL_RESP"})
    out, stats, prev = [], {}, {}
    def emit(op, line, reg, val, mask, arg, note):
        out.append((OPS[op], line, reg, val, mask, arg, note))
        stats[op] = stats.get(op, 0) + 1
    state, aux, dal_arg = "pre", None, 0
    nextw, last = {}, {}   # trace line -> the value of the next write to the same register (FIFO wait rule)
    for ln, l in enumerate(open(a.trace), 1):
        m = re.search(r"amdgpu_dc_wreg: reg=0x([0-9a-f]+), value=0x([0-9a-f]+)", l)
        if m:
            r_ = int(m.group(1), 16)
            if r_ in last:
                nextw[last[r_]] = int(m.group(2), 16)
            last[r_] = ln
    for ln, l in enumerate(open(a.trace), 1):
        m = re.search(r"\s([\d.]+): amdgpu_dc_wreg: reg=0x([0-9a-f]+), value=0x([0-9a-f]+)", l)
        d = re.search(r"\s([\d.]+): dmubcmd\d*: .*?c0=0x([0-9a-f]+) c1=0x([0-9a-f]+) c2=0x([0-9a-f]+) c3=0x([0-9a-f]+)", l)
        t = float((m or d).group(1)) if (m or d) else None
        if t is None:
            continue
        if t > a.until:
            break
        if d:
            hdr = int(d.group(2), 16)
            if hdr == 0x3c000180 and state == "pre" and int(d.group(3), 16) & 0xff00 == 0:
                state = "linkoff"; emit("LINKOFF", ln, 0, 0, 0, 1, "D3, TRANSMITTER_CONTROL DISABLE, FEC_READY off (Linux window)")
            continue
        r, v = int(m.group(2), 16), int(m.group(3), 16)
        n = nm.get(r, f"R{r:05X}")
        # the AUX engine: tracked only to find DPCD writes outside the link windows
        if n.endswith("AUX_SW_CONTROL"):
            if not (v & 1):
                aux = []
            elif aux is not None:
                b = aux; aux = None
                if len(b) >= 4 and (b[0] >> 4) == 8 and state in ("mid", "post"):
                    addr = ((b[0] & 0xf) << 16) | (b[1] << 8) | b[2]
                    if addr in (0x107, 0x160):     # MSA_TIMING_PAR_IGNORE / downspread, DSC enable at the sink
                        emit("DPCDW", ln, addr, b[4], 0xff, 0, f"DPCD 0x{addr:03x} = 0x{b[4]:02x}")
            continue
        if n.endswith("AUX_SW_DATA"):
            if aux is not None:
                aux.append((v >> 8) & 0xff)
            continue
        if n == "DAL_ARG":
            dal_arg = v; continue
        if n == "DAL_MSG":
            if v == 9 and (dal_arg >> 16) in (8, 9):
                emit("DAL", ln, 9, dal_arg, 0, 0, f"SetHardMinByFreq {'DISPCLK' if dal_arg >> 16 == 8 else 'DPPCLK'} {dal_arg & 0xffff} MHz")
            continue
        if n == "DAL_RESP" or DROP.search(n):
            continue
        if state == "linkoff":
            if (n, v) == LINK_OFF_END:
                state = "mid"
            prev[n] = v
            continue
        if state == "mid" and n == "DP1_DP_CONFIG":
            state = "linkup"; emit("LINKUP", ln, 0, int(a.rate, 0), 1, 4, f"link rate {a.rate} x4 with FEC_READY, CR + EQ, FEC on (Linux window)")
        if state == "linkup":
            if (n, v) == LINK_UP_END:
                state = "post"
            prev[n] = v
            continue
        if COPY.search(n):
            if PIPE1.match(n):
                src = n
                for p1, p0 in P1TO0:
                    src = src.replace(p1, p0, 1) if src.startswith(p1) else src
                if "GAMUT_REMAP_C" in n:
                    continue      # the coefficients go with the mode register below (both sets, by their own names)
                if n.endswith("GAMUT_REMAP_MODE"):
                    for cs in ("A", "B"):
                        for c in ("C11_C12", "C13_C14", "C21_C22", "C23_C24", "C31_C32", "C33_C34"):
                            d1, d0 = f"MPCC_OGAM1_MPC_GAMUT_REMAP_{c}_{cs}", f"MPCC_OGAM0_MPC_GAMUT_REMAP_{c}_{cs}"
                            emit("COPY", ln, nm2a[d1], nm2a[d0], 0xFFFFFFFF, 0, f"{d1} = {d0} (live)")
                if src not in nm2a:
                    sys.exit(f"line {ln}: no pipe-0 register for {n} ({src})")
                emit("COPY", ln, r, nm2a[src], 0xFFFFFFFF, 0, f"{n} = {src} (live)")
            continue      # pipe 0's own surface / colour: macOS's values stay
        emit("W", ln, r, v, SHARED_MASK.get(n, 0xFFFFFFFF), 0, n)
        for wr, wm, ww, wt in wait_rules(n, v, prev, nm2a, nextw.get(ln, 0)):
            emit("WAIT", ln, wr, ww, wm, wt, f"wait {nm.get(wr, hex(wr))} & 0x{wm:x} == 0x{ww:x} (<= {wt} us)")
        prev[n] = v
    if state != "post":
        sys.exit(f"the trace ended in state {state}: link windows not found")
    sym = "kPhxP0odm_" + a.name
    p = sys.stdout
    p.write("// SPDX-License-Identifier: MIT\n")
    p.write(f"/* ODM mode set {a.name}, generated by tools/capture/p0odm_from_trace.py: do not edit */\n")
    p.write(f"/* source: {a.source}; until t = {a.until} */\n")
    p.write("/* ops: 1 W (reg, val, mask: RMW), 2 COPY (reg = live value of register `val`), 3 WAIT (reg & mask == val, arg = us),\n"
            "   4 DAL (SetHardMinByFreq, val = arg), 5 DPCDW (reg = DPCD address, val = byte), 6 LINKOFF, 7 LINKUP (val = rate,\n"
            "   mask 1 = FEC, arg = lanes), 8 DELAY (arg = us); line = the trace line */\n")
    p.write("#ifndef PHX_P0ODM_TYPES\n#define PHX_P0ODM_TYPES\n"
            "struct phx_p0odm_step { uint8_t op; uint32_t line, reg, val, mask, arg; };\n#endif\n")
    p.write(f"static const struct phx_p0odm_step {sym}[] = {{\n")
    for op, line, reg, val, mask, arg, note in out:
        p.write(f"  {{{op}, {line}, 0x{reg:05X}, 0x{val:08x}, 0x{mask:08x}, {arg}}},  /* {note} */\n")
    p.write("};\n")
    sys.stderr.write(f"{a.name}: {len(out)} steps " + " ".join(f"{k}={v}" for k, v in sorted(stats.items())) + "\n")


if __name__ == "__main__":
    main()
