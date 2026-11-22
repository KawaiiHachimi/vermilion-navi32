# 2026-11-19: why the desktop runs at 60 Hz, and why HDR doesn't work (XTX machine)

Measured from macOS on the XTX machine itself (VermilionBoot b126, GPU present on, LG ULTRAGEAR+ on DP B at
2560x1440 60 Hz), read-only: sysctls, `ioreg`, `system_profiler`, `/var/log/phxboot.log`, one `phxctl regs` call
(25 MMIO reads, root). No register, sysctl, SMU, DMUB, DAL or file change on the machine. The EDID was decoded locally
and is not kept (its serial descriptor is left out here).

## Measured state

| What | Source | Value | Meaning |
|---|---|---|---|
| macOS modes | `system_profiler`, `debug.phxfb_nmodes` | one mode, 2560x1440 @ 60.00 Hz; `nmodes 0` | head 0 runs the single GOP mode, no list |
| pipe-0 engine | `debug.phxpipe0_enable` / `_modes` / `_stat` | 0 / 0 / never ran | off (XTX flags have no `phxboot-aoc*`) |
| OTG0 totals | `0x4FEA` / `0x4FEF` | `0xa9f` / `0x5c8` = 2720x1481 | the EDID's 1440p59.95 DTD (241.5 MHz) |
| OTG0 pixel DTO | `DP_DTO0_PHASE/MODULO` `0x141/0x142` | 241,500,000 / 716,666,667 | 241.5 MHz |
| Socket | `debug.phxdpwatch_stat` | DP B lit (DIG1, HPD2), DP A / DP E dark | settles the audit's open question: the LG is on DP B |
| DP lanes | `DP1_DP_CONFIG` `0x56CB` | `0x3` | 4 lanes |
| DP link rate | `DP1_DP_VID_M/N` `0x56D2/0x56D1` | `0x395e` / `0x8000` (M/N = 0.4482) | 241.5 MHz / 0.4482 = 538.8 MHz symbol clock = **HBR2** (5.4 Gbit/s per lane, with downspread). Upstream's AOC: HBR |
| DP link state | `DP1_DP_LINK_CNTL` / `DP_VID_STREAM_CNTL` | `0x110` / `0x10201` | trained, stream running |
| Output colour depth | `DP1_DP_PIXEL_FORMAT` `0x56C9`, MSA MISC0 `0x56CA` | `0x03000000`, MISC0 `0x60` | **12 bpc RGB**, VESA range, on the wire (the EDID declares 12 bpc) |
| Secondary packets | `DP1_DP_SEC_CNTL` `0x56EB`, `DP_MSA_MISC` `0x56CE` | `0`, `0` | no SDPs at all: no HDR infoframe, no VSC colorimetry |
| Scan-out format | `HUBP0_DCSURF_SURFACE_CONFIG` `0x3AA5` | `8` | ARGB8888 |
| DISPCLK/DPPCLK | `DENTIST_DISPCLK_CNTL` `0x124` | `0x40194040` | divider ID 0x40 for both; upstream's 1080p144 plans need <= 0x33 (higher clock) |
| DET | `DCHUBBUB_DET0_CTRL` `0x397B`, `debug.phxpipe2_det` | `0x303`, 3 | 3 segments; the pipe-0 engine wants >= 9 above 60 Hz |
| Stutter | `DCHUBBUB_ARB_DRAM_STATE_CNTL` `0x39BC` | `0x1002` | forced off (as the daemon leaves it) |
| Watermarks A | `0x39BE`, `0x39C1-0x39C4` | `0x1fe`, `0x4ba`, `0x424`, `0x4ee8`, `0` | the GOP's, for 1440p60 |
| **DMCUB version** | `DMCUB_SCRATCH1` `0x36A4` | **`0x07008748`** | **not** upstream's `0x07008742`: every DMUB-gated feature refuses on this card |

### EDID (LG ULTRAGEAR+, 256 of 384 bytes read; serial left out)

- EDID 1.4, digital, DisplayPort, **12 bpc**. Range descriptor: 48-240 Hz, max pixel clock 1060 MHz. AMD FreeSync VSDB.
- Base DTD: 2560x1440 @ 59.951 Hz, 241.50 MHz, totals 2720x1481.
- CTA block: DTD 2560x1440 @ **143.991 Hz**, 593.75 MHz, totals 2720x1516; DTD 2560x1440 @ **119.998 Hz**, 497.75 MHz,
  totals 2720x1525. Same horizontal timing as the 60 Hz mode, only the vertical blank grows.
- CTA **HDR static metadata**: EOTF SDR + **PQ (SMPTE ST 2084)**, static metadata type 1, max 604 nits, max FALL
  277 nits, min ~0.0001 nits. **Colorimetry**: BT.2020 RGB and BT.2020 YCC.
- Extension count 2, but only block 1 is read (no E-DDC segment pointer). The **240 Hz (and any 165 Hz) timing is not in
  the first 256 bytes**, so it is most likely in block 2 (DisplayID), which is never read (unverified until block 2 is read).

