# TODO

Open, actionable items only, grouped by the thread of the series in [ROADMAP.md](../ROADMAP.md) that owns them.
One or two lines each, with a link to the evidence. The machine's current state is in
[docs/PROJECT_KNOWLEDGE.md](../docs/PROJECT_KNOWLEDGE.md), questions only the owner can answer are in
[OWNER-TESTS.md](OWNER-TESTS.md), and the history up to 2026-11-23 (daily logs, done items, the old Phase 0-6
plan) is in [findings/2026-11-19..23-bringup-log.md](findings/2026-11-19..23-bringup-log.md).

Rules:
- `[ ]` open, `[~]` in progress, `[-]` dropped (say why). A finished item is deleted here and recorded where it
  belongs: the state in PROJECT_KNOWLEDGE, the evidence in a finding, the reason in decisions.
- **Before every EFI change:** write its check and rollback steps under the item, here, first (ROADMAP rule 3). Remove
  them once the change is verified and PROJECT_KNOWLEDGE's known-good section says what is booted.
- Something that works on a real Mac and not here goes in as a bug under the thread that owns it (ROADMAP rule 9).

## Staged now

Thread 01's machine changes, one per boot, in the order made (2026-11-23). **State at the end of thread 01:** `EFI/OC`
= change 14 (VermilionBoot b155, stage `vermilion-trust`), `EFI/OC.prev` = change 13 (b154), `EFI/OC.bad` = change 12
(the NVRAM test, inconclusive); PhxAccel with the physical size; the daemon with the display check. No further EFI
change in 01. `<ESP>/vermilion/flags/` stays empty; no `run-once.sh`.

1. **EFI change 9: VermilionBoot b151** (stage `vermilion-trust`, only the kext differs from change 8) **+ the boot
   daemon with the display check** (`~/gpu/phxboot-navi31.sh`, not an EFI change; it only reads sysctls).
   **Installed 2026-11-23 03:08** (owner's OK): `EFI/OC` = change 9 (VermilionBoot sha256 `a23a2766`), `EFI/OC.prev` =
   change 8; change 7 kept as `~/vermilion-rollback/EFI-OC.change7-20261122`. **Booted 03:11:** b151 loaded (the `recovery:` line), the 240 HDR login switch DONE in 1 training attempt, daemon `SUCCESS`, no `DARK`, shutdown cause 5, VermilionTrust enforcing (`denied_unlisted=0`). Left: OWNER-TESTS "Login at 60 Hz HDR".
   - Before: `~/vermilion-rollback/EFI-OC.change8-20261123` (a copy of the live `EFI/OC`) and
     `~/vermilion-rollback/phxboot-navi31.sh.pre-thread01`; `efi/build.py install` turns change 8 into `EFI/OC.prev`.
   - Check: `sysctl debug.phxp0odm_stat` has the `recovery:` line; `/var/log/phxboot.log` ends in `SUCCESS` with no
     `DARK`; then OWNER-TESTS 01 "Login at 60 Hz HDR" (3 restarts).
   - Rollback: boot trouble: `EFI/OC` and `EFI/OC.prev` swapped (PROJECT_KNOWLEDGE "Rollback of the EFI"); a daemon
     problem: `sudo cp ~/vermilion-rollback/phxboot-navi31.sh.pre-thread01 ~/gpu/phxboot-navi31.sh`, restart.
1b. **EFI change 10: VermilionBoot b152** (diagnostics only: every link window's attempts and timing, `train_wN` in
   `debug.phxp0odm_stat`) **+ the daemon's per-boot engine record** (`engine run N` lines in `/var/log/phxboot.log`).
   Why: the 03:28 boot's last link window trained at its 4th attempt. Owner's OK 2026-11-23 ~03:45. **Installed:** `EFI/OC` = change 10 (VermilionBoot `9ca53007`), `EFI/OC.prev` = change 9 (b151 `a23a2766`); daemon deployed.
   - Before: `~/vermilion-rollback/VermilionBoot.kext.b151`; `EFI/OC.prev` becomes change 9.
   - Check: `train_w` lines in the stat, `engine run` lines in the daemon log; 2-3 restarts at 1440p60 HDR.
   - Rollback: `EFI/OC` and `EFI/OC.prev` swapped; the daemon: `~/gpu/phxboot-navi31.sh` from git `47c4543`
     (`git show 47c4543:tools/phxctl/phxboot-navi31.sh`).
1c. **EFI change 11: VermilionBoot b153** (the eight HDR plans wait Linux's 100-180 ms before their second link
   window; [finding](findings/2026-11-23-gpu-stability.md) section 6). Owner's OK 2026-11-23 ~03:55. **Installed:** `EFI/OC` = change 11 (`bd3f72fa`), `EFI/OC.prev` = change 10 (b152 `9ca53007`). **Booted 03:51-03:57:** 3 restarts landed; window 2 `since_prev` ~227 ms; tries 2 / 1 / 1 (the 2: no AUX answer at the first attempt) ([finding](findings/2026-11-23-gpu-stability.md) section 7).
   - Before: `~/vermilion-rollback/VermilionBoot.kext.b152`; `EFI/OC.prev` becomes change 10.
   - Check: 2-3 restarts at 1440p60 HDR; every `train_w3` of the daemon log's `engine run` lines `tries=1`, with
     `since_prev_us` about 225000.
   - Rollback: `EFI/OC` and `EFI/OC.prev` swapped.
2. **EFI change 12: stage `vermilion-trust-nvram`** (`DisableVariableWrite` off; nothing else). Built on b153; owner's OK 2026-11-23 ~04:00; `~/gpu/phxboot-auto` created 04:00. For a hang: `<ESP>/vermilion/ROLLBACK-NVRAM-TEST.md` tells Claude on Linux the swap (owner's wish; delete it once the change is verified). First
   `touch ~/gpu/phxboot-auto`: with writes working, the squatter's delete of `phxfb-squat` at boot is real, and only
   the daemon's re-arm (`phxboot-auto`) keeps the next boot accelerated.
   - Check after the boot: `sudo nvram vermilion-test=1; nvram vermilion-test` (reads back), the daemon log's
     `phxfb-squat re-armed ... read-back ok`; one more restart reaches the accelerated desktop (squat boot); then
     `sudo nvram -d vermilion-test`.
   - Rollback: a hang at boot or at shutdown: hold power, power on, F8 > Limine (Linux), rename `/boot/EFI/OC` to
     `OC.bad` and `OC.prev` to `OC`, **power off fully**, power on. From macOS: the same renames under
     `/Volumes/NO NAME/EFI`. Then `rm ~/gpu/phxboot-auto` (back to the stuck-squat setup of today).
   - Worst case named by the old thread 02: a board that hangs on writes may need a CMOS clear (unverified).
   - **Result: inconclusive** (the Linux rollback ran at an unknown time; see 4). Recorded at the time:
     **Booted 04:06 and 04:10:** no hang at boot or shutdown, but the write did **not** persist: `vermilion-test` was
     gone after the restart, `phxfb-squat` stayed in the firmware (squat boot), and
     `nvram IONVRAM-FORCESYNCNOW-PROPERTY=...` returns "general error". Second test armed: `vermilion-test2=forced`,
     checked after the next restart (with staged 3). If it is gone too: back to change 11 (`DisableVariableWrite` buys
     nothing then) and find the real block from Linux (efivars).
