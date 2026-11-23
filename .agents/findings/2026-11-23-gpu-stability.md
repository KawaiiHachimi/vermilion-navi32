# 2026-11-23: GPU stability (thread 01): black screen recovery, physical size, NVRAM writes

Machine: RX 7900 XTX (rev `0xC8`), LG 27GS95QE-B on DP, macOS 26.7.1 25G241, EFI change 8 (VermilionBoot b150,
VermilionTrust 1.0.3), PhxAccel `c6341e87`, display 2560x1440 @ 240 Hz HDR. Everything below was read on this machine
on 2026-11-23 between 02:30 and 04:00, unless a line says otherwise. Nothing on the machine was changed for it.

## 1. The 2026-11-22 20:19 black screen: what is left of it

- `~/vermilion-captures/black-20261122-2025.txt` (captured by hand at 20:25) has only the **refused** run after the
  failure: `result=refused ... why=a run failed in this boot`. The failed run's own stat had been overwritten, and the
  capture kept only the first line of each multi-line sysctl. OTG0 was at 144 Hz (`debug.phxvrr_stat`: 36 frames in
  250 ms, V_TOTAL 0x5eb): pipe 0 sat on the hub timing while the screen was dark.
- The unified log no longer has that boot (`log show` 20:19-20:26: nothing). So which leg failed, and how (CR or EQ,
  which lane), is lost. The b150 code says: a failed fall-back or a failed from plan latches; a failed to-plan whose
  fall-back lands does not. So the fall-back (the 1440p60 HDR from plan, or the entry's 240 from plan) most likely
  failed too.
- b148 already kept a failed run's post check (`last_fail_post`), but not the trainer's report.

## 2. What Linux does when DP link training fails (Linux v6.12, read 2026-11-23)

- `drivers/gpu/drm/amd/display/dc/link/link_dpms.c`: `LINK_TRAINING_ATTEMPTS 4`; `enable_link_dp` calls
  `perform_link_training_with_retries(..., lt_attempts = 4, ..., do_fallback = false)` for a conventional DP link
  (fallback to lower link settings only for USB4 DPIA; conventional links fall back during detection).
- `link/protocols/link_dp_training.c` `perform_link_training_with_retries`: per attempt `dp_enable_link_phy`, panel
  mode, `dp_perform_link_training` (which sets FEC_READY each time); on failure, for a PHY endpoint, stop at the last
  attempt and keep the PHY on, else `dp_disable_link_phy` (`dpcd_write_rx_power_ctrl(false)` = D3, then
  `disable_link_output`), then `msleep(delay)` with the delay starting at 50 ms and growing by 50 before each wait:
  100, 150, 200 ms.
- `phxdpl::linkOff` (PhxDpLink.h) is that disable in the trace's order (D3, TRANSMITTER_CONTROL DISABLE, pattern /
  link / internal 0); `phxdpb::linkUp` is link setup, ENABLE, training. b151 repeats exactly these steps. The only
  repeated write that Linux skips on a retry: DPCD 0x120 = 1 and the FEC_READY shadow, rewritten with the value they
  already hold (Linux keeps `fec_state` ready across the retry).

## 3. Physical size: the EDID's size is everywhere except the per-mode fields

