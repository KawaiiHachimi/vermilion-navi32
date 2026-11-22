# Display polish on the XTX machine: the install and test log (2026-11-22)

Moved here from `.agents/TODO.md` on 2026-11-22, when its "Display polish" item was cut down to the state and the open
items. This is the log as it was written during the work: what was installed when, the test asked of the owner, what the
owner saw, and each step's rollback. Sums are the first 8 hex digits of the sha256 of the kext's executable. Nothing
here is a plan any more; the state is in [TODO.md](../TODO.md) and [docs/XTX.md](../../docs/XTX.md), the reasons in
[decisions.md](../decisions.md), the build notes in `src/vermilion-boot/CHANGELOG.md` (146-150).

Related notes: [Adaptive Sync research](2026-11-20-adaptive-sync-research.md), [HDR](2026-11-21-hdr.md),
[the VRR underflow and the AuxKC](2026-11-22-vrr-underflow-and-auxkc.md).

## What macOS does with the floor's EDID copy

PhxAccel serves macOS an EDID whose minimum rate is the floor (`phxhead::edidFloorPatch`, so that System Settings says
"Variable (60-240 Hz)"). macOS identifies a display by its EDID: each time the floor changed (48 / 90 / 60 on
2026-11-22) it made a new display, with a new UUID and a fresh colour profile, and the owner's profiles and
per-display settings looked gone. Seen on the XTX machine. With the floor left alone nothing resets.

## The log