3. **PhxAccel with the physical size** (**booted 04:15:** loaded, `CGDisplayScreenSize` 591.1 x 332.5 mm; the black
   screen of that boot was the LG after a training retry, not this: section 8 of the finding) (`/Library/Extensions`, AuxKC; needs the owner's **Allow** click in
   Privacy & Security before the restart).
   - Before: `~/vermilion-rollback/VermilionAccel.kext.pre-thread01` (`c6341e87`). **Copied and staged ~04:12**
     (`26685255`, `kmutil load -p`: "not approved"); the owner's Allow click rebuilt the AuxKC at 04:14, restart 04:15.
   - Check: `CGDisplayScreenSize` about 590 x 333 mm (thread 01's `sz` probe), System Settings > Displays.
   - Rollback: the copy back into `/Library/Extensions`, `kmutil load -p`, Allow, restart (INSTALL.md section 4).
4. **EFI change 13: VermilionBoot b154** (a switch whose last link window needed a retry runs once more from the
   target; [finding](findings/2026-11-23-gpu-stability.md) section 8) on stage `vermilion-trust` (= change 11's
   config, `DisableVariableWrite` true; only the kext differs from the live `EFI/OC`). Owner's OK 2026-11-23 ~04:30. **Installed** between the 04:33 build and the 04:36 boot: `EFI/OC` = change 13 (b154 `78f560cc`), `EFI/OC.prev` = change 11 (b153 `bd3f72fa`), config.plist byte-identical; `EFI/OC.bad` (change 12) left on the ESP. **Booted 04:36:** the login switch's last window retried (`rc=aux,ok`), the redo ran (`redone=1`; its own from plan's first window retried too, every later window first time), picture on (owner).
   - Found before the install: the live `EFI/OC` was already change 11 (b153, `DisableVariableWrite` true), change 12 sat
     in `EFI/OC.bad`, no `EFI/OC.prev`: the Linux rollback of 2 had been carried out, at a time not yet known (ask the
     owner). So the NVRAM result of 2 is unsure: the boots 04:06 / 04:10 / 04:15 may all have run change 11.
     In `EFI/OC` a FAT repair had turned `MtkWirelessBT.kext/Contents/Resources/google-libsbc-LICENSE.txt` into
     `FSCK0000.000` (the same text; the kext's binary unchanged).
   - Before: `~/vermilion-rollback/EFI-OC.change11-20261123` (a copy of the live `EFI/OC`) and
     `~/vermilion-rollback/EFI-OC.change12-nvram-20261123` (`EFI/OC.bad`); `efi/build.py install` makes change 11
     `EFI/OC.prev`; `<ESP>/vermilion/ROLLBACK-NVRAM-TEST.md` removed (its swap would now undo b154).
   - Check: `debug.phxp0odm_stat` `recovery:` line has `redone=`; at a login whose last window retried, `redone=1` and
     a picture. 2-3 restarts at 1440p60 HDR.
   - Rollback: `EFI/OC` and `EFI/OC.prev` swapped (from macOS under `/Volumes/NO NAME/EFI`, or from Linux under
     `/boot/EFI`, then power off fully).
5. **EFI change 14: VermilionBoot b155** (`f95b4baf`: drm's 32-try rule for the trainers' DPCD accesses, the
   `dpcd_aux:` stat line, the live switch `debug.phxp0odm_dbuf`, 0 at boot) on stage `vermilion-trust`; only the kext
   differs from change 13. **Installed ~04:49** (`EFI/OC` = b155 `f95b4baf`, `EFI/OC.prev` = change 13 b154 `78f560cc`, config identical). **Booted 04:52:** the login switch trained every window first time (`dpcd_aux: retried=0`); the `debug.phxp0odm_dbuf=1` test ran for the 1440p HDR plans; at the boot after the Linux session the LG NACKed all 32 tries twice and b154's redo ran ([finding](findings/2026-11-23-gpu-stability.md) sections 10-11).
   - Before: `~/vermilion-rollback/VermilionBoot.kext.b154`; `efi/build.py install` makes change 13 `EFI/OC.prev`
     (change 11 stays in `~/vermilion-rollback/EFI-OC.change11-20261123`).
   - Check: `debug.phxp0odm_stat` has `dpcd_aux:`; the login switch's `train_w` lines; `redone=0` if the AUX misses
     are gone. Then the live `debug.phxp0odm_dbuf=1` test (TODO 01, DRR double buffer).
   - Rollback: `EFI/OC` and `EFI/OC.prev` swapped (macOS: `/Volumes/NO NAME/EFI`; Linux: `/boot/EFI`, then power off
     fully).

## 01 GPU stability

- [~] The sink does not answer AUX at the first access after D3 (b153 03:51 and 04:15, b154 04:36). Linux's DC sends every
      DPCD access through drm_dp_dpcd_access (32 tries; at once after a timeout, 500 us after a NACK / DEFER); the
      trainer stopped after 4 timeouts or 1 NACK. **b155** (`f95b4baf`, on the box since 04:52): drm's rule for the
      trainers' DPCD accesses, a `dpcd_aux:` line in `debug.phxp0odm_stat`. Result: it does not cover this miss. At
      the boot after the Linux session the LG NACKed all 32 tries twice (`gave_up=2 nacks=74`), the training attempt
      failed and b154's redo ran (finding section 11). Open: what Linux does differently at that first access (it
      trains every window first time in the captures)
- [x] **Failed DP link training at a login mode switch** (1440p60 HDR, 2026-11-22 20:19, once, b150): black screen,
      every later switch refused until reboot. b151: Linux's 4 training attempts per link window, a failed from plan
      runs once more (2 per boot); b152 / b153: per-window record, Linux's pause; b154: a retried last window re-runs
      the switch. On the box 2026-11-23 03:11-04:36: every login switch at 1440p60 HDR trained, 5 needed a retry, one
      (04:15, b153) stayed black on a locked link until a re-switch, which b154 now does by itself (04:36)
- [x] The daemon reported `SUCCESS` during that black screen: `disp_check` (`DARK`, the engines' state in
      `/var/log/phxdark-*.txt`, one restart) for a failed engine; deployed 03:08. Its limit: the 04:15 kind of black
      (link locked, frames presented) reads as healthy; b154 covers that case at the source
- [ ] **Panic reports:** none is saved, because `DisableVariableWrite` blocks the kernel's NVRAM panic log (from the
      config; not watched failing here). The NVRAM test (change 12) is **inconclusive**: change 12 was rolled back from
      Linux at an unknown time, so the boots 04:06 / 04:10 / 04:15 may all have run change 11 (writes blocked). Open
      for the Linux session: read the firmware's variables from Linux (efivars) and redo the test as its own EFI
      change. Then a deliberate test panic is the only way to prove a report is saved (owner's call). Needed first for
      the restart panic of 2026-11-19 22:44 (cause unknown)
- [x] **Physical size 903 x 508 mm** (= 2560 x 1440 px at 72 dpi), where the EDID says 590 x 333 mm: every mode's
      `imageWidth / imageHeight` was 0 ([finding](findings/2026-11-23-gpu-stability.md) section 3). PhxAccel with the
      EDID's size installed 2026-11-23 04:15: `CGDisplayScreenSize` 591.1 x 332.5 mm
- [ ] Scaled ("looks like") modes: macOS lists only the native-pixel 2560-wide modes (no HiDPI twins); find what a
      real Mac's driver reports for them before adding any (low priority)
- [x] Changing the VRR floor gives macOS a new display identity (colour profile and settings reset), because the
      floor label is patched into the EDID PhxAccel serves. **Owner's decision 2026-11-23:** keep the patch (label
      60-240); documented in [XTX.md](../docs/XTX.md) ("A floor change resets the display's settings")
- [x] OpenCore logs: written at every boot, named in UTC (one hour behind CET); PROJECT_KNOWLEDGE fixed
- [~] Tearing at 1440p60 with GPU present; input lag at 60 Hz. **Input lag measured 2026-11-23** (finding section 9):
      posted mouse event -> OTG0 frame start median 27-30 ms at 60 Hz, 8.3 ms at 240 Hz; WindowServer composites at
      vsync and flips ~3 ms later, the flip waits for the next vblank (14 ms at 60 Hz): the normal one-frame compositor
      pipeline, no extra driver frame. Idea, not planned: start the composite later in the frame (late latch).
      **Tearing:** every flip is a vsync flip (FLIP_TYPE 0 in 40000 samples, never immediate), so a tear cannot come
      from the flip; still needs the owner's phone video (OWNER-TESTS 01) to say what is seen
- [~] DRR double buffer for the whole boot as on Linux, not only while Variable is on (decisions 2026-11-22,
      "Revisit"), then re-verify every plan. **b155** adds `debug.phxp0odm_dbuf` (live, 0 at boot): after the b155
      boot, set 1 and run session switches through every 1440p rate (SDR / HDR), post checks and the owner's eye; if
      all land, make 1 the default in a later build. **2026-11-23 04:56:** HDR 144, 240 (ODM + DSC), 120, 60 under
      mode 2: all DONE, first-attempt training, no underflow, owner saw every one land. Left: the SDR plans and the
      1080p / small modes under mode 2
- [x] Display sleep: tested 2026-11-23 (b155): the signal stays up while macOS's display sleeps; the LG never goes to
      standby. Linux's DPMS sequence captured; the fix moved to **05 Sleep and wake** (coordination thread, owner's OK)