| Where | Width x height |
|---|---|
| EDID (`IODisplayEDID` on AppleDisplay): first detailed timing | 590 x 333 mm |
| EDID base block bytes 21-22 | 59 x 33 cm |
| `IODisplayCreateInfoDictionary` (IOKit) on PhxAMDFB and on IODisplayConnect | 590 x 330 mm |
| `CoreDisplay_DisplayCreateInfoDictionary` (CoreDisplay's own info dictionary) | 590 x 330 mm (it also carries `IODisplayEDIDOriginal`) |
| `CGDisplayScreenSize` | **903.1 x 508.0 mm** = 2560 x 1440 at 72 dpi |
| `IODisplayModeInformation.imageWidth / imageHeight` (PhxAMDFB `getInformationForDisplayMode`) | **0 x 0** (bzero) for every mode |

So the finding of 2026-11-23 (macOS integration inventory: "PhxAMDFB doesn't hand WindowServer the EDID size") is not
right as stated: the EDID and the size reach IOKit and CoreDisplay. The one place with no size is the mode
information, documented in IOGraphicsTypes.h as "physical width of active image if known, in millimeters, otherwise
zero". Inferred, not shown: CoreGraphics takes the screen size from the current mode and falls back to 72 dpi when it
is 0. The PhxAccel change of this thread fills both fields from the EDID (`phxhead::edidImageMm`); the test is the
install (TODO "Staged now" 3).

Probes (built in the session scratchpad, not in the repo): a Swift `CGDisplayScreenSize` print, a C call of
`IODisplayCreateInfoDictionary`, a C call of CoreDisplay's dictionary through `dlsym` (no EDID or serial printed).

## 4. NVRAM writes and the squat boot

- `phxfb-squat` arms one squat boot: PhxFBSquatter removes it at probe and asks for an immediate NVRAM commit
  (`IONVRAM-FORCESYNCNOW-PROPERTY`), so a hung boot comes back on IONDRV (phx_classes.cpp). With `DisableVariableWrite`
  none of that reaches the firmware, which is why the variable written once from Linux makes every boot a squat boot
  (and why b138's recovery looped).
- With writes allowed, the removal is real: the next boot is a squat boot only if the daemon re-armed it, which it does
  at SUCCESS only while `~/gpu/phxboot-auto` exists. **That file does not exist on the machine** (`ls ~/gpu`). Without
  it, the boot after the NVRAM test would come up on the GOP desktop without acceleration (recoverable: create the
  file and restart; the GOP boot with `phxboot-auto` re-arms). TODO "Staged now" 2 creates it first.
- `nvram phxfb-squat` reads "not found" in this boot: macOS's copy was removed at probe; the firmware still holds it.

## 5. Smaller observations

- **OpenCore logs again:** `<ESP>/opencore-2026-11-23-004410.txt` (00:44 UTC, this boot). PROJECT_KNOWLEDGE said no
  log had been written since 2026-11-22 23:20.
- `/Volumes/BIOS` (`disk2s1`, FAT) was mounted: an external disk, not touched.
- **VRR floor and display identity:** AppleDisplay's `IODisplayPrefsKey` ends in a hash (16 hex digits) next to vendor
  and product. The display-polish log (2026-11-22) saw a new display UUID and colour profile each time the floor
  changed (48 / 90 / 60), because PhxAccel's floor patch changes the EDID macOS reads. Options for the owner: keep the
  patch (the menu says "Variable (60-240 Hz)"; a floor change resets the display's settings, the floor is fixed at 60
  now), or drop it (the identity never changes; the menu says 48-240 although the floor stays 60). Decision open.

## 6. On the box: b151 / b152 at the 1440p60 HDR login switch (03:11-03:46)

The owner restarted 3 times on b151 and 2 times on b152 with 1440p60 HDR saved; every login switch landed (owner:
"pretty stable"). The switch from macOS's state is three link windows: the entry to 240 (earlier run), then `240-from`
(HBR2) and the HDR to plan's two (HBR each).

| Boot | Build | Last window of the HDR to plan | Time since the previous window ended |
|---|---|---|---|
| 03:28 | b151 | **4 attempts** (only the count was kept) | not measured |
| 03:42 | b152 | 1 attempt | 46.5 ms |
| 03:44 | b152 | **3 attempts**: clock recovery failed on every lane (`status 00 00 ...`, adjust 3/3: the sink asks for maximum swing and pre-emphasis), then an AUX error, then trained after 100 + 150 ms | 46.6 ms |

The first window of the same plan always trained at once, 8 ms after its link off. Linux's capture v4 of the same switch
(`to-hdr-59.95`): every window trains at its first ENABLE (`DIS EN ... DIS EN ...`), and 179.8 ms pass from window 1's
FEC-on write to window 2's DISABLE: Linux's commit sleeps there, the replay runs the writes in between in ~46 ms. Reading
(inferred, not shown): the LG is still busy with the first HDR mode set when the second training starts. b151 at 20:19
(2026-11-22) would have had one attempt: the black screen. b153 adds Linux's time there (CHANGELOG 153).

At 03:43:19 a shutdown during the daemon's watch caught a run in flight (`target=60 phase=leg1`, the `60hdr-from`
window trained): macOS asked for SDR 60 Hz as the session ended (seen once; whether every logout does is unknown).
