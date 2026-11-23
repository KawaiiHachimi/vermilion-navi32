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

## 7. On the box: b153 (Linux's pause before the second window), 03:51-03:57

Three restarts with 1440p60 HDR saved; every login switch landed, daemon fine.

| Boot | HDR to plan, window 2 | since previous window | link off -> up |
|---|---|---|---|
| 03:51 | **2 attempts**: the first got no AUX answer (`rc=aux`, status all 00, CR loop never reached), the second trained 100 ms later | 227.9 ms | 20.4 ms |
| 03:53 | 1 attempt | 227.6 ms | 3.9 ms |
| 03:55 | 1 attempt | 226.8 ms | 4.8 ms |

With Linux's pause the clock-recovery failures of b151 / b152 did not come back (0 of 3; they were 2 of 3 recorded
boots before), but the sink once did not answer AUX at the first access after its D3 (a different failure). Three boots
are too few to call either rate. The retry (b151) is what keeps the screen on; the pause stays because it is Linux's
timing. Again a run at the shutdown: macOS asked for SDR 60 Hz as the session ended (03:53:59, second sighting).

## 8. 04:15 (b153 + the size fix): a black picture on a locked link, and b154

The owner restarted with the new PhxAccel (physical size) and had a black screen; they logged in blind and reached T3
from the phone. What the machine showed during the black screen (04:17-04:25):

- The daemon: `engine run 2 ... result=DONE target=1440060`; the HDR to plan's second window `tries=2 rc=aux,ok`
  (the first attempt got no AUX answer, as at 03:51), 227 ms after window 1; then `SUCCESS`.
- The link: `debug.phxdpcd=0x12` (AUX 2 = DP B, reads only): link rate 0x0a x4, lane status `77 77`, interlane align
  `01`, sink status `01` (receive port 0 in sync).
- Pipe 0: OTG0 on, 60 Hz; HUBP0 scanning `0x80_1000_0000` (GPU present's slot 0), no underflow; DPG0 off (no test
  pattern); post check passed.
- WindowServer: GPU present ON (ARGB2101010), 3000 presents, 0 dropped; `screencapture` gave the normal desktop.
- The size fix: loaded (AuxKC rebuilt 04:14), `CGDisplayScreenSize` 591.1 x 332.5 mm, not built-in, brightness 1.0.

A session-only switch (`CGCompleteDisplayConfiguration(..., kCGConfigureForSession)`) to 1440p144 HDR brought the
picture back (owner) and the switch back to 1440p60 HDR kept it. The 144 switch's first window needed 3 attempts
(clock recovery, then AUX, then trained); the to plan's windows and every window of the switch back trained at once.

Reading: the sink can stay black after a retried link window although DPCD says the link is locked and in sync
(seen once, LG 27GS95QE-B, after `rc=aux,ok` in the last window; the same retry at 03:51 gave a picture). A fresh mode set
recovers it. The size fix is not the cause (the picture works with it at 144 and at 60 Hz). Nothing on the GPU side
tells the black state apart, which is why the daemon said `SUCCESS`.

b154: after a switch whose last window trained only after a retry, the engine runs the switch once more from the
target (its from plan to the hub, its to plan), once per run, at most 4 per boot; at the hub (no plan pair) it is
noted (`redone=-1`), not run. Same Linux sequences as any switch away and back; nothing new is written.

Also found: `EFI/OC` was already change 11 and change 12 sat in `EFI/OC.bad` (the Linux rollback of the NVRAM test had
been carried out; when is not known yet), so the NVRAM test result of 04:06-04:15 is unsure. A FAT repair had turned one
licence text in each tree into `FSCK0000.000` (same bytes).

**b154 on the box, 04:36:** the 1440p60 HDR login switch's last window again needed a second attempt (`rc=aux,ok`); the
redo ran (`redone=1 redos=1/4`): `60hdr-from` (its first window `rc=aux,ok` as well), then `60hdr-to`, both windows
first time. Picture on (owner). 7 link windows, `train_retried=2`.

## 9. Input lag and tearing at 60 Hz, measured (b154, 04:45-04:55)

Tool (session scratchpad, not in the repo): posts a mouse-moved event (`CGEventPost(kCGHIDEventTap)`, the software
cursor makes WindowServer composite), then polls through the `N48N` user client (ReadRegs, reads only) HUBP0's
primary surface address (0x3ACA: changes when GPU present programs a flip) and OTG0's frame count (0x500C). 100 events
each, 30-50 ms apart. Times in ms, p10 / median / p90 / max:

| 1440p HDR | event -> flip programmed | flip -> next frame start | event -> frame start |
|---|---|---|---|
| 60 Hz (run 1) | 7.9 / 16.2 / 22.0 / 42.3 | 10.1 / 13.8 / 14.1 / 15.5 | 21.5 / 29.8 / 34.7 / 52.6 |
| 60 Hz (run 2) | 6.8 / 13.6 / 21.2 / 39.5 | 10.4 / 13.9 / 14.4 / 15.3 | 19.5 / 27.1 / 34.3 / 53.8 |
| 60 Hz (run 3, after 240) | 6.3 / 13.0 / 20.9 / 25.8 | 10.6 / 14.1 / 14.7 / 16.0 | 20.6 / 27.1 / 34.9 / 36.4 |
| 240 Hz | 4.9 / 7.7 / 10.0 / 13.9 | 0.3 / 0.6 / 1.8 / 4.1 | 5.9 / 8.3 / 11.7 / 14.4 |

Reading: at both rates the flip lands about 3 ms after a frame start (WindowServer composites at the vsync it is told
about, GPU present's copy is 0.7 ms), and it shows at the next frame start. At 60 Hz that leaves the finished frame
waiting 14 ms for the vblank; at 240 Hz the 3 ms is almost a whole frame. So the 60 Hz lag is the standard
compose-then-scan pipeline at a 16.7 ms frame, about 3.5x the 240 Hz figure; no extra frame is added by the driver.
Not measured: the USB HID path (posted events skip it) and the scan from the frame start down to the cursor's line
(up to one frame). Not compared with a real Mac. A late-latch scheme (tell WindowServer a vsync phase so it composites
just before the vblank) could save up to ~11 ms at 60 Hz (idea only).

Tearing: HUBPREQ0 DCSURF_FLIP_CONTROL (0x3ADB) sampled 40000 times while the cursor moved: 0x04100000 or 0x04100100
(bit 8 = an update pending for the vblank), bit 1 (immediate) never set. A flip cannot tear then; what the owner saw
as tearing (2026-11-19) needs the phone video (OWNER-TESTS 01).

## 10. b155 on the box (04:52-05:00): the DPCD retry rule, Linux's double buffer, display sleep

- Login switch (1440p60 HDR): every window first time, `dpcd_aux: calls=141 ok=141 retried=0`: no AUX miss this boot,
  so whether drm's retry rule catches the LG's first-access miss is not shown yet.
- `debug.phxp0odm_dbuf=1` (OTG_DRR_TIMING_DBUF_UPDATE_MODE 2, as Linux): session switches 60 HDR -> 144 -> 240 (ODM 2:1 +
  DSC) -> 120 -> 60, all DONE, post checks first time, no underflow (`post_retries=0`), every window `tries=1`; the
  owner saw each switch land. 0x501B read 0x02000000 after each run. SDR and the non-1440p plans not run under it yet.
- Display sleep (`pmset displaysleepnow`): PhxAMDFB `CurrentPowerState` 0, but OTG0 enabled and counting frames, HUBP0
  scanning, DPCD lanes `77 77` aligned: the stream never stops. Owner: the LG stayed on showing black (no standby, no
  "no signal"). `caffeinate -u` woke it: power state 1, picture back, OTG0 still at 60 Hz, no engine run needed.

## 11. The DPMS capture (Linux, 05:05) and the boot after it

- Capture v5 (`--dpms`, Linux 7.2.15, Hyprland through `hyprctl eval`), every 1440p rate SDR and HDR. DPMS off: 549
  (60 Hz) to 2409 (240 Hz) register writes; while off `OTG0_CONTROL` = 0x00001200 (master enable off) and no frames:
  Linux stops the timing generator and the stream, which is what lets the LG go to standby. DPMS on: 3778-10310 writes
  and 502-2127 DMUB commands (a full stream enable with link training). Kept at
  `~/vermilion-captures/dpms/display-20261123-050500` (no serials; not for git).
- The first power-on after Linux did not reach macOS's userspace: OpenCore's log of that boot
  (`opencore-2026-11-23-040938.txt`, UTC) ends at ExitBootServices as a good boot does; no unified log, no daemon log,
  no panic report (NVRAM writes blocked). The next power-on booted normally (b155). Cause unknown; seen once.
- That boot's login switch: `dpcd_aux: retried=2 gave_up=2 nacks=74 max_tries=32`: twice the sink NACKed all 32 tries of
  drm's rule (the first access after D3), so the training attempt still failed and b154's redo ran (`redone=1`). drm's
  rule does not cover this sink's miss; b154 does.
- OpenCore names its logs by the firmware clock in UTC: the "no logs since 03:15" of earlier was the 04:15 CET boot.