## Why 60 Hz: the chain of limits (in order)

1. **macOS is offered one mode.** Head 0's mode table (`phxhead.h` `kHead0Modes`) holds only upstream's two 1080p AOC
   modes. Even with `debug.phxpipe0_modes=1`, `modeList()` finds nothing for a 2560x1440 surface, so head 0 keeps the
   single GOP mode. The offer gate is open but has nothing to offer.
2. **No pipe-0 plan for 1440p.** `phx_p0_plan_1080p*.inc` only; the survey refuses OTG0 totals `0xa9f/0x5c8`. The
   survey's `DP_VID_M` check is hard-wired to the AOC's HBR link (`kVidM60 = 0x468b` at 148.5 MHz) and would refuse the
   XTX's HBR2 link even with a plan.
3. **12 bpc output doesn't fit higher rates on the trained link.** HBR2 x4 carries 17.28 Gbit/s of payload (8b/10b):

   | Mode | 8 bpc | 10 bpc | 12 bpc (today) |
   |---|---|---|---|
   | 1440p60 (241.5 MHz) | 5.8 | 7.2 | 8.7 fits |
   | 1440p120 (497.75 MHz) | 11.9 fits | 14.9 fits | 17.9 **no** |
   | 1440p144 (593.75 MHz) | 14.3 fits | 17.8 **no** (3 % over) | 21.4 **no** |
   | 1440p240 (~995 MHz, estimated from 2720x1525) | ~23.9: needs **HBR3 x4** (25.9) | ~29.9: needs **DSC** | no |

   So **120 Hz and 144 Hz need no link retraining**, the same trick upstream uses at 1080p144 (re-time pipe 0 on the
   GOP's link), but the plan must also lower the output depth (DP pixel format, MSA MISC0, the FMT/OPP depth and dither
   registers), which no upstream plan does. 120 Hz can keep 10 bpc; 144 Hz must go to 8 bpc.
4. **Clocks, DET and watermarks are the GOP's 1440p60 values.** DISPCLK/DPPCLK divider 0x40 is lower than upstream needs
   even for 1080p144; DET is 3 segments; the watermarks are for 60 Hz. Upstream raises DISPCLK/DPPCLK with two DAL hard
   minimums and sets DET with `det.sh` before a 144 set. The right 1440p values must come from a Linux capture on this
   card and monitor (DCN 3.2 on the XTX, not the XT's 1080p values).
5. **240 Hz needs DP link training on DP B.** `PhxDpLink.h` trains only DP A / DP E and only up to HBR2; DP B is
   excluded on purpose (the GOP owns it). HBR3 on DP B means taking the link over from the GOP: the riskiest step, and
   at 10 bpc DSC on top. Plus the EDID block 2 read to even see the 240 Hz timing.
6. **DMCUB `0x07008748`.** The pipe-0 engine uses no DMUB command, so 120/144 are not blocked by it. Everything that
   is (pipe-2 engine, DMUB restart, DP training through `TRANSMITTER_CONTROL`, which 240 Hz would need) refuses until the
   `0x07008742` gate learns the XTX version; the command layout of `0x07008748` must be checked against Linux first.

## Why no HDR: what macOS needs vs what Vermilion does

The macOS side (IOGraphics, `IOGraphicsTypes.h` in the SDK on this machine): a framebuffer reports HDR per mode in
`IODetailedTimingInformationV2` (`pixelEncoding`, `bitsPerColorComponent`, `colorimetry`, `dynamicRange` =
`kIODynamicRangeHDR10`), the supported ranges (`supportedColorimetryModes`, `supportedDynamicRangeModes`), the
connection's depths (`kConnectionColorDepthsSupported` / `kConnectionControllerDepthsSupported` with
`kIODisplayRGBColorComponentBits10`), a 30-bit pixel format, and takes the mastering metadata through
`setAttribute(kIOFBHDRMetaDataAttribute 'hdrm', IOFBHDRMetaData *)`. Which of these CoreDisplay actually requires before
it shows the "High Dynamic Range" switch is **unverified**: Apple's own AMD framebuffer (`AMDRadeonX6000Framebuffer`,
inside the boot kernel collection on this install) is the reference to read.

