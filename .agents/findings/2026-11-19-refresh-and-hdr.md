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