The item numbers are the order the owner asked for (0 and 00 came up during the work).


  1. **10-bit drawing in HDR.** **b149 + PhxAccel + plugin installed 2026-11-22 18:00** (EFI `OC` = b149 sha d34093c0,
     `OC.prev` = b148 sha 0a680199; `~/vermilion-rollback/`: VermilionBoot.kext.b148, VermilionAccel.kext.pre-hdr10,
     NVMTLDriver.b139). Done as: the HDR plans keep Linux's pipe-0 format writes (generator `--pixel-format`: CNVC0 0x418F /
     HUBP0 0x3AA5 = 0xa in, 0x8 out; pipe 1 follows by its live copies); GPU present accepts the 10-bit scan-out and flags it
     (FMT10), the plugin then copies l10r raw (BGRA widened on the CPU), the CPU copy likewise (`debug.phxfb0_scan10`).
     MPCC0 blends with global alpha (0xffff0461) so l10r's top bits do not matter. The depth macOS picks stays (WindowServer
     draws l10r anyway). (unverified) **Test:** restart; SDR as before; HDR on: colours right, a dark gradient without
     banding; `sysctl debug.phxfb0_scan10` = 1 in HDR, 0 in SDR; plugin log "scan-out ARGB2101010". **Rollback:** wrong
     colours in HDR: HDR off (the from plan sets 0x8); boot trouble: swap `EFI/OC` / `EFI/OC.prev`; plugin trouble:
     `sudo cp ~/vermilion-rollback/NVMTLDriver.b139 /Library/GPUBundles/NVMTLDriver.bundle/Contents/MacOS/NVMTLDriver`.
     Was: Today macOS picks depth 0 (32-bit) for the HDR twin and the plans keep pipe 0's 8-bit
     surface. Plan: (a) PhxAccel offers the HDR twins with the 30-bit depth preferred / only (getInformationForDisplayMode
     maxDepthIndex, getPixelInformation), check macOS then sets depth 1 on them; (b) on an HDR mode landing, HUBP0
     DCSURF_SURFACE_CONFIG 0x3AA5 and CNVC_CFG0_CNVC_SURFACE_PIXEL_FORMAT 0x418F = 0xa (Linux's to-hdr values; 0x8 back
     for SDR), with pipe 1's copies under ODM (the plans COPY pipe 1 from pipe 0 live); (c) GPU present: the kernel's
     scanAllocSlot accepts ARGB2101010 (PHXN_SCAN_FMT_*), the Metal plugin copies l10r surfaces raw instead of through
     `scan_l10r.comp` while HDR; (d) the CPU copy (nvrm-accel.cpp, the l10r branch) copies raw while HDR. Test: a gradient
     in HDR without banding.
  - **Display identity follows the EDID copy (2026-11-22):** PhxAccel's floor patch (getDDCBlock) changes macOS's EDID, and
    macOS made a new display (new UUID, profile `LG ULTRAGEAR+-<UUID>.icc`, 01:32 / 02:23 / 17:26) each time the floor
    changed (48 / 90 / 60): the owner's display profiles and settings looked gone. The floor is stable at 60 now. Owner's
    profiles (LG 27GS95QE-B SDR / HDR, "corrected for macOS from LG/Microsoft-generated data") re-installed in
    /Library/ColorSync/Profiles/Displays/; a copy in ~/Documents/ColorSync originals 2026-11-22/corrected/. Consider: keep
    the label patch only when it does not change identity, or document that a floor change resets the display's settings.
  2. **Variable with HDR on.** **PhxAccel with id 18 installed 2026-11-22 18:40** (kStdHdrVrrTwin: id 17's timing with the
     HDR10 fields and the VRR extension, key 1440240, "phxVrrSet" 1; offered with both features; rollback
     `~/vermilion-rollback/VermilionAccel.kext.pre-hdrvrr`). Needs Allow in Privacy & Security before the restart (AuxKC).
     b150 on the box (owner 18:30): HDR works (LG in HDR, `debug.phxfb0_scan10` 1); Variable was off in HDR (no twin).
     (unverified) **Test:** HDR on, then the refresh menu offers Variable; pick it: the LG's overlay moves, `debug.phxvrr_stat`
     on=1. Was: Today picking HDR sends phxVrrSet(0). Plan: a VRR+HDR twin of 240 (HDR10 fields and the
     verticalBlankingExtension, new std kind), offered with both features; PhxAccel calls phxVrrSet(1) for it; the kernel
     side needs nothing new (vrrOnLocked works on key 1440240: h 1440, same V_TOTAL). Check CoreDisplay then shows
     Variable in HDR.
  3. **Dropped (owner, 2026-11-22):** 2560x1440 works fully (every rate to 240, Variable 60-240 with the ramp, 10-bit HDR,
     Variable + HDR); the owner uses only that. The other resolutions stay as they are (fixed rates, no VRR / HDR); not
     disabled. Was: **VRR and HDR at the other resolutions.** VRR: the CPU copy needs the per-flip trigger (a platform function to
     PhxBringup after each copied frame, OTG_TRIGA_MANUAL_TRIG, while gVrrOn), vrrOnLocked must accept the other modes
     (drop the h == 1440 gate; vrrRegs per mode), and per-size VRR twins in phxhead.h (48 Hz to the mode's rate; only
     modes above 48 Hz). HDR: Linux's HDR mode sets at those modes are not captured: capture v5 in Omarchy
     (`--hdr-pairs` for 1080p60 / 1080p120 / ...), then plans + twins as b145.
  0. **VRR minimum (owner, 2026-11-22):** the LG flickers in VRR near the low end (also on Windows and Linux: the monitor's
     VRR, brightness swings between long and short frames). Fix: a configurable floor, `~/gpu/phxboot-vrrmin` (Hz, default
     the EDID's 48), read by the daemon into a PhxAccel sysctl before the claim; PhxAccel uses it for the twin's
     verticalBlankingExtension and the timing range's minFrameRate (menu "Variable (N-240 Hz)") and passes it to
     "phxVrrSet" (io = 1 | min << 8, VermilionBoot b146: vrrRegs with that min instead of the fixed gVrrMinHz 48).
     **b146 + PhxAccel installed 2026-11-22 02:10** (EFI `OC` = b146 sha 132f6c02, `OC.prev` = b145 sha 281f119c; b144 / b145
     kexts, PhxAccel `.hdr2` and the daemon `.pre-vrrmin` in `~/vermilion-rollback/`). Default floor 60 Hz (PhxAccel
     `debug.phxfb0_vrrmin`; the menu uses it only with PhxBringup b146+, else the EDID's 48), daemon `~/gpu/phxboot-vrrmin`
     (0 = the EDID's, 24-240). Host tests: p0odm 684978, head0_1440 66; mocks 225 + 159. (unverified on the box)
     **Test:** restart; System Settings shows "Variable (60-240 Hz)"; pick it, the LG's FPS overlay stays at 60 or above,
     `sysctl debug.phxvrr_stat` names floor 60 Hz and MAX 0x194b (6475) at 240. **Rollback:** boot trouble: swap `EFI/OC`
     and `EFI/OC.prev` (from macOS or Linux); only the menu wrong: `echo 0 > ~/gpu/phxboot-vrrmin` and restart.
     **2026-11-22 on the box:** the floor works (OTG0 MAX 0x194b, the LG's overlay at 60, `debug.phxvrr_stat` floor 60), the
     flicker is less but still there (owner), and the menu still said "Variable (48-240 Hz)" although IOFBTimingRange said 60:
     CoreDisplay takes the label from the EDID (the LG has 48 in its 0xFD descriptor and in AMD's FreeSync VSDB). PhxAccel
     now serves macOS an EDID copy with the floor as both minima (phxhead::edidFloorPatch; the monitor's EDID kept inside).
     Floor set to 90 for the next test (`~/gpu/phxboot-vrrmin`). **On the box:** menu "Variable (90-240 Hz)", still flickers
     (owner). Web (Blur Busters, RTINGS on the 27GR95QE): the LG OLEDs flicker with frame-time swings; no firmware fix; the
     other OSes hide it by running the desktop at a fixed rate. Owner's preference: **60-240 with a ramp** (floor back to 60).
     **b147 installed 2026-11-22** (EFI `OC` = b147 sha 6213fecd, `OC.prev` = b146 sha 132f6c02; b144-b146 in
     `~/vermilion-rollback/`): the ramp (PhxVrrRamp.h, `debug.phxvrr_ramp` % per flip, default 10, 0 = off) and a live
     floor (`debug.phxvrr_floor`). Host test vrr_ramp 13/13. (unverified)
     **Test:** restart, Variable (60-240 Hz); flicker? Tune live as root: `sysctl -w debug.phxvrr_ramp=5` (gentler) / `20` /
     `0` (off); `sysctl debug.phxvrr_stat` shows the window and the writes. **Rollback:** black screen or flicker worse:
     `sudo sysctl -w debug.phxvrr_ramp=0`; boot trouble: swap `EFI/OC` and `EFI/OC.prev`.
     **On the box (b147, owner):** Variable 60-240 works with the ramp; flicker less. Ramp 5: typing 60-100 Hz, still 60,
     mouse 240, "way less flickering", animations very smooth; 10 and 20 alike (more flicker); 0 stuck at 137 Hz (bug: the
     last window stayed). Some flicker remains (the panel; maybe no way around it). The boot before, one replay run toward
     HDR 240 failed its post check after the 240 -> 144 leg (details cleared by the refusals after it).
     **b148 installed** (EFI `OC` = b148 sha 0a680199, `OC.prev` = b147 sha 6213fecd; b147 in `~/vermilion-rollback/`):
     ramp default 5, ramp 0 restores the full range, `debug.phxp0odm_stat` keeps a failed run's post check
     (`last_fail_post`). **Test:** restart; Variable feels as with ramp 5; `sudo sysctl -w debug.phxvrr_ramp=0` gives the full
     range (no stuck rate). **Rollback:** swap `EFI/OC` and `EFI/OC.prev`.
  00. **2026-11-22 the failed Variable -> HDR switch, the white flashes, PhxAccel not loaded**
     ([findings](findings/2026-11-22-vrr-underflow-and-auxkc.md)). The run failed on HUBP0's sticky underflow status alone
     (twice: b147, b149); on the box the range / ramp writes took effect mid-frame (OTG0 DRR double buffer mode 0; Linux
     runs 2) and raised that sticky at 144 Hz by hand (4 in 27 000 window changes; 0 in 19 600 with mode 2). PhxAccel:
     the AuxKC is only rebuilt by the Allow click in Privacy & Security, the 17:57 restart skipped it.
     **b150 installed 2026-11-22 18:25** (EFI `OC` = b150 sha 3806555e, `OC.prev` = b149 sha d34093c0;
     `~/vermilion-rollback/`: VermilionBoot.kext.b149, AuxiliaryKernelExtensions.kc.0222). The AuxKC was rebuilt 18:22 with
     b149's PhxAccel (UUID F954FE49; the 10-bit CPU copy, `debug.phxfb0_scan10`); the plugin is b149's, installed 18:00.
     Host tests: p0odm 686405, the other tests/kext suites, mocks 225 + 159. (unverified at 240 / in HDR)
     **Test:** restart. (1) `kmutil showloaded | grep vermilion.accel` shows F954FE49. (2) Variable for a while: no white
     flash; `sysctl debug.phxvrr_stat` shows `dbuf now 0x02000000` and HUBP0 with bits 30:28 = 0 (0x0...). (3) HDR on from
     Variable: the LG's OSD shows HDR; `sysctl debug.phxfb0_scan10` = 1; `sysctl debug.phxp0odm_stat` result=done
     (`stale_cleared` / `post_retries` say whether a sticky was met). (4) HDR off: SDR again, scan10 = 0.
     **Rollback:** Variable worse: pick a fixed rate (the range and the double buffer go off with it). Boot trouble: mount
     the ESP and swap `EFI/OC` and `EFI/OC.prev` (from macOS: `sudo diskutil mount disk0s1`, then in
     `/Volumes/NO NAME/EFI`: `mv OC OC.bad && mv OC.prev OC`; or from Linux). PhxAccel trouble:
     `sudo cp -R ~/vermilion-rollback/VermilionAccel.kext.pre-hdr10 /Library/Extensions/VermilionAccel.kext`, chown
     root:wheel, `sudo kmutil load -p ...`, **Allow** in Privacy & Security, restart.

      **Next:** install PhxAccel, boot once without the flag (b140 test), then `touch ~/gpu/phxboot-vrrprobe` and restart:
      does System Settings offer a variable refresh rate, and what does the log show. **Missing for the hardware:** a Linux
      capture of OTG0 V_TOTAL_MIN / MAX / CONTROL (0x4ff0-0x4ff4) with VRR active (capture v3 did not read them)
