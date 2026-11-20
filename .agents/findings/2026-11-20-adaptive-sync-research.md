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
