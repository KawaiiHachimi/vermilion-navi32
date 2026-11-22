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
#   dropped  Linux's own surface and colour values for pipe 0 (macOS's stay; --pixel-format keeps its two format writes), the per-frame OTG interrupt / static-screen /
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
import argparse, os, re, sys

SEG = [0x12, 0xC0, 0x34C0, 0x9000]
OPS = {"W": 1, "COPY": 2, "WAIT": 3, "DAL": 4, "DPCDW": 5, "LINKOFF": 6, "LINKUP": 7, "DELAY": 8, "SWAIT": 9}
DET_CUR, COMPBUF_CUR, SOFT_US = 0x1F00, 0x1F00, 50000


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


NOISE = re.compile(r"VERTICAL_INTERRUPT|GLOBAL_SYNC_STATUS|STATIC_SCREEN|TRIGA_MANUAL_TRIG|DC_GPIO|_AUX_|SURFACE_ADDRESS|"
                   r"FLIP_CONTROL|SURFACE_CONTROL|VMID|CURSOR|^CUR_|SURFACE_FLIP_INT|UCLK_PSTATE_FORCE|DMCUB|^AZ|AFMT|"
                   r"DCCG_AUDIO|OTG0_OTG_GLOBAL_CONTROL2|OTG0_OTG_MASTER_UPDATE_LOCK")
SYM_RATE = {16200: 0x06, 27000: 0x0A, 54000: 0x14, 81000: 0x1E}


