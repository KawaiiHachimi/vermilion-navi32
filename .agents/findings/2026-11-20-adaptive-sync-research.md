# 2026-11-20: Adaptive Sync, what macOS needs and what Linux sends (read-only research)

Machine: RX 7900 XTX, LG ULTRAGEAR+ (EDID range 48-240 Hz, AMD FreeSync VSDB). Nothing was run, loaded or booted.

## macOS supports Adaptive Sync on Navi Intel Macs

Apple lists the 2019 Mac Pro with Radeon Pro W5500X / W5700X / W6800X / W6900X as Adaptive Sync capable
(support.apple.com/102144). Its driver, `AMDRadeonX6000Framebuffer` (a fileset entry in
`/System/Library/KernelCollections/SystemKernelExtensions.kc`, symbols kept), was carved out and disassembled on the Mac
(scratchpad tool `kccarve.py`, not in git). No Apple code is copied; only what it reports and calls is noted.

| Piece | Apple's AMD driver | Notes |
|---|---|---|
| Capability to IOGraphics | `reportCapabilities` publishes `IOFBTimingRange` (`IODisplayTimingRangeV2`, a stack struct at rbp-0x150: min / max pixel clock at -0x130 / -0x128 confirm the base); `supportedSyncFlags` (offset 52, rbp-0x11c) gets `0x10` when a feature bit (framebuffer flags word bit 7) is set **and** the link reports VRR support | `0x10` has no name in the SDK's `IOGraphicsTypes.h` (`supportedSyncFlags` documents 0x1-0x8). Per the header, an `IOFBTimingRange` property also lets the OS program detailed timings within the range |
| The range per mode | `prepareVRR(enable, timing)` at every `setDisplayMode`: min V_TOTAL = vActive + vBlank, max = min + `verticalBlankingExtension`; VRR on only if enabled, extension > 0 and max > min | `IODetailedTimingInformationV2::verticalBlankingExtension`, SDK comment: "lines (AdaptiveSync: 0 for non-AdaptiveSync support)" |
| Link | `setLinkAdaptiveSyncSupport`, `AmdLink::isVRRSupported` | the DP sink side (MSA ignore, DPCD) |
| SDP | `composeFalconAdaptiveSyncGenericSDPPacket`, `...CustomInfoFrameSDPPacket` | the Adaptive Sync / FreeSync info packet |
| AGDC | **command `0x979`** (`vendor_doDeviceAttribute` jump table at 0xbf74304, entry 0x979 - 0x975) -> `getVrrCapability(AGDCVrrCapability_t *, 0x44)`: checks the size is 0x44, writes only the first dword (a controller capability query, id 8) and returns success | PhxAGDC answers 0x921 / 0x711 already. On this machine macOS sent 0x711, 0x925, 0x2001, 0x941 and 0x3 in two hours, never 0x979 |
| `'vrr?'` (`kIOFBVariableRefreshRate`) | not handled in the AMD driver | in IOFramebuffer or CoreDisplay (not read yet) |

Upstream's rotation work (rot5) found empirically that CoreDisplay leaves modes with `verticalBlankingExtension > 0`
out of its default-mode choice, so CoreDisplay reads the extension from the driver's timing info: the driver offers the
variable-refresh timing (inferred).

## Linux capture v3 (display-20261120-130351, Adaptive Sync on in the monitor)

- The `to-vrr-*` mode set writes, beyond the plain mode set: the Adaptive Sync SDP through VPG1 (`VPG_GENERIC_PACKET_DATA`
  starting `0x13050700`, `VPG_GSP_IMMEDIATE_UPDATE_CTRL 1`) and `DP1_DP_SEC_CNTL 0x501111` (secondary packets on).
- No `OTG0_OTG_V_TOTAL_MIN/MAX/CONTROL` (0x4ff0 / 0x4ff1 / 0x4ff3) write in the traced window, and `regs.txt` does not
  read them. Linux sets the range at the first flip after the mode set (amdgpu_dm VRR transition, inferred), outside the
  window. DPCD 0x107 = 0x90 in both plain and VRR steps (MSA_TIMING_PAR_IGNORE already set for the whole run).