- [ ] b138 freeze at 1280x1024 (low priority): leading guess is GPU present restarting on the small slot layout
      ([finding](findings/2026-11-20-b138-freeze-and-squat-loop.md)); then GPU present at every size
- [ ] Music's skipped readbacks: the `nvmtl_plane_mapped` guard stops the crash, but those pixels never reach the
      surface. A GPU mapping of the IOSurface pages, as a real driver does
- [ ] b150's underflow fix was checked at 144 Hz only (4 vs 0 underflows); check at 240 with ODM + DSC
- [ ] `bringup_navi31.sh` says "this card is rev 0xcc" and "non-kicker firmware": name both revisions; find out
      whether the XTX needs kicker firmware ([docs/XTX.md](../docs/XTX.md) #6)
- [ ] CHANGELOG 146-150 still mark as "(unverified)" what the owner has since confirmed (floor, ramp, 10-bit,
      Variable -> HDR)

## 02 GPU and CPU performance, power

- [ ] **Linux reference capture** (one boot, read-only): [HANDOFF-LINUX-REFERENCE.md](HANDOFF-LINUX-REFERENCE.md).
      Results go into PROJECT_KNOWLEDGE's performance record. Staged 2026-11-23 06:06: `<ESP>/vermilion/LINUX-THREAD02.md`
      (instructions for the Linux agent; it copies results to `<ESP>/vermilion/results-02/`). Afterwards copy them off
      and delete both from the ESP
- [ ] **CPU never above 5.15 GHz, idle package 40-50 W** (2026-11-23). Suspect: `HLT`-only idle, the firmware's `_CST`
      C2 / C3 (`inb 0x414` / `0x415`) unused, so no CC6 *(unverified)*. If Linux reaches ~5.5 GHz and idles 15+ W
      lower: a minimal `_CST` idle kext (never AMDRyzenCPUPowerManagement). BIOS C-state / CPPC settings first
      ([OWNER-TESTS](OWNER-TESTS.md))
- [ ] Real-app test set: WebGL / WebGPU in Safari and Chromium, a Metal game for 20 min, Minecraft (F3 renderer line)
- [x] XTX RADV profile chosen by PCI revision: `patches/mesa/0018` (Mesa's `navi31` for revision 0xc8; the name
      checks match every `navi31*`). Side-loaded: render -19 % GPU time ([perf finding](findings/2026-11-23-perf-power.md) 5)
- [ ] **Install RADV with 0018:** `libvulkan_radeon.dylib` from `~/src/vermilion/build-02/` to `/Library/GPUBundles/nvmtl/`,
      `PHXTRUST_KEEP=1 trust.sh`, EFI rebuild and install (one EFI change, one boot; rollback: `EFI/OC.prev` + the old
      dylib `8054fbe4` kept in `~/vermilion-rollback/`). Bundle with the other 02 boot items. Check `gpuCoreCount` = 96
      in an app afterwards
- [ ] Working set from VRAM instead of the fixed 6 GiB (`phx_globals.cpp`), together with the next item only
- [ ] Where buffers live: every BO is system memory today (`phx_client.cpp`). Measured: 44.7 GB/s buffer copy in
      Metal and in RADV directly (PCIe; VRAM ~960). Next: the Linux number, then VRAM-backed BOs (weeks; design note
      first: `.agents/findings/2026-11-23-vram-bo-design.md`)
- [ ] Fence waits by interrupt (`debug.phxih`, `debug.phxwait_irq`): **works on the XTX** (live test 2026-11-23, no
      storm in 1.19 M IVs; reverted). Median of a short waited job 265 -> 115 us, throughput 2x, but p99 +1.5-4 ms.
      Left: an interleaved A/B on an idle desktop (owner's OK: GPU load), the tail's cause, a daemon flag
      ([perf finding](findings/2026-11-23-perf-power.md) 4). The plugin itself adds ~55 us per waited submit (89 vs 34 us)
- [ ] Idle power (62-70 W board; UCLK pinned at 456 MHz, FCLK 2301): only what Linux sends, after the Linux numbers
- [ ] Fan readings: `SMCSuperIO` (in the pinned VirtualSMC 1.3.8 zip) once Linux names the Super I/O chip. One EFI
      change
- [ ] Per-core clock SMC keys missing from SMCProcessorAMD (low priority; `tools/sensors/cpuclk.c` covers it)
- [ ] `smumetrics.py` `CurrClock[GFXCLK]` always reads 1200 MHz: a mis-read field or a target; decode the SMU
      feature masks (0x71ffffff / 0x0003ebb8) against Linux's header
- [ ] The Mesa init line and `nvmtl-mesa ... Permission denied` on stderr of command-line Metal tools: from
      `MESA_SHADER_CACHE_DIR` inherited from the T3 Code process (pointing at root's cache). Apps launched normally
      have a cache. Find where T3 Code gets the variable; print the init line only in debug builds

## 03 Graphics APIs and video

- [ ] VCN decode (HEVC, AV1) through VideoToolbox: `vcn_up.sh` behind `phxboot-vcn`, then `src/video-decoder`.
      Check every register write against Linux first. The bundle goes to the sealed system volume
- [ ] Vulkan loader + ICD manifest for RADV, then a real window surface (Darwin WSI is stubbed in patch 0001)
- [ ] Zink for apps that load their own libGL (Minecraft / LWJGL), after the window surface
- [ ] Safari's WebKit GPU process doesn't map the plugin (sandbox: executables from system paths only;
      INSTALL.md 5.6 `relocate_gpubundle.sh`, never applied here). Two Safari `ExcUserFault` reports may be this
- [ ] Quick Look 12.2 s vs `sips` 1.6 s for a 6016x6016 HEIC: one cold run each. Re-measure (warm cache, n >= 5)
- [ ] Photos, video wallpaper, Quick Look on hardware decode once VCN works

## 04 DP / HDMI audio, HDMI output

- [ ] Inventory and a Linux capture: nothing in the stack implements DP / HDMI audio or HDMI output today

## 05 Sleep and wake

- [ ] **Display sleep (DPMS), from thread 01.** Today `pmset displaysleepnow` puts PhxAMDFB in power state 0, but
      OTG0, the link and the stream stay up (DPCD lanes locked, frames counting): the LG shows black and never goes to
      standby (owner, 2026-11-23 04:58, b155); wake (`caffeinate -u`) brings the picture back at once. A real Mac stops
      the signal; for the OLED that also means no standby-time panel care.
      - Capture (done 2026-11-23 05:05, Linux 7.2.15, capture v5 `tools/capture/linux_display_capture.py --dpms`):
        `~/vermilion-captures/dpms/display-20261123-050500` (not in git; no serials), every 1440p rate SDR and HDR:
        `trace-dpms-off.txt` (549-2409 writes; while off `OTG0_CONTROL 0x00001200`, no frames), `regs-off.txt`,
        `dpcd-off-*`, `trace-dpms-on.txt` (3778-10310 writes, 502-2127 DMUB commands: a full stream enable with link
        training). Finding: [2026-11-23-gpu-stability.md](findings/2026-11-23-gpu-stability.md) sections 10-11.
      - Plan: (1) a DPMS mode for `tools/capture/p0odm_from_trace.py` (its burst detection refuses both traces today:
        "no OTG0 unlock", "fewer than 2 bursts"); generate `phx_p0odm_<mode>_dpmsoff/_dpmson.inc` per 1440p mode;
        (2) replay engine: a `dark` state (off plan from the current mode, on plan back to it; b154's retry rule and
        the redo apply to the on plan's link windows); (3) PhxAccel: run them at power state 0 / 1 (IOFramebuffer
        power), with GPU present yielding the plane while OTG0 is off; (4) host tests, then one boot + the Allow click.
        Other resolutions: fixed rates today, keep the signal (or capture them too)
- [ ] Platform S3 test on a `-phxbringupoff` boot (one EFI change, `disablesleep 0` for that boot): wake by keyboard,
      mouse, power button; then USB, NVMe, Wi-Fi, Bluetooth, audio
- [ ] GPU resume path (the driver assumes a cold GPU today), after the S3 test
- [ ] Then sleep allowed again: `sudo pmset -a disablesleep 0` (blocked since 2026-11-23 00:58)

## 06 Fresh-install path

- [ ] **First:** can VermilionAccel load from OpenCore instead of the AuxKC (its IOGraphics / IOAccelerator
      dependencies)? Removes the Allow click and unblocks SIP / Secure Boot (09)
- [ ] VermilionTrust's cdhash list as an `Info.plist` property filled by `efi/build.py` from the installed files or a
      release manifest (EFI buildable on Linux); keep `PHXTRUST_KEEP`
- [ ] Daemon out of `~/gpu` into a system path; its arm logic compatible with a FileVault login (it refuses with a
      user logged in today)
- [ ] `phxfb-squat` set by OpenCore (`nvram_add`, Apple GUID) instead of from Linux *(unverified: does the daemon
      treat an OpenCore-set value the same?)*. Reset NVRAM deletes it today
- [ ] Monitor replay plans as data loaded at boot, not compiled into VermilionBoot
- [ ] Daemon flags (`sdma`, `cg`, `uclk`, `p0modes`, `wscdcopy`, `waitstep`, `vrrmin`, `autologin`) from the machine
      profile; `ws-gpu-present*` files, colour profiles (`~/gpu/tools/colorprofile.swift` is outside the repo),
      `smumetrics.py`, `metalbench`, `apiprobe` installed by the installer
- [ ] `recovery_install.sh` / `esp_stage.sh`: the air-runtime reader; pick the APFS Data role in Recovery (3
      candidates found)
- [ ] GitHub Releases and CI (mock tests on a Linux runner, a macOS runner building kexts, plugin, translator)
- [ ] `INSTALL.md` rewritten around the skills (it is upstream's manual XT guide); recovery and troubleshooting:
      black screen after a failed switch, the squat loop, booting `OC.prev`

## 07 Wireless and Continuity

- [ ] **Step 1, in this order:**
      1. router on 5 GHz channel 44 (not DFS 52);
      2. MtkWireless 1.1.2 (one EFI change; it claims the AirDrop `missingEndpoint` fix);
      3. `netstat -rn -f inet6 | grep awdl0` (look for a `fe80::%awdl0/64` route) and `ndp -an` (look for an entry
         for the peer);
      4. AirDrop both ways with `amtk_awdl_offchan=0`.

      Background in [the finding](findings/2026-11-22-audio-connectivity-inventory.md). The route / NDP check
      comes from issue #10 below *(unverified, web research 2026-11-23)*.
- [ ] [MtkWireless issue #10](https://github.com/Dino-VN/MtkWireless/issues/10) (closed, fixed in 1.1.2, commit
      `5715618`) is nearly this machine: a Ryzen 9 7900X, ASUS ROG STRIX B650E-F, MT7922 `14c3:0616`, Bluetooth
      `0489:e0e2` with MtkWirelessBT 1.1.1 + BlueToolFixup 2.7.2, MacPro7,1, macOS 26.7.3, and an iPhone17,1 on
      iOS 27.0.1.
      - AirDrop worked both ways there after a patch. The root cause was a missing `fe80::%awdl0/64` route and no NDP
        entry for the peer.
      - It only worked while the Mac was **associated on 5 GHz channel 44**. Unassociated, or on 2.4 GHz channel 6,
        AWDL never synced (the same as issue #6).
      - Check which of their extra patches are in 1.1.2 and how each interacts with our `amtk_awdl_offchan=0`: ROC
        grant reuse (`liveGrantServesNext`), clock re-acquire (`amtk_awdl_reacquire`), and mDNS replies on the query
        channel (`amtk_awdl_mdns_query_channel=1`).

      *(unverified, web research 2026-11-23)*
- [ ] Research option, not a plan: macOS 26 ships Apple's own MediaTek driver (`AppleSunriseWLAN.dext` +
      `AppleSunrise.kext`; [mediatek-sunrise-hackintosh](https://github.com/GeekAudio/mediatek-sunrise-hackintosh),
      from tonymacx86 thread 332855).
      - On 26.7.1 the MT7922's Bluetooth firmware is built in, but its Wi-Fi firmware is missing.
      - The route needs a device-id spoof (SSDT), the HAL kext from Apple's KDK, and the firmware written into the
        sealed system volume with authenticated-root off.
      - AWDL there: "seen on 26.3 beta, limited success on MT7922".
      - It would be Apple's own stack (Mac parity), but it conflicts with 09's SIP / sealed-system goals.
      - Apple's kext must never be redistributed: the user would extract it from their own KDK.

      *(unverified, web research 2026-11-23)*
- [ ] MtkWirelessBT's bluetoothd patches (BLE advertising for AirDrop / Handoff) exist only for 25G241 (26.7.1) and
      25G332 (26.7.2), and Instant Hotspot needs 25G332. So any macOS update (ROADMAP rule 12) is decided together
      with 09 *(unverified, web research 2026-11-23)*. Open: issue #10's machine ran 26.7.3 with AirDrop working,
      so find out which features really need the build-specific patches.
- [ ] Study [ForbiddenItlwm](https://github.com/retropineaple/ForbiddenItlwm) (fork of AirportItlwm, GPL-2.0; AWDL for
      Intel AX200/201/210/211) as the reference design for the MtkWireless fork: its AWDL, the separate `llw0`
      interface (Continuity Camera) and `BTLEAdvFixup.kext`; read its `docs/HOW-IT-WORKS.md` and `DEBUGGING.md`, and
      its credits (FranciumOS-org/AirPort_RTW89, DrogaBox/RTL8814Native for the Continuity Camera peer-cache records,
      seemoo-lab/owl). It claims, on 26.7.1-26.7.3: AirDrop both ways at ~20 MB/s, Universal Clipboard, Handoff,
      Continuity Camera; AirDrop prefers channel 149 / 80 MHz, iPhones use 44 where 149 isn't allowed. Public beta
      (0.6.0-beta.4, 2026-11-22), written with AI help. MtkWireless shares its zxystd driver layer and licence, so
      ideas and code can be ported *(unverified, owner's find 2026-11-23)*
- [ ] **Owner decides:** an Intel AX210 M.2 card (~20 EUR) with ForbiddenItlwm + IntelBluetoothFirmware instead of the
      MT7922. Community-maintained, not ours; the profile's `wifi:` field must then support both cards. One EFI change
      per boot as always *(unverified, owner's find 2026-11-23)*
- [ ] Universal Clipboard (probably the same AWDL link *(unverified)*), Continuity Camera wired (no frames,
      `EADDRNOTAVAIL` on the USB link) and wireless, Sidecar / Universal Control, Auto Unlock
- [ ] Fork MtkWireless only for what's left: adaptive AWDL off-channel, shorter windows, the per-second
      `mismatch of channel sequence` re-program
- [ ] Wi-Fi throughput drops for ~30 s every ~5 min (owner, 2026-11-19; never reproduced since the AWDL fix). Measure
      first: a 15 min ping / throughput log next to `log stream --predicate 'process == "airportd"'`
- [ ] Bluetooth / Wi-Fi coexistence (`wdutil`: BTC Mode Off): AirPods while transferring on 2.4 GHz
- [ ] Regulatory domain (driver country `00`; System Information shows DE / ETSI); possibly half the Wi-Fi
      throughput (upstream issue #11): measure on the LAN first
- [ ] Profile field `wifi:` for other cards (Intel, Broadcom)

## 09 Security and updates

- [ ] **VermilionTrust layout check:** at boot, decode `_mac_file_check_library_validation`, `_mac_file_check_mmap`
      and `_mac_file_check_mmap_downgrade` and check the slots they call through; a pre-update check against the new
      `ipsw`. Today a shifted layout most likely panics or wraps the wrong hook; it does **not** refuse cleanly
- [ ] `ipc_control_port_options=0`: find what needs it, then one boot without it
- [ ] AMFIPass 1.4.1: still loaded, probably useless next to VermilionTrust; one boot without it
- [ ] SIP `0x0A03` -> `0x0203` (check the daemon doesn't need unauthenticated root), then towards `0x0` after 06
- [ ] Apple Secure Boot (`SecureBootModel`) once nothing needs the AuxKC
- [ ] FileVault, after 06's daemon change
- [ ] Update policy: pending Safari 27.2, Rapid Security Responses (`CriticalUpdateInstall = 0`), the pre-update
      checklist (offsets, link gate, AuxKC), rollback. **No macOS update until then** (ROADMAP rule 12)
- [ ] Drop `NVMTLDriver-previous` (`8f6eb933`) from the trust list once the old plugin is gone
- [ ] Widevine in Chrome / Firefox ([OWNER-TESTS](OWNER-TESTS.md))

## 10 ryzentosh

- [x] Installer partition `disk0s4`: deleted on the owner's request (2026-11-23, thread 02: the Data volume was full)
      and the container grown to 107.4 GB. Left: a 26.7.1 reinstall path off the disk (Recovery would install the
      newest macOS), and fix `efi/README.md`
- [ ] NVMe shown as "external": `device_properties` in `efi/build.py` and the profile, `built-in = <01>` on
      `PciRoot(0x0)/Pci(0x1,0x2)/Pci(0x0,0x0)` (from ioreg, not checked with gfxutil); one EFI change (cosmetic)
- [ ] Generators: DSDT NOP patches (`fix_dsdt.py resolve` emits the plan, the patches were copied by hand),
      SSDT-CPUR from the MADT, the USB map from `usbwatch.py` (ports beyond 15 need two passes)
- [ ] ACPI dump without Linux (OpenCore SysReport, or macOS after a first boot from a generic profile)
- [ ] `iasl` and `smcread` pinned and fetched (both built or copied by hand today)
- [ ] Post-install check that `en0` is built-in before the Apple Account sign-in; a fix script with backups
- [ ] ROM when the build doesn't run on Linux on the target (`rom_from_interface: eno1` is a Linux name)
- [ ] OpenCore debug settings (`Target = 67`, `-v`) to `0` once stability is proven. No OpenCore log was written
      since 2026-11-22 23:20 although `Target = 67` is set (cause unknown)
- [ ] Optional: a Limine menu entry that chainloads OpenCore
- [ ] BIOS settings table (dGPU boot display, ReBAR, CSM off, C-states) checked against the board's menus

## 11 Final validation

- [ ] USB 3 halves of every rear port (paired by convention, never seen at SuperSpeed)
- [ ] Ethernet link with a cable; fallback AppleIntelI210Ethernet + `e1000=0` if the built-in driver fails;
      LAN throughput with an `iperf3` peer
- [ ] Time Machine first backup and restore; Recovery boot (with the GPU thread's OK)
- [ ] Stress and soak: 30 min CPU, 30 min `metalbench`, both together, `memtester`, 8 h idle
- [ ] Audio: hot-plug of the line-out, microphone, 24-bit default, the bass distortion at 50 %
- [ ] Desktop performance baseline: app launch times, Mission Control / Stage Manager smoothness, idle CPU
- [ ] Sharing (Screen Sharing, SSH, SMB; the Public folder is guest-writable), printers, Xcode and simulators
- [ ] The three JetsamEvent memory-pressure kills (not read yet)
- [ ] Shutdown stall report at almost every restart (~2-3 s, normal teardown; low priority)
- [ ] Cold shutdown (not restart): SMART "Unsafe Shutdowns" still 212

## 12 Final refactor

- [ ] Regenerate every replay plan from a capture directory and compare, in a script (by hand today)
- [ ] The connector table from the VBIOS parser (`PhxVbios.h` already produces the same table)
- [ ] Machine-specific values into per-board / per-monitor data files with detection
- [ ] Latent on the XTX ([docs/XTX.md](../docs/XTX.md)): `mesboot.c` `SYSTEM_APERTURE_HIGH` from `FB_LOCATION_TOP`
      (sized for 20 GiB), the pipe-1 HUBP1 aperture, the Metal core count from the profile (`nvmtl_corecount.c`)
- [ ] EDID: the E-DDC segment pointer and all 384 bytes (`PhxDdc.h`, `PhxEdid.h`); mode matching by EDID timings
      instead of the fixed tables
- [ ] One boot log line with the PCI revision, DMCUB version, OTG0 totals, DP link rate / lanes, BAR0 length, VRAM
- [ ] Split `nvmtl_vk.c`, `NVMTLObjects.m`, `nvrm-accel.cpp` where reviews get easier, in separate commits
- [ ] Mock tests: five suites that already failed before thread 00 (`phxctl_serve_test.sh` 1 failed;
      `b21guard_mut.sh` 1 bad, `bootflags_mut.sh` 2 bad, `bootrate_mut.sh` 4 missed + 1 bad, `wsprefs_mut.sh` 1 bad);
      a first run on a real Linux machine
      ([finding](findings/2026-11-18-mock-tests-on-linux.md))
- [ ] Upstream PRs to leolivm (VermilionTrust first, the XTX profile and detection, derived apertures, portable tests)

## Later: virtualization (after the series)

- [ ] macOS as a VM with the dGPU passed through (VFIO), Linux on the iGPU: reset strategy for Navi 31 (Vermilion
      needs a cold GPU), VM definition, the `macos` launcher. The macOS guest still has no Hypervisor.framework