def auto_bounds(trace, nm, modesets=1):
    """(until, rate): the mode set = the first two write bursts (> 50 ms apart; 800x600 -> 144 has a 20 ms pause inside link training, per-frame flip / lock noise ignored);
    until = the first OTG0_OTG_MASTER_UPDATE_LOCK = 0 after the second burst; rate = the ENABLE symbol clock."""
    ev, locks, rate, lanes = [], [], None, None
    for l in open(trace):
        m = re.search(r"\s([\d.]+): amdgpu_dc_wreg: reg=0x([0-9a-f]+), value=0x([0-9a-f]+)", l)
        d = re.search(r"\s([\d.]+): dmubcmd\d*: .*?c0=0x([0-9a-f]+) c1=0x([0-9a-f]+) c2=0x([0-9a-f]+)", l)
        if d and int(d.group(2), 16) == 0x3C000180 and int(d.group(3), 16) & 0xFF00 == 0x0100 and rate is None:
            sym = int(d.group(4), 16)
            if sym not in SYM_RATE:
                sys.exit(f"auto rate: ENABLE symbol clock {sym} not known")
            rate = SYM_RATE[sym]
            lanes = int(d.group(3), 16) >> 24
        if not m:
            continue
        t, n, v = float(m.group(1)), nm.get(int(m.group(2), 16), "?"), int(m.group(3), 16)
        if n == "OTG0_OTG_MASTER_UPDATE_LOCK" and v == 0:
            locks.append(t)
        if not NOISE.search(n):
            ev.append(t)
    bursts = [[ev[0], ev[0]]]
    for t in ev[1:]:
        if t - bursts[-1][1] > 0.05:
            bursts.append([t, t])
        bursts[-1][1] = t
    # v4 HDR (2026-11-21): an HDR switch is two full mode sets (amdgpu commits the mode, then the colour space / depth, each
    # with its own link window): --modesets 2 takes bursts 1 and 2
    if len(bursts) < 1 + modesets:
        sys.exit(f"auto until: fewer than {1 + modesets} bursts")
    end = bursts[modesets][1]
    after = [t for t in locks if t > end]
    if not after or (len(bursts) > 1 + modesets and after[0] > bursts[1 + modesets][0]):
        sys.exit("auto until: no OTG0 unlock between the mode set and the next burst")
    if rate is None:
        sys.exit("auto rate: no TRANSMITTER_CONTROL ENABLE")
    if lanes not in (1, 2, 4):
        sys.exit(f"auto lanes: ENABLE lane count {lanes}")
    return after[0], rate, lanes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trace", required=True)
    ap.add_argument("--until", default="auto", help="trace time (s) after which nothing belongs to the mode set; auto = the "
                    "first OTG0 unlock after the mode set's main burst (bursts split at 20 ms gaps, per-frame noise ignored)")
    ap.add_argument("--name", required=True)
    ap.add_argument("--source", required=True)
    ap.add_argument("--rate", default="auto", help="LINKUP link rate (DPCD 0x100 value); auto = from Linux's "
                    "TRANSMITTER_CONTROL ENABLE symbol clock (16200 RBR, 27000 HBR, 54000 HBR2, 81000 HBR3)")
    ap.add_argument("--lanes", default="auto", help="LINKUP lane count; auto = Linux's TRANSMITTER_CONTROL ENABLE (needs --rate auto or --until auto)")
    ap.add_argument("--pixel-format", action="store_true", help="keep Linux's pipe-0 scan-out format writes (HUBP0 "
                    "DCSURF_SURFACE_CONFIG, CNVC_CFG0 SURFACE_PIXEL_FORMAT) as literals: the HDR plans' ARGB2101010 (0xa) and back to "
                    "ARGB8888 (0x8); b149, 10-bit HDR")
    ap.add_argument("--modesets", type=int, default=1, help="mode-set bursts in the step (auto until): 1, or 2 for an HDR switch")
    ap.add_argument("--hdr", default=os.path.expanduser("~/vermilion-captures/linux-hdr/dcn_3_2_0_offset.h"),
                    help="Linux's dcn_3_2_0_offset.h (drivers/gpu/drm/amd/include/asic_reg/dcn), for the register names")
    a = ap.parse_args()
    nm = names(a.hdr)
    nm2a = {v: k for k, v in nm.items()}
    nm.update({0x1628A: "DAL_MSG", 0x16273: "DAL_ARG", 0x16274: "DAL_RESP"})
    if a.until == "auto" or a.rate == "auto":
        u, rt, ln_ = auto_bounds(a.trace, nm, a.modesets)
        if a.lanes == "auto":
            a.lanes = str(ln_)
        if a.until == "auto":
            a.until = u
        if a.rate == "auto":
            a.rate = hex(rt)
    a.until = float(a.until)
    lanes = 4 if a.lanes == "auto" else int(a.lanes)
    out, stats, prev = [], {}, {}
    def emit(op, line, reg, val, mask, arg, note):
        out.append((OPS[op], line, reg, val, mask, arg, note))
        stats[op] = stats.get(op, 0) + 1
    state, aux, dal_arg, div_set = "pre", None, 0, False
    win_rate, win_lanes, windows = None, None, 0   # v4: each link window's own ENABLE (rate, lanes); --rate / --lanes for the first
    det, det_pend, compbuf = {}, set(), None   # DETn size last written, DETs written since the last OTG0 unlock
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
            c1, c2 = int(d.group(3), 16), int(d.group(4), 16)
            # v4: a second DISABLE after a finished window starts another one (an HDR switch: two mode sets)
            if hdr == 0x3c000180 and state in ("pre", "post") and c1 & 0xff00 == 0:
                state = "linkoff"; windows += 1
                emit("LINKOFF", ln, 0, 0, 0, 1, "D3, TRANSMITTER_CONTROL DISABLE, FEC_READY off (Linux window" + (f" {windows})" if windows > 1 else ")"))
            if hdr == 0x3c000180 and c1 & 0xff00 == 0x0100 and state in ("linkoff", "mid") and c2 in SYM_RATE:
                win_rate, win_lanes = SYM_RATE[c2], c1 >> 24   # this window's ENABLE: its link rate and lane count
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
            rate_w = win_rate if (windows > 1 and win_rate) else int(a.rate, 0)
            lanes_w = win_lanes if (windows > 1 and win_lanes in (1, 2, 4)) else lanes
            state = "linkup"; emit("LINKUP", ln, 0, rate_w, 1, lanes_w, f"link rate {hex(rate_w) if windows > 1 else a.rate} x{lanes_w} with FEC_READY, CR + EQ, FEC on (Linux window" + (f" {windows})" if windows > 1 else ")"))
        if state == "linkup":
            if (n, v) == LINK_UP_END:
                state = "post"
            prev[n] = v
            continue
        if COPY.search(n) and n != "HUBPREQ0_DCSURF_SURFACE_PITCH":   # b138: pipe 0's pitch is Linux's (PhxAccel's rows follow it)
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
            if a.pixel_format and n in ("HUBP0_DCSURF_SURFACE_CONFIG", "CNVC_CFG0_CNVC_SURFACE_PIXEL_FORMAT"):
                emit("W", ln, r, v, 0xFFFFFFFF, 0, f"{n} = {hex(v)} (Linux's scan-out format, --pixel-format)")
                continue
            continue      # pipe 0's own surface / colour: macOS's values stay
        if n == "DP_DTO0_PHASE" and not div_set:
            # Linux started from its own 2-pixel-per-clock setup and never wrote OTG_PIXEL_RATE_DIV; macOS starts from the
            # GOP's one pixel per clock (OTG0 field 6). Linux's value for a 2 ppc DP pipe: OTG0 DIVK1 /1, DIVK2 /2 = 2
            # (dccg32_set_pixel_rate_div; box b132 2026-11-20: without it OTG0 ran at half rate, 120 Hz, DIO FIFO errors).
            emit("W", ln, nm2a["OTG_PIXEL_RATE_DIV"], 0x2, 0x7, 0, "OTG_PIXEL_RATE_DIV OTG0 = 2 (2 ppc; not in Linux's trace)")
            div_set = True
        md = re.match(r"DCHUBBUB_DET(\d)_CTRL$", n)
        if n == "DCHUBBUB_COMPBUF_CTRL" and compbuf is not None and (v & 0x1F) > compbuf:
            # hubbub32_program_compbuf_size: before the compressed buffer grows, REG_WAIT every DETn_SIZE_CURRENT ==
            # the size programmed (box b135 2026-11-20: DET1 never released, COMPBUF CONFIG_ERROR, DET0 0, pipe 0 dark)
            for k in sorted(det):
                dn = f"DCHUBBUB_DET{k}_CTRL"
                emit("SWAIT", ln, nm2a[dn], det[k] << 8, DET_CUR, SOFT_US, f"soft wait {dn} SIZE_CURRENT == {det[k]} (compbuf grows)")
        emit("W", ln, r, v, SHARED_MASK.get(n, 0xFFFFFFFF), 0, n)
        if md:
            det[int(md.group(1))] = v & 0x1F
            det_pend.add(int(md.group(1)))
        if n == "DCHUBBUB_COMPBUF_CTRL":
            compbuf = v & 0x1F
        if n == "OTG0_OTG_MASTER_UPDATE_LOCK" and v == 0 and det_pend:
            # the DET sizes written under the lock apply at the next update; the next writes may blank and power-gate
            # the pipe that gives segments back (Linux has a frame here; a replay has microseconds)
            for k in sorted(det_pend):
                dn = f"DCHUBBUB_DET{k}_CTRL"
                emit("SWAIT", ln, nm2a[dn], det[k] << 8, DET_CUR, SOFT_US, f"soft wait {dn} SIZE_CURRENT == {det[k]} (after the unlock)")
            det_pend.clear()
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
            "   mask 1 = FEC, arg = lanes), 8 DELAY (arg = us), 9 SWAIT (as WAIT; a timeout is counted, not a failure: Linux's\n"
            "   REG_WAIT only logs); line = the trace line */\n")
    p.write("#ifndef PHX_P0ODM_TYPES\n#define PHX_P0ODM_TYPES\n"
            "struct phx_p0odm_step { uint8_t op; uint32_t line, reg, val, mask, arg; };\n#endif\n")
    p.write(f"static const struct phx_p0odm_step {sym}[] = {{\n")
    for op, line, reg, val, mask, arg, note in out:
        p.write(f"  {{{op}, {line}, 0x{reg:05X}, 0x{val:08x}, 0x{mask:08x}, {arg}}},  /* {note} */\n")
    p.write("};\n")
    sys.stderr.write(f"{a.name}: {len(out)} steps " + " ".join(f"{k}={v}" for k, v in sorted(stats.items())) + "\n")


if __name__ == "__main__":
    main()
