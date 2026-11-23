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

State at the end of thread 01 (verified, recorded in PROJECT_KNOWLEDGE): `EFI/OC` = change 14 (VermilionBoot b155),
`EFI/OC.prev` = change 13 (b154), `EFI/OC.bad` = change 12. Thread 01's per-change steps: git history of this file
(`fork/main` `c9f132e`).

Thread 02 (2026-11-23): EFI change 15 booted 07:10 and is verified (PROJECT_KNOWLEDGE, known-good); the Linux run's
results are in `~/vermilion-captures/linux-ref-20261123/` and the ESP is clean again (07:22).

Thread 02 part B: **EFI change 16** (VermilionBoot b156, VRAM-backed BOs, off at boot) booted 08:54 and is verified
(PROJECT_KNOWLEDGE). Rollback: `EFI/OC.prev` = change 15, copy in `~/vermilion-rollback/EFI-OC.change15-20261123`.

Thread 02 part C: **EFI change 17** (VermilionBoot b157: GPU time per context, off by default) **plus mesboot with
the system aperture from FB_LOCATION** (24 GiB on the XTX, `0x00217fbf`; the XT keeps `0x00213fbf`), installed together
2026-11-23, **booted 10:29 and verified** (desktop 1440p240 HDR + VRR, `0x28DE` = `0x00217fbf`, pool `[512, 24464)` MiB,
self-test 1, `debug.phxgpu_time` answers). Check after the boot was: accelerated desktop at 1440p240 HDR; `phxctl regs 0x28DE` reads
`0x00217fbf`; `debug.phxbo_vram_stat` pool `[512, 24464)` MiB and `debug.phxbo_vram_selftest` 1 (it now tests the
pool's new top MiB); `sysctl debug.phxgpu_time` answers. Rollback: EFI `EFI/OC.prev` = change 16 (copy in
`~/vermilion-rollback/EFI-OC.change16-20261123`); mesboot `sudo cp ~/vermilion-rollback/mesboot.pre-aperture
/usr/local/sbin/mesboot`. If the desktop fails, roll back mesboot first (the aperture is the riskier half).

Thread 02 part C: **EFI change 21** (stage `vermilion-trust-bar1g`): VermilionBoot **b160** (the parking of mapped
CPU-visible BOs can't run out) and **VermilionTrust rebuilt for a new NVMTLDriver** (app CAMetalLayer drawables drawn
TILED, copied to their pages at present: `NVMTL_DRAWABLE_TILED`, on by default outside WindowServer). Installed
2026-11-23, **booted 16:40 on the first try** (verbose text, no Apple logo): `phxtrust_stat` enforce, denied_unlisted 0;
self-test 1; desktop survived the daemon's watch; the plugin logs "drawables: TILED". **Verified with Horizon** (16:43):
its three drawables drawn TILED, the screenshot matches the old path's, 292 fps vs 172 with `NVMTL_DRAWABLE_TILED=0`
(finding horizon-present-cost). Still to see: a Chromium/Electron app's window over a few days (OWNER-TESTS). Check after the boot: accelerated desktop 1440p240 HDR + VRR; `sysctl debug.phxtrust_stat`
`mode=enforce`, `denied_unlisted=0`; `debug.phxbo_vram_selftest` 1; `/tmp/nvmtl.log` has "CAMetalLayer drawable drawn
TILED" for a Metal app and its window looks right (Horizon; a Chromium/Electron app). Rollbacks, smallest first:
(1) a broken app window: `sudo touch /Library/GPUBundles/nvmtl/drawable-tiled-off` and relaunch the app (no restart);
(2) the old plugin: `~/vermilion-rollback/NVMTLDriver.pre-drawtiled-20261123` back into
`/Library/GPUBundles/NVMTLDriver.bundle/Contents/MacOS/` (.new + mv; the new trust list keeps it as `-previous`);
(3) EFI: `EFI/OC.prev` = change 20 (copy in `~/vermilion-rollback/EFI-OC.change20-20261123`); restore the old plugin
first (change 20's trust list doesn't know the new one). No desktop at all: boot Linux, `mv /boot/EFI/OC
/boot/EFI/OC.b160-bad && mv /boot/EFI/OC.prev /boot/EFI/OC`, then the old plugin from macOS.