| Needed | Vermilion today |
|---|---|
| Monitor advertises HDR | **Yes**: PQ, BT.2020, 604 nits (EDID block 1, which is read) |
| >= 10 bpc on the wire | **Yes at 60 Hz**: the GOP drives 12 bpc. Not at 144 Hz on HBR2 (see table) |
| 10-bit scan-out | **No**: HUBP0 scans ARGB8888. WindowServer already composites into 10-bit `l10r` (ARGB2101010) surfaces, and the plugin narrows them to 8 bit (`scan_l10r.comp`, `nvIopRowSSE`) |
| Framebuffer reports 10-bit / HDR | **No**: `phxfb.cpp` reports `IO32BitDirectPixels` only, `bitsPerComponent 8`, depths `0x2` (8 bit), RGB only; the timing info has no colorimetry / dynamic range; `'hdrm'` falls through to IOFramebuffer |
| HDR static metadata infoframe (DP SDP) | **No**: `DP1_DP_SEC_CNTL = 0`, no SDP code (`PhxDpLink.h` skips SDP / VSC on purpose) |
| BT.2020 colorimetry signalling (MSA MISC1 or VSC SDP) | **No** |
| PQ output transfer (head-0 regamma, MPCC OGAM0) | **No**: head-0 gamma tables are counted, never written (`PhxGamma.h` drives MPCC_OGAM1 = pipe 1 only) |

HDR at 60 Hz is the reachable part: the link already carries 12 bpc. HDR at 120 Hz fits HBR2 at 10 bpc; HDR at 144 Hz
or 240 Hz needs HBR3 or DSC.

## Where the Linux sequences come from

All writes in the plan below must be ones Linux amdgpu DC sends on this ASIC: DCN 3.2 register writes for the mode set
(OTG, HUBP DLG/TTU, DET, watermarks, FMT/OPP depth, DP pixel format / MSA), the DAL clock hard minimums, the HDR SDP
(`enc3_stream_encoder_update_dp_info_packets` path, generic SDP registers) and MSA/VSC colorimetry. Linux trains this
sink at its highest rate (most likely HBR3, unverified), so a capture at 120/144 should force HBR2 x4 (debugfs
`link_settings` on the Linux side) and `max bpc` 8/10 to match what the GOP left on macOS.

## Proposed plan (smallest safe steps first; nothing done yet)

| # | Step | Kind | Needs |
|---|---|---|---|
| 1 | Read EDID block 2 (E-DDC segment pointer, 384 bytes) and log it; confirm the 240/165 Hz timings | kext change (DDC read only, no display write) | 1 boot |
| 2 | Linux capture, same card and monitor, root, read-only: DCN registers + `amdgpu_dm_dtn_log` + DPCD at 1440p 60/120/144 with `link_settings` HBR2 x4 and `max bpc` 10/8; one at 1440p60 with HDR on (SDP, MSA MISC1, OGAM0 registers); trace of the mode set | Linux session | 1 Linux boot, cold power-off back |
| 3 | Reverse-read how `AMDRadeonX6000Framebuffer` reports HDR to IOGraphics (timing info, depths, `'hdrm'`) | read-only, on the Mac | none |
| 4 | 1440p120 at 10 bpc: plan `phx_p0_plan_1440p120.inc` from step 2, head-0 table rows chosen by the EDID DTDs, survey's `VID_M` check derived from the live link (HBR2 case next to the HBR one), depth registers in the plan; DAL DISPCLK/DPPCLK hard minimums and DET as Linux sends them (**owner OK for the DAL messages**) | kext + accel change, host tests | 1 boot, then the switch from System Settings |
| 5 | 1440p144 at 8 bpc: second plan, same path | kext change | 1 boot |
| 6 | 10-bit scan-out: HUBP0 ARGB2101010 for `l10r` surfaces (drop the 8-bit narrowing); framebuffer reports 10 bpc | kext + plugin + accel | 1 boot |
| 7 | HDR at 60/120 Hz: report HDR10/BT.2020 per mode, take `'hdrm'`, send the HDR SDP and colorimetry, PQ regamma on head 0 | kext + accel, needs 3 and 2 | 1-2 boots |
| 8 | 240 Hz: HBR3 link training on DP B (taking the link from the GOP), DMCUB `0x07008748` gate, maybe DSC | large; separate plan | several boots |

## Step 3 done: how Apple's own AMD driver offers HDR (2026-11-19, read-only)

Read from `AMDRadeonX6000Framebuffer` (the Navi 1x/2x framebuffer, a fileset entry in
`/System/Library/KernelCollections/SystemKernelExtensions.kc` on this 26.7.1 install; its symbol table is kept),
carved out and disassembled on the Mac. Nothing was run or loaded. No Apple code is copied into the repo; only what it
reports and which selectors it handles are noted.