- **Missing for the hardware side:** the OTG V_TOTAL_MIN / MAX / CONTROL values with VRR active, and the writes that set
  them. A short Linux capture (VRR on, read 0x4ff0-0x4ff4, trace a few seconds past the mode set) gives them.

## Plan

1. PhxAccel probe (no hardware change): report the timing-range bit and a variable timing for head 0 (the 240 mode with
   the extension to 48 Hz), log what CoreDisplay calls (`validateDetailedTiming`, `setDetailedTimings`, `'vrr?'`,
   `setDisplayMode` on that mode). Picking it sets the fixed mode. Shows whether System Settings offers "Variable".
2. Linux capture of the OTG VRR registers (above).
3. VermilionBoot: the range and the SDP as Linux writes them, when the variable mode is set.

## Probe boot 1 (2026-11-20 22:00, the XTX machine)

- The probe ran: `vrrprobe: IOFBTimingRange published: 48-240 Hz, 390-390 kHz, pixel clock to 1060 MHz, sync flags 0x11`,
  and `IOFBTimingRange` is in the registry.
- **macOS did nothing with it:** 0 `validateDetailedTiming` / `setDetailedTimings` / `'vrr?'` calls, no AGDC 0x979.
  System Settings lists the same four 1440p rates (owner).
- The 390-390 kHz line rate is what the LG's descriptor says (byte 4 = 0x0c: both horizontal rates + 255); the 1440p60
  line rate is ~89 kHz. Whether CoreDisplay reads the line rates is unknown.
- The parser read the EDID 1.4 offset bits in the wrong order (bit 0 / 1 instead of the 10 / 11 codes); same result for
  the LG, fixed with tests.
- The AGDC commands macOS sends here, mapped through Apple's jump tables (tool `~/vermilion-captures/tools/agdcmap.py`,
  outside git): **0x925 `getPortStatus`**, **0x941 `getPortCapabilities`** (48 bytes, `getCapabilitiesOfPort`: two flag
  words at +8 / +0xc built from link and controller feature queries), 0x921 `getLinkConfig`, 0x980 `getGpuCapability`.
  0x3, 0x711 and 0x2001 are in a compare chain (not mapped). PhxAGDC refuses 0x925 and 0x941. Inferred: macOS reads
  the port's VRR support from 0x941 (or 0x925) before it asks 0x979; which flag bit means VRR is not decoded yet.

## Where the range comes from on Apple's path (read 2026-11-20 22:30)

- `AmdLink::isVRRSupported` is AmdLink vtable slot 0x210; `reportCapabilities` calls it before setting the timing range's
  sync flag 0x10. `getCapabilitiesOfPort` (AGDC 0x941) never calls it: the port capability answer carries no VRR bit
  (its link queries are `AmdLink::getFeature` ids 0, 1, 4, 5, 6, 0xf and `getConnectorId`).
- The per-mode extension is written in `AgdcServices::notifyModeValidation` / `notifyModeSet`: the timing goes into an
  AGDC entry, `this->vfunc(0x8b0)(0xa, entry)` hands it to the AGDC client, and on a status 0..3 the entry's fields,
  `verticalBlankingExtension` included, are copied back into the timing. So on the Mac Pro the range per mode is set by
  the AGDC client during mode validation (inferred), and the timing range only says "capable".
- Probe step 2 (PhxAccel, same flag): head 0's 1440p modes report the extension to the EDID minimum themselves
  (`phxhead::vrrExtension`: pixel clock / (H total x 48 Hz) - V total; 1440p60: 368 lines). Host: head0_1440 47/47.

## Probe step 2 on the box (2026-11-20 22:12): System Settings offers "Variable (48-240 Hz)"

- With the timing range (sync flag 0x10) **and** an extension on each 1440p mode (3: 368, 4: 2287, 5: 3031, 6: 6474
  lines to 48 Hz), System Settings shows **Variable (48-240 Hz)** and nothing else for 2560x1440, and picked it itself
  (mode 6). The hardware stays fixed: OTG0 measured 240.000 Hz. Still 0 `validateDetailedTiming` / `'vrr?'` calls and no
  AGDC 0x979: macOS needs only the range flag and the per-mode extension.