Thread 02 part C: **EFI change 20** (stage `vermilion-trust-bar1g` with VermilionBoot b159: CPU-visible VRAM BOs in
[352 MiB, 1 GiB) of BAR0, used only by processes in `debug.phxbo_vram_proc`), installed 2026-11-23, **booted 15:49 and verified**: desktop
1440p240 HDR, `phx-fb-aper-len` 1073741824, self-test 1 (step 3's BAR0 read-back at 352 and 1023 MiB passed), hi pool
`[1024, 24464)` MiB, CPU-visible pool `[352, 1024)` MiB. **The first boot attempt after the restart at 15:45 failed**
(owner: "one restart didn't work, the second one did"): the shutdown was clean (logs to 15:45:02), the failed boot left
no log at all (the next line is the good boot at 15:49:15), so it stopped before logd; what the screen showed: asked.
Cause unknown (unverified whether b159, the 1 GiB BAR or a warm restart; b159's `start` only maps 1 GiB instead of 512 MiB).
Check after the boot: accelerated desktop 1440p240 HDR + VRR; `ioreg -l | grep phx-fb-aper-len` 1073741824;
`debug.phxbo_vram_selftest` 1 (now with step 3, the BAR0 read-back); `debug.phxbo_vram_stat` shows the hi pool from
1024 MiB and a "CPU-visible pool" `[352, 1024)` MiB. Rollback: `EFI/OC.prev` = change 19 (copy in
`~/vermilion-rollback/EFI-OC.change19-20261123`); if macOS does not reach the desktop, boot Linux and
`mv /boot/EFI/OC /boot/EFI/OC.b159-bad && mv /boot/EFI/OC.prev /boot/EFI/OC`.

Thread 02 part C, follow-ups to b159:
- [x] (1) The leak: a process that exits with its CPU-visible BOs still mapped parks each one (Horizon: ~80 per run),
  and past b159's 256 slots the rest leaked until reboot. **Fixed in b160** (one slot per possible BO, reaped on every
  allocation and stat read), built 2026-11-23, **not installed**: it goes in with the next build that needs a restart.
  **No process other than `horizon-client` joins `debug.phxbo_vram_proc` until b160 runs.**
- [ ] (2) Boots that stop without a macOS log: the counter is in OWNER-TESTS 01 (watch item). #2 (15:45, b159, warm)
  stopped in OpenCore with no GOP. A third one on b159 means reverting to EFI change 19 before going on.
- [ ] (3) Horizon is GPU-bound at 94 % (finding horizon-present-cost, "b159"): profile the main pass.

Thread 02 part C: **EFI change 19** (stage `vermilion-trust-bar1g`: OpenCore `ResizeAppleGpuBars` 0 -> 10, BAR0 1 GiB
for macOS; config.plist only, VermilionBoot b158 unchanged), installed 2026-11-23, **booted 11:09 and verified**: `phx-bar0-len`
1073741824, `phx-fb-aper-len` 536870912, desktop 1440p240 HDR + VRR, VRAM self-test 1. New on this boot (owner): an
Apple logo with a progress bar instead of the verbose text (`-v` is in the boot-args), and no password prompt;
cause not looked into (unverified whether it comes from the BAR change).
Check after the boot: the machine boots to the accelerated desktop 1440p240 HDR; `ioreg -l | grep phx-bar0-len` reads
1073741824 (or the largest size the card supports up to 1 GiB); `phx-fb-aper-len` 536870912 (kFbAperMapMax); the
VRAM self-test 1. Rollback: `EFI/OC.prev` = change 18 (copy in `~/vermilion-rollback/EFI-OC.change18-20261123`); if
macOS does not reach the desktop at all, boot Linux and `mv /boot/EFI/OC /boot/EFI/OC.bar1g-bad && mv /boot/EFI/OC.prev /boot/EFI/OC` (`OC.bad` is taken by change 12) (as [runbooks](runbooks.md) line ~204).

Thread 02 part C: **EFI change 18** (VermilionBoot b158: per-submission GPU trace, off by default), installed
2026-11-23 ~10:55, **booted 10:57 and verified** (desktop, trace works, pool [512, 24464) MiB). Check after the boot was: accelerated desktop 1440p240 HDR; `sysctl
debug.phxgpu_trace` answers; pool `[512, 24464)` MiB. Rollback: `EFI/OC.prev` = change 17 (copy in
`~/vermilion-rollback/EFI-OC.change17-20261123`). The same install replaced a file Linux's fsck had renamed on the
ESP (`MtkWirelessBT.kext/Contents/Resources/FSCK0000.000`, the libsbc license text).

Thread 02 part B: the second Linux reading is in ([linux-02b](findings/2026-11-23-linux-02b.md)); the ESP is clean (08:24).

The thread 02 daemon (`irqwait_boot`, `idledpm_boot`, `idle_dpm.sh`; flags `irqwait`, `idledpm`, `idledpm-dcf`) booted
07:51 and is verified (PROJECT_KNOWLEDGE). Rollback: `rm ~/gpu/phxboot-irqwait ~/gpu/phxboot-idledpm
~/gpu/phxboot-idledpm-dcf`; the previous daemon is `~/vermilion-rollback/phxboot-navi31.sh.pre-thread02`.

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

- [-] CPU never above 5.15 GHz, idle ~40 W: Linux does the same (5.16 GHz, 37.8 W with CC6 98 %, BIOS all Auto), so
      no `_CST` idle kext for ~2-3 W (decisions 2026-11-23; [perf finding](findings/2026-11-23-perf-power.md) 8)
- [ ] Real-app test set: WebGL / WebGPU in Safari and Chromium, a Metal game for 20 min, Minecraft (F3 renderer line)
- [x] XTX RADV profile chosen by PCI revision: `patches/mesa/0018` (Mesa's `navi31` for revision 0xc8; the name
      checks match every `navi31*`). Side-loaded: render -19 % GPU time ([perf finding](findings/2026-11-23-perf-power.md) 5).
      Installed with EFI change 15 (`profile navi31`, Metal "AMD Radeon RX 7900 XTX")
- [ ] Working set from VRAM instead of the fixed 6 GiB (`phx_globals.cpp`): PR B, with the VRAM buffers (design note
      step 6); raising it while buffers are system memory would let apps wire 24 GiB of RAM
- [~] **VRAM-backed BOs** (b156, opt-in per process, [design + M1 / M2](findings/2026-11-23-vram-bo-design.md)):
      840.9 GB/s (Linux 837), `zerocheck` clean. Daemon flag `phxboot-bovram` (names; mock-tested, installed
      2026-11-23 without the flag). Benchmark set 09:45 (PROJECT_KNOWLEDGE): compute 2.9x, Metal render 1.9x, both
      bandwidths ~840 GB/s, zerocheck clean, no leak; Horizon (Metal game) 88 -> 158 fps at 1440p. Left: the real-app set before / after (OWNER-TESTS 02); days of use (OWNER-TESTS 02), then M3 (all processes, the default;
      final call in thread 11 if open); a faster clear before M3 (~8 GB/s now); the GFXHUB aperture to 24 GiB
      (XTX.md row 3, Linux's value) to open the pool's last 4 GiB
- [ ] Fence waits by interrupt (`debug.phxih`, `debug.phxwait_irq`): **works on the XTX** (live test 2026-11-23, no
      storm in 1.19 M IVs; reverted). Median of a short waited job 265 -> 115 us, throughput 2x, but p99 +1.5-4 ms.
      Quiet interleaved A/B (06:25): better in median and p99 everywhere. On at boot since 07:51 (`phxboot-irqwait`):
      124 us vk / 207 us Metal median after the restart. Left: a day of use, then INSTALL's recommended set
      ([perf finding](findings/2026-11-23-perf-power.md) 4). The plugin itself adds ~55 us per waited submit (89 vs 34 us)
- [~] **Idle power:** live 53.5 -> 23.5 W socket at 1440p240 HDR with Linux's messages, 0 underflows in 20 min, nothing
      slower under load ([perf finding](findings/2026-11-23-perf-power.md) 9). On at boot since 07:51 (socket 23 W idle).
      Left: a few days of use; other modes (60 / 120 / 144, SDR) only seen through the mock so far
- [ ] Fan readings: `SMCSuperIO` (pinned VirtualSMC 1.3.8 zip; knows the NCT6799D). Chip named 2026-11-23: **NCT6799D at
      0x2e:0x290**, fan2 1046 RPM ([linux-02b](findings/2026-11-23-linux-02b.md)). Linux reads it through ASUS's ACPI WMI
      methods; SMCSuperIO uses the ports directly (nothing on macOS calls those methods; a clash unverified). Its own EFI
      change (one per boot): owner's call when, not bundled with the VRAM kext boot
- [ ] Per-core clock SMC keys missing from SMCProcessorAMD (low priority; `tools/sensors/cpuclk.c` covers it)
- [ ] `smumetrics.py` `CurrClock[GFXCLK]` always reads 1200 MHz: a mis-read field or a target. The SMU feature masks
      (0x71ffffff / 0x0003ebb8) are decoded (`tools/phxctl/smufeatures.py`) and equal Linux's
- [~] The Mesa init line and `nvmtl-mesa ... Permission denied` on stderr of command-line Metal tools. Found: loginwindow
      runs as root with the plugin, which `setenv`s `MESA_SHADER_CACHE_DIR` (root's cache) and `NVRM_FLUSH_TIMEOUT_MS`;
      the apps loginwindow reopens at login (T3 Code) inherit both, so they run without a Mesa disk cache. Fixed in the
      plugin (unset once RADV is up) and patch 0019 (the init line to the unified log): change 15. Verified 07:22: no
      `MESA_*` / `NVRM_*` in T3 Code's environment; the init line in the unified log only. Left: the `Permission denied`
      line, if a tool still prints it
- [ ] RADV's canned `navi31` firmware versions (ME 1486, PFP 1525, MEC 494) vs Linux's (2540 / 2710 / 2790, all versions
      in [linux-02b](findings/2026-11-23-linux-02b.md)): compare with what the firmware macOS loads (`tools/firmware/fw.lock`)
      reports, and whether RADV keys anything off them

- [ ] **A fixed cost per presented frame** (Horizon fullscreen 1440p ~160 fps vs >3x on Linux by the owner's report;
      a 1280x720 window gives 328 fps): the app's drawable IOSurface in system memory plus WindowServer's composite,
      ~4-5 ms per 1440p frame ([finding](findings/2026-11-23-horizon-present-cost.md)). **Measured with b157:**
      Horizon 3.47 ms + WindowServer's composite 2.20 ms per frame, gfx queue 93 % full. **Linux twin (10:45):
      4370 fps, p50 0.22 ms** for the same view ([linux-horizon](findings/2026-11-23-linux-horizon.md)): ~25x. Even
      Horizon's own GPU time on macOS (3.47 ms) is >15x Linux's whole frame. **b158 trace:** the main pass 2.35 ms (5.39
      off), the drawable pass 0.75 ms, WindowServer 3 x ~0.7 ms. Next, by reach: (1) CPU-visible VRAM for Shared
      buffers (Resizable BAR in macOS: OpenCore `ResizeAppleGpuBars` plus the kext's BAR0 window); (2) GPU-used
      IOSurfaces resident in VRAM (drawables, WindowServer's surfaces); (3) M3, VRAM BOs for every process.
- [ ] **GPU hang under clpeak's MPS / CoreML pass** (2026-11-23 09:15, VRAM BOs off, no VM fault; WindowServer
      starved, daemon reverted to GOP: [finding](findings/2026-11-23-clpeak-hang.md)). Find the MPS kernel that does
      not finish (clpeak's `metal:0` runs MPSMatrix, `coreml:0` MPSNDArray); keep clpeak out of the benchmark set until then.

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