| What | Apple's AMD driver | Vermilion (b126 / PhxAccel) |
|---|---|---|
| `getTimingRange` (`IODisplayTimingRangeV2`) | `AmdAsicInfoNavi2::getTimingRange` fills `supportedPixelEncoding 0x0b`, `supportedBitsPerColorComponent 0x0e` (8/10/12), `supportedColorimetryModes 0x1cb` (incl. BT.2020), **`supportedDynamicRangeModes 0x27` (SDR, HDR10, ...)**, `maxBandwidth` and the DSC limits | not overridden: IOFramebuffer's default, so no HDR range is offered (most likely the first gate, unverified) |
| Per-mode timing (`IODetailedTimingInformationV2` `bitsPerColorComponent`, `colorimetry`, `dynamicRange`) | filled from its own timing list | zero |
| HDR mastering metadata | `setAttribute('hdrm')` handled | falls through to IOFramebuffer |
| HDR infoframe | `setAttributeForConnection('dsdp', AGDCFBSDPPacket_t *)` -> `processSdpInfoPacket` (37-byte packet kept per display path; `getAttributeForConnection('dsdp')` returns it, `'dsdo'` is also answered). IOGraphics has no `'dsdp'`/`'hdrm'`: CoreDisplay sends both directly | falls through (unsupported) |
| Depth / colour selectors | `getAttributeForConnection` answers `'colr'`, `'cyuv'`, `' grd'`, `' dpd'`, `' gdc'` per link | `'colr'`/`'cyuv'` = RGB only, depths `0x2` (8 bit) |
| AGDC `0x711` (FB extended capability) | types 1 CSC, 2 gamma, 4 degamma, 0x10 scaler, 0x8000 cursor; pixel formats `0x70007` / `0x50005` (incl. 10-bit `BGRX 10` / `BGRA 10`) | only type 0x10 (scaler) answered; 16 other `0x711` calls refused this boot (`log show`), plus `0x925`, `0x941`, `0x2001`, `0x3` |
| AGDC `0x921` link config timing | carries `pixelEncoding` / `bitsPerColorComponent` / `colorimetry` / `dynamicRange` | those fields zero |

What this means for step 7: before any SDP or regamma register is written, a **report-only probe** can test what macOS
needs: PhxAMDFB reports the timing range (8/10/12 bpc, BT.2020, SDR | HDR10) and 10 bpc depths, and only logs `'hdrm'`
and `'dsdp'` (what CoreDisplay sends and when). If System Settings then shows the HDR switch, the remaining work is the
hardware side (10-bit scan-out, the SDP from the `'dsdp'` packet as Linux sends its HDR infoframe, PQ on head 0). Risk
of the probe: with HDR switched on, CoreDisplay sends PQ-encoded pixels to a monitor still in SDR (washed-out picture)
until it is switched off again; so it goes behind a boot-arg, off by default (unverified).

## Step 1 prepared: VermilionBoot b127 (built, not installed)

`debug.phxedid_full` (root, read) reads up to 4 EDID blocks; blocks 2-3 through the E-DDC segment pointer over AUX,
in Linux's message order. Host test `tests/kext/edid_full_test.cpp` (fake AUX engine + E-DDC sink, 27 checks: the
LG's 384 bytes, segment-0 sequence identical to b126's `readEdid`, no-E-DDC sink, bad block 2, 512 bytes, capping).
After the boot: `sudo sysctl -w debug.phxedid_src=0x22` (a kext variable, no register), then
`sudo sysctl -b debug.phxedid_full > edid.bin`; strip the serial before quoting it. The segment write itself is
unverified on hardware.

## Linux side (captured 2026-11-20, Linux 7.2.15 amdgpu, Hyprland 0.56.2, LG ULTRAGEAR+ on DP-3)

Capture files: EFI partition `/boot/vermilion/capture/display-20261120-{001650,001722,001737}/` (not in git). All 2560x1440,
4 lanes, `output_bpc` max 12, FEC/DSC sink support yes. Linux picks the link rate itself from the mode (DPCD 0x100:
0x0a = HBR 2.7, 0x14 = HBR2 5.4, 0x1e = HBR3 8.1).

| Step | Hz (OTG0 fps) | Link (current) | DSC | max_bpc | DP_PIXEL_FORMAT | DISPCLK = DPPCLK kHz | DCFCLK / deep sleep kHz | VID_M/N |
|---|---|---|---|---|---|---|---|---|
| 60 | 59.951 | HBR x4 | off | 8 | 0x41000000 | 245714 | 199000 / 17357 | 0x72b9/0x8000 |
| 120 | 119.998 | HBR2 x4 | off | 8 | 0x41000000 | 505882 | 199000 / 35775 | 0x763e/0x8000 |
| 144 | 143.991 | HBR2 x4 | off | 8 | 0x41000000 | 614285 | 199000 / 42675 | 0x8d0b/0x8000 |
| 240 | 239.97 | **HBR3 x4** | **on** (bpp 256 = 16.0, slice 640x120, 2 slices, chunk 1280) | 8 | 0x41000000 | 537500 | 199000 / 71486 | 0xa75e/0x8000 |
| hdr-60 | 59.951 | HBR x4 | off | 10 | **0x42000000** | 245714 | same as 60 | 0x72bc/0x8000 |
| hdr-120 | 119.998 | HBR2 x4 | off | 10 | **0x42000000** | 505882 | same as 120 | 0x763b/0x8000 |
| hbr2-60 / 120 / 144 | as above | same as unforced (Linux already picks HBR x4 at 60, HBR2 x4 at 120/144) | off | 8 | 0x41000000 | as above | as above | as above |
| hbr2-120-10 | 119.998 | HBR2 x4 | off | 10 | 0x42000000 | 505882 | 35775 | 0x763a/0x8000 |