- macOS folds every 1440p mode that has an extension into the one "Variable" entry, so the fixed rates are gone from
  the menu. Apple's Macs list "Variable" next to fixed rates: the fixed modes need no extension, and the variable entry
  its own mode (the 240 timing with the extension; inferred).

## The hardware side is in capture v3 after all: a DMUB command, not registers

DCN 3.2 on this card sets the V_TOTAL range through the DMUB firmware: command type 0x4c (`FW_ASSISTED_MCLK_SWITCH`),
subtype 0x01 (`FAMS_DRR_UPDATE`), header `0x0c00014c`, payload c1 = V_TOTAL max, c2 = V_TOTAL min (register values,
lines - 1), c3.. = 0. In the traces (`trace-modeset.txt`, `dmubcmd1` lines):

| Step | DRR_UPDATE (max, min) |
|---|---|
| plain 144 | 0x5eb, 0x5eb only (fixed, 578 times; one per frame with `0x0c00034c` = subtype 3 `SET_MANUAL_TRIGGER`) |
| to-vrr-143.99 | 0x5eb, 0x5eb, once 0xc53 / 0xc53 (VRR inactive in the traced window) |
| from-vrr-143.99 (VRR active, back at the hub) | **0x11c3 / 0x5eb, 85 times** (4548 lines = 48 Hz at the 144 timing; the probe's own extension 1516 + 3031 = 4547 + 1), and single equal pairs while ramping |
| plain / vrr 240 | 0x652 / 0x652 (fixed); 240's mode sets also send `FAMS_SETUP` (0x3400004c, c1 = 1, c2 = 0xa13f4c, c3 = 0x2000101, c4 = 0x100) |

So on the hardware, VRR on = `FAMS_DRR_UPDATE` with max = the 48 Hz V_TOTAL - 1 and min = the mode's V_TOTAL - 1;
VRR off = max = min. Open: the replay plans drop every DMUB command but TRANSMITTER_CONTROL (`p0odm_from_trace.py`), so
whether DMUB's FAMS is set up on macOS, and whether a DRR_UPDATE without Linux's `FAMS_SETUP` works, is not known.
The Adaptive Sync SDP (VPG1 generic packet `0x13050700..`, `DP_SEC_CNTL 0x501111`) and DPCD 0x107 = 0x90 (MSA timing
ignore, set in every step of the run) are the other pieces.

## Which DRR path Linux takes on DCN 3.2 (Linux master, read 2026-11-20 23:00)

`optc32_set_drr` (dcn32_optc.c) -> `optc3_set_vtotal_min_max` (dcn30_optc.c):
`if (dc->caps.dmub_caps.mclk_sw && !dc->debug.disable_fams) dc_dmub_srv_drr_update_cmd(...) else optc1_set_vtotal_min_max(...)`,
then `optc32_setup_manual_trigger` with the same condition (DMUB `SET_MANUAL_TRIGGER`, or `OTG_V_TOTAL_CONTROL`:
MIN_SEL 1, MAX_SEL 1, FORCE_LOCK_ON_EVENT 0, SET_V_TOTAL_MIN_MASK = TRIGA (1 << 1)). The register path writes
OTG_V_TOTAL_MAX, then OTG_V_TOTAL_MIN (both vertical_total - 1), then that OTG_V_TOTAL_CONTROL update. `disable_fams` has
no module parameter or dcdebugmask bit on DCN 3.2 (`DC_DISABLE_SUBVP_FAMS` = 0x20000 sets only force_disable_subvp and
fams2). `mclk_sw` comes from the firmware's QUERY_FEATURE_CAPS answer. On macOS the firmware is the GOP's DMCUB build,
and PhxDmub.h forbids FAMS (76) commands to it. VermilionBoot b141 reads that answer (`debug.phxdmub_caps`): no
fw-assisted mclk switching = Linux on this firmware would use the register path.