(DP_PIXEL_FORMAT 0x41 = 8 bpc, 0x42 = 10 bpc.) FCLK 601000 kHz in every step.

- **Result for macOS:** 120 Hz at 10 bpc and 144 Hz at 8 bpc need only HBR2 x4, which matches the link the firmware trains.
  240 Hz needs HBR3 + DSC on Linux (DSC 2 slices, 16 bpp); not a target.
- **Watermarks (DTN, set A):** 120 Hz: data_urgent 19.28, sr_enter 61.52, sr_exit 54.54, dram_clk_change 419.28 (all us); 240 Hz:
  20.18 / 62.26 / 55.30 / 420.18. Set B 13.0 / 55.24 / 48.26 / 413.0 at 120 Hz. They barely move with refresh rate.
  The DET segment table is in each step's `amdgpu_dm_dtn_log.txt` (not extracted here).
- **HDR:** Hyprland `cm, hdr` + `bitdepth, 10` was accepted (10 bpc, DP_PIXEL_FORMAT 0x42). The hdr-60 trace wrote many registers
  the plain 60 trace did not (inferred from a set difference of two traces whose starting states differ, so treat as candidates):
  0x3542/49/4b, 0x36b8, 0x397c, 0x3b8f-92, 0x3bb8, 0x3c04, 0x42f0, 0x4d6e-74, 0x4f24/25, 0x4f8d/8e, 0x53e5, 0x5712/13/21,
  0x64c0, 0x651c and a block at 0x90xx. The OGAM/SDP assignment of these is **not verified**; map them against the DCN 3.x
  register headers before use. hdr-120 vs 120 differs only in 0x90f4-0x90f9.
- 0x90f4-0x90ff appears when the pass starts from a different state (also in hbr2-120 vs 120), so it is probably a
  per-session register and not HDR or HBR2 specific (inferred).
- **Tooling bug fixed:** Hyprland 0.56 uses Lua config; `hyprctl keyword monitor` no longer works, so the script now sends
  `hyprctl eval 'hl.monitor{...}'`. The first run of all steps (every step captured 240 Hz) was deleted.
- The optional draft pipe-0 plans (hbr2-120/144 traces into `phx_p0_plan` form) are not done.

## Capture read on macOS (2026-11-20): what a 1440p pipe-0 plan needs

Read from the EFI partition copy (scratchpad only) and compared with the firmware's live 1440p60 state (one `phxctl regs`
read of 87 registers on macOS, same boot). Register names from Linux v6.8 `dcn_3_2_0_offset.h` (DCN bases 0x12 / 0xC0 /
0x34C0 / 0x9000; the `0x90xx` block is MPCC0/OGAM0, `0x1627x/0x1628a` the DAL mailbox).

- **Same pipe as macOS:** Linux drives the LG from OTG0 / HUBP0 / DIG1 / DP1 at every step, so captured values map onto
  the pipe-0 engine's registers one to one. The EDID block 2 is a DisplayID 1.x block (the 240 Hz timing).
- **Convention:** Linux runs DP at 2 pixels per cycle (DTO phase = pclk / 2, `DP_PIXEL_FORMAT` bit 30, `DP_VID_TIMING`
  N_MUL 1); the firmware at 1 (phase = pclk, modulo 716,666,667). Upstream's 1080p plans keep the firmware convention with
  Linux's other values (their `REF_FREQ_TO_PIX_FREQ` = 50 MHz reference, as Linux's 1440p values: 0xcdb9 at 497.75 MHz,
  0xac76 at 593.75 MHz). The 1440p plans do the same.
- **Plan values (Linux, HBR2 x4):** OTG timing (only V_TOTAL / V_BLANK change: 0x5f4 at 120, 0x5eb at 144), frame position
  (VSTARTUP 0x54/0x4b, VUPDATE 0xf702a8/0xf602a8, VREADY 0x148), HUBP DLG/TTU (13 registers), MSA timing. 10 bpc changes
  none of them. DSCL0 OTG H/V blank were not written by Linux; they equal the OTG H/V blank values (as in upstream's plans).
- **Depth (new for the engine):** the firmware sends 12 bpc (`DP_PIXEL_FORMAT` depth 3, MISC0 0x60, `FMT_BIT_DEPTH_CONTROL` 0).
  Linux: 8 bpc = depth 1, MISC0 0x20, FMT 0xc900 (spatial dither to 8); 10 bpc = depth 2, MISC0 0x40, FMT 0xd100.
- **Clocks:** Linux sends DAL `SetHardMinByFreq` (msg 0x09) DISPCLK + DPPCLK 245 MHz at 60, **505 MHz at 120, 614 MHz at
  144** (DENTIST divider IDs 0x43 / 0x22 / 0x1c); the firmware left 0x40. Linux also sends DCFCLK 199 MHz, UCLK 96 MHz (the
  daemon pins UCLK 456 instead), deep-sleep DCFCLK (0x0d), display count (0x0e), 0x10, and **0x14 = SetAlwaysWaitDmcubResp**
  (hung the PMFW with the VBIOS DMCUB in upstream's work: never sent) and 0x15. Upstream's pipe-1 switch already sends only
  the two DISPCLK/DPPCLK hard minimums and lets the PMFW move DENTIST; the same for pipe 0.
- **DET:** Linux 4/0/0/0 segments at 60/120/144 (9 at 240), COMPBUF 16; firmware 3/3/3/3, COMPBUF 2. The engine's "DET0 >= 9
  above 60 Hz" rule is the XT's two-pipe 1080p value; the 1440p plans carry Linux's 4.
- **Watermarks:** Linux's sets A-D are the same at 60/120/144 and about twice the firmware's (urgency 0x3c4 vs 0x1fe,
  UCLK p-state 0x51e4 vs 0x4ee8). Upstream ran 1080p144 on the firmware's watermarks; the first 1440p attempt does too,
  with the engine's underflow check (100 samples over 1 s, else automatic fall-back to 60).
- `DP_VID_M` on HBR2: 0x395e at 241.5 MHz (firmware), so 0x7c74 / 0x9466 expected at 497.75 / 593.75 MHz (unverified).

## b128 on hardware (2026-11-20 ~00:55, XTX machine, LG ULTRAGEAR+ on DP B): 120 and 144 Hz work

Boot with VermilionBoot b128 (b126 EFI as `OC.prev`); desktop armed normally (daemon SUCCESS 00:50:01). From a root shell,
with WindowServer running (macOS still believes 60 Hz: no PhxAccel change yet):

| Step | Result | Read after |
|---|---|---|
| `debug.phxpipe0_enable=1`, surveys (reads only) | 60: refused on the stale OPTC0 sticky only (1440p60 plan matched, HBR2 M check passed); 120: refused on DENTIST (expected) | 0 writes |
| `mode=120`, `debug.phxpipe0=4` | **done**: sticky cleared, DET 4/0/0/0 (4 writes), 2 DAL hard mins (505 MHz), round trip at 60, set; 126 frames in the ~1.05 s window; stream up after 8.5 ms; 1.06 s shell | V_TOTAL 0x5f4, DENTIST 0x221f2222 (= Linux), DP_VID_M 0x763e (= Linux), depth 10 bpc, no underflow |
| `mode=144` | **done**: 2 DAL hard mins (614 MHz); 152 frames / ~1.05 s | V_TOTAL 0x5eb, DENTIST 0x1c191c1c (= Linux), DP_VID_M 0x8d0b (= Linux), 8 bpc dithered, no underflow |
| `mode=60` | **done**: back to the GOP's plan (every plan register read back), 62 frames / ~1.05 s | DP_VID_M 0x395f |

Owner, at each step: the monitor's OSD showed **120 Hz** and **144 Hz**, the picture looked normal. The power firmware moved
DENTIST by itself after the hard minimums (no DENTIST write needed, as upstream's pipe-1 switch). The firmware's watermarks
were enough at 144 (no underflow in the verify window; longer runs unverified). Not tested yet: macOS rendering at the new
rate (needs the head-0 mode list), sleep/wake, a cold boot after a switch (the GOP relights 1440p60 each boot).

## 120/144 Hz from System Settings (2026-11-20 morning, XTX machine): works

PhxAccel with the head-0 1440p rows + the daemon's `phxboot-p0modes` flag, after a full power-off: daemon log
`p0modes: pipe-0 engine on, head-0 mode list armed`, `debug.phxfb_nmodes` 3, System Settings offers 60/120/144. Owner
picked 144: the monitor shows 144 Hz, "less input lag, less screen tearing, everything is better". Read after:
`debug.phxpipe0_stat` mode=144 last=done, 150 frames per window, VID_M 0x8d07, no fall-back. Not tested yet: the rate
after a reboot (the GOP lights 60 each boot), sleep/wake, hours at 144.

## 240 Hz research (2026-11-20, read-only)

**Monitor (full EDID via b127 `debug.phxedid_full`, src 0x22, macOS):** the E-DDC segment read works on hardware: 3
blocks, 384 bytes. Block 2 is DisplayID 1.2 with one Type I timing: **2560x1440, 1056.75 MHz, H total 2720 (blank 160,
front 48, sync 32, +), V total 1619 (blank 179, front 3, sync 5), 239.96 Hz**. Kept outside git
(`~/vermilion-captures/`, has the serial).

**DPCD (Linux capture):** base caps DPCD 1.2, max rate 0x14 (HBR2!), 4 lanes, TPS3; extended caps (0x0E bit 7, 0x2200)
DPCD 1.4, **max rate 0x1e (HBR3)**. A trainer that reads only 0x001 (b116 `PhxDpLink.h`) sees HBR2. DSC 1.2 supported
(0x060 = 0x01), FEC capable (0x090 = 0xbf), downspread on.

**Linux at 240 Hz (capture 001650/240):** HBR3 x4, **FEC + DSC on** (16 bpp, 2 slices 640x120 per half), **ODM combine
2:1** (OTG0 drives OPP0 + OPP1, HUBP0 + HUBP1, DSC0 + DSC1; `odm_combine_segments` 2, OPTC_DATA_SOURCE_SELECT 0x00100001,
OPTC_WIDTH_CONTROL 0x02800500), 2 pixels per cycle (DP_DTO0_PHASE 0x1f7e5cd8 = 528.375 MHz = pclk / 2), DISPCLK 537.5 MHz,
DET 9/9/0/0, V_TOTAL 0x652. The 240 trace writes 220 registers the 144 trace does not: DSC (2 engines, the PPS words),
the second pipe (HUBP1 DLG/TTU, DPP1, MPCC1, OPP1, DSCL1), DSCRM, DP_DSC_CNTL, DP_SEC_CNTL2 / GSP11 (the PPS SDP).
- **The link training itself is not in the register trace:** DC sends the PHY steps (`TRANSMITTER_CONTROL`, VBIOS 128/1)
  and every AUX/DPCD access through the DMCUB inbox (1902 `DMCUB_INBOX1_WPTR` writes in the 240 switch); only the
  training-pattern registers (`DP1_DP_DPHY_TRAINING_PATTERN_SEL`, `DP_DPHY_CNTL`, scrambler) are visible. The payloads
  live in the ring in VRAM. `tools/capture/linux_display_capture.py` now records them (kprobe on `dmub_srv_cmd_queue`,
  16 dwords per command, interleaved with the register writes; untested on Linux yet).
- **Bandwidth:** 1056.75 MHz x 24 bit = 25.36 Gbit/s; HBR3 x4 8b/10b = 25.92 Gbit/s (97.8 %), with FEC (3 %) 25.14 =
  does not fit. So **240 Hz at 8 bpc fits HBR3 without FEC/DSC** (inferred; Linux turns FEC on for any FEC-capable sink,
  which is why it needs DSC). 240 Hz at 10 bpc (HDR) = 31.7 Gbit/s: **needs DSC** in any case.
- **1 pixel per cycle cannot reach 1056 MHz:** the firmware's DTO convention (phase = pclk, modulo 716,666,667) tops out
  near 716 MHz. A 240 Hz plan must use Linux's 2-pixel-per-cycle convention (DTO phase = pclk / 2, `DP_PIXEL_FORMAT`
  bit 30, `DP_VID_TIMING` N_MUL) (inferred from the register values).
- DCN 3.2 runs ODM 2:1 for a single display as a power policy (`enable_single_display_2to1_odm_policy`, Linux source);
  whether one pipe can carry 1056.75 MHz (DISPCLK/DPPCLK >= ~1.06 GHz) on this card is unverified. The `240-nodsc`
  capture step asks Linux to light 240 with DSC forced off.

**macOS side today:** b116's trainer (`PhxDpLink.h`) speaks TRANSMITTER_CONTROL ENABLE / SETUP_VSEMPH / DISABLE and
8b/10b CR + EQ for DP A / DP E up to HBR2 (TPS2/3), behind the DMCUB version gate `0x07008742`; this card runs
`0x07008748`. HBR3 needs TPS4 if the sink has it (0x003 bit 7: 0x81 in 0x2203 = yes), else TPS3 (inferred, Linux
`decide_eq_training_pattern`).

## Linux capture v2 (2026-11-20): DMUB commands for 144 -> 240 Hz, 240 Hz at 10 bpc, HDR

Files: EFI partition `/boot/vermilion/capture/display-20261120-065140/` (not in git). Steps `base,144,240,144,240-10,hdr-240,240-nodsc,hdr-120`,
Linux 7.2.15 amdgpu, Hyprland 0.56.2, LG ULTRAGEAR+ on DP-3. Header layout and enum values checked against `dmub_cmd.h` of tag v7.2.

- **Tool bugs fixed:** the DMUB kprobe symbol on this kernel is `dmub_srv_fb_cmd_queue` (the `dmub_srv_cmd_queue` name does not exist).
  Python's buffered text-mode append to `kprobe_events` returns EINVAL on Linux 7.2 tracefs; the script now uses one raw `write(2)`.
  Per command: 16 dwords from `%si` (the command union), `phxcap/dmubcmd1` lines in `trace-modeset.txt`.
- **No DP_AUX_ACCESS (type 70) went through DMUB** in any step. The AUX transactions for link training (DPCD 0x100/0x101, 0x120,
  0x160, lane settings) are not in the DMUB trace; they use the register AUX engine (not captured by design: the script never reads
  the AUX data FIFOs). The macOS side must take the AUX bytes from the DPCD dumps (`dpcd-*.bin`) and the DP spec.
- **What DMUB did carry per mode set (counts of commands):**

  | Step | total | 128/1 TRANSMITTER_CONTROL | 75/x CAB_FOR_SS | 76/x FAMS (`FW_ASSISTED_MCLK_SWITCH`) |
  |---|---|---|---|---|
  | 144 | 1243 | 22 | 107 | 1114 |
  | 240 | 2043 | 22 | 179 | 1842 |
  | 240-10 | 2047 | 22 | 181 | 1844 |
  | hdr-240 | 2036 | 22 | 174 | 1840 |
  | hdr-120 | 1049 | 22 | 97 | 930 |
  | 240-nodsc | 2099 | **0** | 180 | 1919 |

  Types 75/76 (power-feature and FAMS DRR/manual-trigger housekeeping, ~1 per 0.1 ms, e.g. `76/1` DRR_UPDATE pl 12, `76/3`
  SET_MANUAL_TRIGGER) are not part of link bring-up; their count tracks the duration, not the mode. Ignore them for the port.
- **The 22 TRANSMITTER_CONTROL commands** (type 128, subtype 1 = `DMUB_CMD__VBIOS_DIG1_TRANSMITTER_CONTROL`, header dword
  `0x3c000180`, payload 60 bytes), identical in every step except the symclk dword; dwords 1..3 after the header:

  | # | dword1 | dword2 (symclk, 10 kHz) | dword3 | meaning (inferred from the atombios transmitter control v1.7 field order) |
  |---|---|---|---|---|
  | 1 | `00000001` | 0 | `f4130003` | phyid 1 (UNIPHYB), action 0 DISABLE |
  | 2 | `04000101` | **81000 (0x13c68) at 240 Hz; 54000 (0xd2f0) at 120/144 Hz** | `f4000003` | action 1 ENABLE, 4 lanes |
  | 3-6 | `04000b01` | same | `f6130003` | action 0x0b SETUP_VSEMPH, lane-setting byte 0 (x4) |
  | 7-10 | `04010b01` | same | `f6130003` | lane-setting byte 1 (x4) |
  | 11-22 | `04020b01` | same | `f6130003` | lane-setting byte 2 (x12) |

  So HBR3 is symclk 81000 and HBR2 is 54000. The sequence (DISABLE, ENABLE, then SETUP_VSEMPH with lane setting 0, 1, 2) is the same
  at every rate; only dword2 changes. The training pattern and AUX writes between these commands are not in the trace.
  The meaning of dword3 (`f4130003` / `f4000003` / `f6130003`) is not verified.
- **240 Hz at 10 bpc and HDR:** `240-10` and `hdr-240` train HBR3 x4 with DSC on (slice width 640, 256 = 16 bpp, `odm_combine_segments` 2),
  same as the 8 bpc 240 step; `max_bpc` 10, DP_PIXEL_FORMAT `0x42000000`; Hyprland shows `XRGB2101010` and `cm hdr`.
  Linux 240 Hz is never lit without DSC at any bpc in this capture.
- **`240-nodsc` is inconclusive:** it issued no TRANSMITTER_CONTROL commands (no new link training) and the state afterwards still
  shows DSC on, 10 bpc, HDR preset, i.e. the earlier `hdr-240` state with 239 fps. The forced `dsc_clock_en = 2` did not
  change the mode (inferred: it only applies at the next full link training, and Hyprland did not retrain for an identical mode).
  Whether Linux can light 240 Hz without DSC is therefore unanswered; 8 bpc without DSC at HBR3 stays the inference from the 240 Hz research.

## Linux capture v3 (2026-11-20): every mode, Adaptive Sync, HDR

`linux_display_capture.py --all-modes` (EFI copy of the tool) ran to the end on the same machine (Linux 7.2.15, Hyprland 0.56.2, LG on DP-3).
Output: EFI partition `/boot/vermilion/capture/display-20261120-*/` (newest; 42 steps, not in git). Every step set its mode; none failed.

- Modes Hyprland listed (12): 2560x1440 at 239.97 / 143.99 / 120.00 / 59.95; 1920x1080 at 120.00 / 119.88 / 60.00; 1280x1024@75.03;
  1024x768@60.00; 800x600@60.32; 640x480@60.00 and @59.94. No 165 Hz mode is offered.
- Steps: `hub` (2560x1440@144), `to-<mode>` / `from-<mode>` for each other mode (hub first, back to the hub after), then `to-vrr-<rate>`
  (vrr 1) and `to-hdr-<rate>` (10 bpc, cm hdr) with their `from-` steps for the four 1440p rates.
- DMUB commands per step (kprobe `dmub_srv_fb_cmd_queue`) are > 0 in every step. Every `to-` step at 1440p 144/120/240 Hz and every
  `from-` step back to the hub carries the usual ~1000-2000; the low-rate modes (60 Hz and the 1080p/legacy modes) ~470-600.
  The decode of the TRANSMITTER_CONTROL sequence per step was not done this time.
- Not checked: whether `vrr 1` actually enabled Adaptive Sync on the panel (only the commands were captured).
