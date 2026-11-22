# TODO

The goal: a fully working, stable macOS Tahoe on the XTX machine ([docs/REFERENCE-MACHINES.md](../docs/REFERENCE-MACHINES.md)).
That means the GPU accelerated at 1440p high refresh, plus Wi-Fi, Bluetooth, Ethernet, audio and USB. After that, the
same install as a VFIO VM. Then clean up, document and hand over to the community.

Rules: tick items when done (with the date), add new ones where they belong, link findings. `[ ]` open, `[~]` in
progress, `[x]` done, `[-]` dropped (say why).

## Current state (2026-11-22): the display works fully at 2560x1440

On the XTX machine, from System Settings: every rate to 240 Hz, Variable (60-240 Hz) with the ramp, 10-bit HDR, and
Variable with HDR on (VermilionBoot b150). Summary, installed binaries, rollback and the open items: "Display polish"
below; how it works: [docs/XTX.md](../docs/XTX.md). Branch `ownback/gpu-refresh-hdr` (PR #1 to `fork/main`) had its
clean-up pass on 2026-11-22: comments and docs only, the binaries unchanged.

Next, in the owner's order (2026-11-22): performance and heat, the remaining APIs, sleep / wake, audio, HDMI.

## GPU performance, heat and APIs (branch `ownback/gpu-performance`, started 2026-11-22)

Owner's goal: "I need this GPU perfectly working". Baseline, measured on the XTX machine:
[findings](findings/2026-11-22-gpu-performance-baseline.md). In short: the GPU clocks up and down by itself (63 W
board idle, 389 W / 88 C hotspot / fan 49 % under a full Metal load, idle again within 3 s); Metal 3 works; OpenGL is
the software renderer; OpenCL has no GPU; no Vulkan loader; apps get no Mesa shader disk cache; Metal reports 6 GiB
and the XT's name. Not in this branch: sleep / wake, DP audio, HDMI.

**Paused 2026-11-22 after the baseline** (owner: ACPI, Wi-Fi, USB, Bluetooth and the rest come first, in another
thread). Nothing is staged or half-installed. This branch is then the general performance branch: GPU and CPU
performance, loads, power use, heat. When resuming: measure the baseline again first (the EFI and ACPI work in
between changes the CPU side), then go on at step 1 / 2.

Order of work (one change at a time; every kext change = a new build, a CHANGELOG line, the host and mock tests, one
EFI change per boot with a copy in `~/vermilion-rollback/` and the rollback steps written here first):

- [x] 2026-11-22 Baseline: idle and load (clocks, power, temperatures, fan), CPU wakeups, what each API does;
      `tools/metaltest/metalbench.m`, `tools/metaltest/apiprobe.m`
- [ ] **1. Linux reference** (a `/boot/vermilion/HANDOFF-*.md` note for the Omarchy session; read-only there; back to
      macOS = full power-off): at 1440p 240 Hz HDR and 60 Hz, idle and under a load: `power1_average`, the
      temperatures, the fan, `pp_dpm_sclk` / `mclk` / `fclk` / `socclk`, `amdgpu_pm_info`, `pp_features`, the SMU
      metrics table, whether UCLK is pinned, GFXOFF residency. Plus the same Mandelbrot shader through Vulkan for a
      frames/s number. Answers: is 63 W idle normal, and how far is Metal from native
- [ ] **2. Shader disk cache for apps** (plugin / RADV environment, no kext): Mesa's cache folder is root's, so user
      processes run without one. Measure a cold and a warm launch before and after
- [ ] **3. What Metal reports** (detect, do not overwrite): the XTX's name and RADV profile by PCI revision
      (docs/XTX.md items 1, 2, 5), the working-set size from the VRAM the kext reads instead of the fixed 6 GiB
      (first find out what the budgets guard: buffer objects are system memory)
- [ ] **4. Where buffer objects live.** Add a memory-bound mode to `metalbench` (large textures, blits) and compare
      with step 1. If system-memory buffers cost much: research VRAM-backed buffer objects (a design item, findings
      first)
- [ ] **5. Fence waits by interrupt** instead of the 200 us poll (`debug.phxih`, `debug.phxwait_irq`: the path exists,
      off and unverified on the XTX). 101 us per waited submit today, 6 us of it GPU. Read the code, then a live test
      with the sysctls and their rollback (set back to 0)
- [ ] **6. On-screen frame pacing:** an app window at 240 Hz and with Variable on (present gaps, the copy per
      present, tearing; the open items of 2026-11-19 below: schedule-time copy, tearing, input lag)
- [ ] **7. Idle power and heat**, after step 1 says what Linux does: the UCLK pin (only what Linux sends: it unpins
      when the display can take a switch), FCLK, GFXOFF. Nothing here before the Linux numbers
- [ ] **8. OpenGL** (software renderer today; Prism Launcher / Minecraft is installed on the machine): options to
      research: Mesa Zink on RADV for apps that can load their own libGL, ANGLE for GLES. Ask the owner which
      OpenGL apps matter
- [ ] **9. Vulkan:** MoltenVK apps through the plugin (test); a loader + ICD manifest for RADV directly (does the
      Darwin backend have a window-system surface?)
- [ ] **10. The rest of the API table:** WebGL / WebGPU in Safari and Chromium, video decode (VCN, `phxboot-vcn`),
      OpenCL (none; decide whether anything needs it), real apps and games; rendering issues the owner reports
- [ ] CPU temperature and power are not readable on this install (no AMD sensor kext). An EFI change, so it belongs
      to the EFI's owner thread; without it "the CPU runs hot" stays unmeasured
- [ ] Wrap-up: a clean-up pass with no behaviour change, docs (docs/XTX.md, docs/BUILD.md for the new tools)

## Earlier (2026-11-19, 22:10): flicker fixed (E3); restart 2 of the daemon flag pending; E3 permanent (verified); wait step 200 daemon flag installed, first active next restart; one panic at a restart, cause unknown

## Earlier (10:20): graphics resumed on the Mac (branch `ownback/gpu-1440p-polish`)

GPU present flicker and heat measured read-only on the XTX machine ([findings](findings/2026-11-19-gpu-present-flicker-heat.md)).
The flicker most likely comes from an ordering bug: with the kernel's GPU marks on (`debug.nvaccel_surface_gpu_on 1`)
and `ws-gpu-present-async` absent, the plugin copies WindowServer's frame at *schedule* time, not after the composite
finished. Ruled out: plane loss, the b74 latch race (0 on-screen slots reused), display underflow. Heat: WindowServer
wakes ~1300 times/s (the fence wait's 50 us timed steps), and presents come faster than the 60 Hz refresh.

- [x] 2026-11-19 10:17 Flicker step 1: `/Library/GPUBundles/nvmtl/ws-gpu-present-async` created live: no change
      to the flicker (owner); it stays on (correct ordering, needed by E3). If a later boot misbehaves, Linux can't
      delete it (apfs-fuse is read-only): `<EFI>/vermilion/run-once.sh` with `rm -f /Library/GPUBundles/nvmtl/ws-gpu-present-async`
- [x] 2026-11-19 Flicker step 2, **fixed** (owner, boot 10:28): E3 for one boot: `<EFI>/vermilion/run-once.sh` sets `NVMTL_WS_PRESENT_CDCOPY=1`
      for one boot (E3: present CoreDisplay's display surfaces). Boot: Apple menu > Restart (macOS -> macOS, no power-off
      needed); in the OpenCore picker Macintosh HD; **no Reset NVRAM**. After the boot, check: `grep run-once
      /var/log/phxboot.log` shows `NVMTL_WS_PRESENT_CDCOPY=1`; the unified log (WindowServer, NVMTL) shows
      `presenting CoreDisplay's display surfaces` and `is a display surface`; the owner checks the flicker.
      **Rollback:** nothing to undo on disk (EFI/OC unchanged; the env var lasts one boot; run-once is moved off the ESP
      before it runs). Black screen: hold power, power on, OpenCore > Macintosh HD = the boot before. From Linux (Limine,
      default): not needed; the same power cycle is enough
- [x] 2026-11-19 22:46 Flicker step 3, make E3 permanent: restart 2 showed the daemon's own `wscdcopy:` line (verified). **restart 1 done 22:00** (daemon installed, `.prev` kept, flag set, E3 on, no
      flicker); **restart 2 still to verify** the daemon's own `wscdcopy:` line. Staged 2026-11-19 21:58:
      daemon flag `phxboot-wscdcopy` (commit 6ba5f6e, mock 175/175) + `tools/phxctl/daemon_install.sh` (f7a7d4a).
      On the EFI partition: `vermilion/flags/phxboot-wscdcopy`, `vermilion/daemon/` (daemon sha256 36dc5db5...),
      `vermilion/run-once.sh` (installs the daemon into ~/gpu with `~/gpu/phxboot-navi31.sh.prev`, and sets the variable
      for that boot itself, because that boot still runs the old daemon). Tested as root end to end through the real
      `esp_flags.sh` against a fake root (9/9). **Restart 1** (any normal Apple menu > Restart): check `grep -E
      'wscdcopy|daemon-install|run-once' /var/log/phxboot.log` and no flicker. **Restart 2**: the daemon's own line
      `wscdcopy: NVMTL_WS_PRESENT_CDCOPY=1 for the restarted WindowServer` = verified.
      **Rollback:** black screen: hold power, power on, OpenCore > Macintosh HD (the daemon has its own fallbacks: kill
      switch, pending mark, GOP revert). Old daemon back: a `run-once.sh` with `mv /Users/<user>/gpu/phxboot-navi31.sh.prev
      /Users/<user>/gpu/phxboot-navi31.sh`; flag off: `<EFI>/vermilion/flags/phxboot-wscdcopy.rm`. EFI/OC unchanged
- [ ] GPU idle power: 66-67 W board / 53 W socket at the 1440p60 desktop (2026-11-19 23:00, smumetrics). Compare with
      Linux amdgpu idle on this machine (sysfs power1_average, read-only) before changing anything
- [ ] Plugin: fix the schedule-time copy of the non-async GPU present (present only after completion)
- [~] Heat: re-measured with E3 (22:03): WindowServer wakeups 700-1050/s (was 1300-1400), CPU about the same.
      2026-11-19 22:07 `debug.phxwait_step_us` 50 -> 200 live: WindowServer wakeups ~255/s (from 640-950), CPU the same,
      each fence wait +~240 us; owner "can't tell or it's worse": **set back to 50** at 22:09:58
      ([findings](findings/2026-11-19-gpu-present-flicker-heat.md), live test 3). 22:16-22:28 blind A/B (AABB):
      200 **not worse** (the owner's smoothest stretch was a 200 one), WindowServer wakeups 137-352/s median vs
      1673-2458/s at 50 (live test 4). Live at 200 since 22:29:47 (this boot only).
- [~] Heat: make 200 permanent: daemon flag `phxboot-waitstep` (commit 8e3718b, daemon sha256 5c2c1513..., mock 189/189,
      b15test 159/159). **Staged 2026-11-19 22:38** on the EFI partition: `vermilion/flags/phxboot-waitstep` (200),
      `vermilion/daemon/` (new daemon; the E3 bundle kept as `vermilion/daemon.prev`), `vermilion/run-once.sh` (installs
      the daemon into ~/gpu with `~/gpu/phxboot-navi31.sh.prev` = the E3 daemon 36dc5db5, and sets the sysctl for that
      boot itself, because that boot still runs the old daemon). Tested as root end to end through the real
      `esp_flags.sh` against a fake root (12/12). **Next restart** (Apple menu > Restart, OpenCore > Macintosh HD, no
      Reset NVRAM) is also restart 2 of the E3 flag: check `grep -E 'wscdcopy|waitstep|daemon-install|run-once'
      /var/log/phxboot.log` = the old daemon's own `wscdcopy: NVMTL_WS_PRESENT_CDCOPY=1 ...` line, `daemon-install:
      installed ... 5c2c1513...` and `run-once: debug.phxwait_step_us=200 for this boot rc 0, reads 200`; `sysctl
      debug.phxwait_step_us` = 200. **The restart after** shows the daemon's own `waitstep: debug.phxwait_step_us=200 rc 0`.
      **22:46 boot:** daemon installed (`.prev` = E3 daemon), flag = 200; the run-once's own sysctl failed (`unknown oid`:
      PhxBringup registers its sysctls with the first user client, after the pre-hook), so that boot runs at 50. **Next
      restart** = the first one where the daemon sets it: check `grep waitstep /var/log/phxboot.log` and `sysctl
      debug.phxwait_step_us` = 200
      **Rollback:** the value: `rm ~/gpu/phxboot-waitstep` (a user file; next boot = 50), or live `sudo sysctl -w
      debug.phxwait_step_us=50`, or from Linux `<EFI>/vermilion/flags/phxboot-waitstep.rm`. The E3 daemon back: a
      `run-once.sh` with `mv /Users/<user>/gpu/phxboot-navi31.sh.prev /Users/<user>/gpu/phxboot-navi31.sh`. Black screen:
      hold power, power on, OpenCore > Macintosh HD (the daemon's fallbacks are unchanged). EFI/OC unchanged
- [ ] Kernel panic at the 22:44 restart (most likely at the end of the shutdown: no OpenCore log, no phxboot run in
      between; no panic report saved; [findings](findings/2026-11-19-gpu-present-flicker-heat.md)). If it happens again:
      a photo of the panic screen. Then: why no panic report is saved on this install
- [~] Screen tearing at 1440p60 with GPU present (owner, live test 4, both wait steps). Flips are vsync and wait for the
      copy (code). 22:55 `ws-cdcopy-hold` 1000 us: no change, 1.1 % of presents superseded; deleted (live test 5).
      Next: a phone video of the tearing (where, which motion) before any further change
- [ ] Input lag at 60 Hz (owner: "always been bad"): part may be macOS pointer acceleration; measure before changing anything
- [x] 2026-11-22 **TARGET (owner, restated 2026-11-20): the LG at every resolution and refresh rate it supports, incl.
      2560x1440 at 240 Hz, with HDR and Adaptive Sync, all from System Settings.** Reached at 2560x1440 (every rate,
      Variable, HDR, both together); the other resolutions run at their fixed rates, without VRR / HDR by the owner's
      choice (see "Display polish" below). The captures behind the replay plans (v2, v3, v4) are in
      `~/vermilion-captures/capture/` (outside git).
- [ ] 2026-11-20 **b138 froze the machine at 1280x1024; the recovery looped** ([findings](findings/2026-11-20-b138-freeze-and-squat-loop.md)).
      16:39:21 the switch to 1280x1024@75 landed (75.000 Hz), ~0.5 s later the whole machine stopped (no log, no panic
      report: DisableVariableWrite also blocks the NVRAM panic log). Then three boots in a row rebooted "onto IONDRV" and
      came back squatting: `phxfb-squat` was set from Linux and macOS cannot clear it. The owner fixed it from Linux
      (EFI `OC.bad` = b138, `OC` = b137 again, `phxfb-squat` deleted). Machine now: EFI b137, PhxAccel b138 and plugin
      b138 still installed (inert without phx-p0-keys), GOP desktop. 19:40 kill switch removed (the new daemon
      makes a squat boot safe); back to the accelerated b137 desktop = `phxfb-squat` set from Linux again (line ~299).
  - [x] 2026-11-20 Daemon: a squat boot that can't leave the squat no longer reboots. The second squat boot runs a
        safe desktop (CPU copy at the boot mode, no mode list / GPU present / flip / M7 / b15 / VCN); a safe attempt that
        hangs leaves the next boot headless. Mock: 208/208 (8 new; the old daemon fails 4 of them: reboot issued).
        Deployed to `~/gpu/phxboot-navi31.sh` (backup `~/vermilion-rollback/phxboot-navi31.sh.pre-nvstuck`).
  - [ ] Freeze cause. Leading hypothesis: GPU present restarted at 1280x1024 (Linux's pitch = the width there, so the
        plane check passes; the b138 note "below 1440p GPU present stays off" is wrong for 1280x1024 and 1024x768),
        on the small slot layout (VRAM 24/32/40 MiB), never used before on the XTX. Next b138 attempt: keep GPU present
        to the boot size (plugin), and log the kext's scan acquire to a file the next boot can read
- [x] 2026-11-20 **Every LG mode from System Settings, the desktop resizes** (owner, 20:05-20:20, b138 kext + b139
      plugin, GPU present off for the session: CPU copy): 1080p at 60 / 120, 2560x1440 at 60 / 120 / 144 / 240,
      1280x1024@75, 1024x768@60, 800x600@60, 640x480 at 59.94 / 60. No freeze. The boot itself went black: macOS applied
      the saved 1280x1024 at login, the plugin kept the 1440p plane, the CPU copy skipped every frame as foreign;
      `debug.phxscan=0` gave the plane back (fix: b139 kernel)
- [x] 2026-11-20 **Adaptive Sync** (done, see "Display polish" below; this is the log of its steps). **Step 1: the macOS side mapped, PhxAccel probe built** ([findings](findings/2026-11-20-adaptive-sync-research.md)).
      Apple's AMD driver: `IOFBTimingRange` with sync flag 0x10, the range per mode in `verticalBlankingExtension`, AGDC
      command 0x979 (VRR capability), an Adaptive Sync SDP. Probe (PhxAccel, flag `~/gpu/phxboot-vrrprobe`, off by
      default): publishes the range (48-240 Hz from the EDID) with the flag, answers 0x979, logs CoreDisplay's timing and
      `'vrr?'` calls; no hardware change. Host: head0_1440 41/41, mock 211/211. First install (21:14) booted 21:53
      without its sysctl: PhxAMDFB registers its sysctls at its own start (the claim), after the daemon's write; now
      registered at kext load (_start). Reinstalled 21:58 (UUID 77557B85), flag `~/gpu/phxboot-vrrprobe` set.
      **Rollback:** rm ~/gpu/phxboot-vrrprobe (the probe is then inert); PhxAccel copies in `~/vermilion-rollback/`.
      b140 verified 21:54: the login screen switched to 240 at 21:54:18, 4 s before it was shown.
      Probe boot 1 (22:00): range published, macOS did nothing with it. Step 2 installed 22:30: the 1440p modes also
      carry the extension to 48 Hz (findings). 22:12 on the box: System Settings shows "Variable (48-240 Hz)" only, picked
      it; OTG0 stays at a fixed 240. Step 3 installed 22:40: the fixed rates without the extension plus a variable twin of
      240 (id 13, `kStdVrrTwin`). **Test:** restart: Variable next to 60 / 120 / 144 / 240. **Next:** the hardware (DMUB
      `FAMS_DRR_UPDATE`, findings), after a read-only check of DMUB's FAMS state on macOS. 22:50 on the box: Variable next to the four fixed
      rates (owner). b141 installed 23:05 (EFI `OC` = b141 sha 76482ab3, `OC.prev` = b140; b139 kext in
      `~/vermilion-rollback/`): `sudo sysctl -w debug.phxdmub_caps=1; sysctl debug.phxdmub_caps_stat` after the restart
      (one allow-listed ping). **Rollback:** swap `EFI/OC` and `EFI/OC.prev`. 22:38 on the box: rc 0 in 10 us, every
      feature-caps byte 0 (no fw-assisted mclk switching: Linux would take the OTG register path). b142 installed 23:25
      (`OC` = b142, `OC.prev` = b141; b140 kext in `~/vermilion-rollback/`): `debug.phxvrr` 1 / 0, `debug.phxvrr_stat`.
      **Test:** at 2560x1440 240, `sudo sysctl -w debug.phxvrr=1`: the OSD rate follows what is drawn; `=0` undoes it.
      22:50 on the box: the range works (OTG0 48 Hz, OSD 48, picture normal), 0 triggers: GPU present was off since the
      boot (plane 1280x1440 under ODM 2:1, a b138 bug; at 240 the CPU copy has drawn since b140). b143 installed 23:35
      (`OC` = b143, `OC.prev` = b142; b141 kext in `~/vermilion-rollback/`): the plane is twice HUBP0's viewport under ODM.
      22:56 on the box: **VRR works** (debug.phxvrr=1: OTG0 52-188 Hz with the content, the owner sees it). b144 + PhxAccel
      installed 23:50 (`OC` = b144, `OC.prev` = b143; b142 kext and the PhxAccel before in `~/vermilion-rollback/`):
      System Settings' Variable turns the range on (phxVrrSet), a fixed rate off; on only while GPU present scans.
      **Test:** restart; pick Variable, move windows (OSD follows), pick 144 (fixed 144), Variable again.
      **23:08 on the box: works** (owner: "Works amazingly"; log: Variable -> range on, 1080p120 / 1440p120 -> off, Variable
      -> on, 6057 triggers). Left: Variable for the other resolutions; the probe flag becomes the feature (rename,
      docs); the CPU copy has no trigger (Variable needs GPU present: 1440p only today).
      2026-11-21 00:10 cleanup: the probe is the feature (`debug.phxfb0_vrr`, default 1; daemon opt-out
      `~/gpu/phxboot-novrr`, off on the safe desktop); probe diagnostics removed; docs/XTX.md updated. Installed, not
      booted (behaviour unchanged)
- [x] 2026-11-21 **HDR** (done, see "Display polish" below; this is the log of its steps). **Step 1: Linux's HDR state read, a report-only PhxAccel probe** ([findings](findings/2026-11-21-hdr.md)).
      Linux at every rate: ARGB2101010 scan-out, FMT 10 bpc, DP_PIXEL_FORMAT 0x42, MSA colorimetry, VSC SDP 0x206, the
      HDR mastering infoframe in generic packet 3, identity gamut remap; no regamma table. Probe installed 00:40
      (`~/gpu/phxboot-hdrprobe`): the timing range reports HDR10 / BT.2020 / 10 bpc, CoreDisplay's calls logged.
      **Test:** restart; is "High Dynamic Range" offered in System Settings > Displays. **Rollback:** rm
      ~/gpu/phxboot-hdrprobe; PhxAccel copies in `~/vermilion-rollback/`. Probes on the box: steps 1-2 no switch (30-bit depth taken),
      **step 3 (HDR10 twins 14-17): System Settings offers High Dynamic Range** (on = mode 17, off = mode 6); step 4: no
      0x713 packet. Hardware next: HDR twins as replay modes from Linux's HDR mode sets. Capture v3 has no HDR -> SDR
      (findings): **needs capture v4** in Omarchy (`--hdr-pairs`, tool on the ESP). Capture v4 done 2026-11-21 23:40 (`display-20261121-234031`,
      every from-hdr back at 8 bpc). **b145 + PhxAccel installed 2026-11-22 00:50** (`OC` = b145, `OC.prev` = b144; b143
      kext and the PhxAccel before in `~/vermilion-rollback/`): the HDR twins switch to HDR replay modes (10 bpc, BT.2020,
      PQ infoframe; 8-bit surface). **Test:** restart, HDR on in System Settings: the LG's OSD shows HDR; HDR off: SDR again.
      **Rollback:** HDR off in System Settings; boot trouble: swap `EFI/OC` and `EFI/OC.prev`, rm ~/gpu/phxboot-hdrprobe.
      **2026-11-22 00:38 on the box: HDR works** (owner; log: HDR on = key 1440240 DONE, ODM + DSC + FEC; off 00:49 = 240
      DONE; 3 runs, 0 fall-backs; after it DP_PIXEL_FORMAT 0x41000000, FMT 0xc900, DP_SEC_CNTL 0x00501111). Left: HDR becomes
      a feature (done 01:00: `debug.phxfb0_hdr` default 1, needs the monitor and PhxBringup b145+; daemon opt-out
      `~/gpu/phxboot-nohdr`, off on the safe desktop; the probe flag removed; installed, not booted), 10-bit drawing (the
      surface is 8-bit: banding), Variable + HDR together.
      01:35 on the box: HDR offered without the flag, and **HDR stayed on across the restart** (mode 17 after boot).
- [x] 2026-11-22 **Display polish at 2560x1440: done on the XTX machine** (owner). From System Settings > Displays:
      every rate to 240; Variable (60-240 Hz) with the ramp; High Dynamic Range drawn in 10 bits; Variable with HDR on.
      How it works: [docs/XTX.md](../docs/XTX.md); the install and test log of each step:
      [findings](findings/2026-11-22-display-polish-log.md); reasons: [decisions.md](decisions.md) (2026-11-20 and
      2026-11-22).
  - **Installed** (2026-11-22; the clean-up pass after it changed comments and docs only, all three binaries rebuild
    byte for byte, nothing was reinstalled):
    - VermilionBoot b150 on the EFI partition (`EFI/OC`, sha256 3806555e...; `EFI/OC.prev` = b149, d34093c0...);
    - VermilionAccel with mode id 18 in `/Library/Extensions` (sha256 c6341e87...);
    - the Metal plugin of b149 (sha256 7f46d89b...);
    - the daemon with `phxboot-novrr` / `-nohdr` / `-vrrmin` in `~/gpu`; `~/gpu/phxboot-vrrmin` holds 60.
  - **Knobs:** `~/gpu/phxboot-vrrmin` (the floor in Hz), `~/gpu/phxboot-novrr`, `~/gpu/phxboot-nohdr`; live as root
    `debug.phxvrr_ramp` (default 5) and `debug.phxvrr_floor`; state in `debug.phxvrr_stat`, `debug.phxp0odm_stat`,
    `debug.phxfb0_scan10`. Table: docs/XTX.md, "Display settings".
  - **Rollback** (copies in `~/vermilion-rollback/`; one EFI change per boot):
    - a feature misbehaves: `touch ~/gpu/phxboot-novrr` or `~/gpu/phxboot-nohdr` and restart; Variable alone: pick a
      fixed rate (the range and the double buffer go off with it);
    - boot trouble: `sudo diskutil mount disk0s1`, then in `/Volumes/NO NAME/EFI`: `mv OC OC.bad && mv OC.prev OC` (or
      the same from Linux); older kexts: `VermilionBoot.kext.b134` .. `.b149`;
    - PhxAccel: `sudo rm -rf /Library/Extensions/VermilionAccel.kext && sudo cp -R
      ~/vermilion-rollback/VermilionAccel.kext.pre-hdrvrr /Library/Extensions/VermilionAccel.kext`, `sudo chown -R
      root:wheel` it, `sudo kmutil load -p /Library/Extensions/VermilionAccel.kext`, **Allow** in Privacy & Security,
      restart (without the Allow click the AuxKC is not rebuilt and the old PhxAccel keeps loading);
    - the plugin: `sudo cp ~/vermilion-rollback/NVMTLDriver.b139
      /Library/GPUBundles/NVMTLDriver.bundle/Contents/MacOS/NVMTLDriver`, restart.
  - **Open:**
    - [ ] Some flicker is left in Variable (owner: "way less" with ramp 5). It is the panel's: the same monitor does
          it on Windows and Linux. Nothing planned.
    - [ ] A floor change resets macOS's display identity (colour profile, per-display settings), because the floor
          is in the EDID PhxAccel serves. Either keep the label patch only when it does not change the identity, or
          leave it documented (docs/XTX.md does). The owner's two profiles are set with `~/gpu/tools/colorprofile.swift`
          (outside the repo).
    - [ ] b150's own checks were not written down after the restart: over a long Variable session no white flash,
          `debug.phxvrr_stat` shows `dbuf now 0x02000000` while Variable is on and HUBP0's bits 30:28 = 0.
    - [ ] 10-bit HDR: a dark gradient without banding, by eye (not written down).
    - [ ] DRR double buffer for the whole boot, as Linux has it, instead of only while the range is on
          (decisions.md 2026-11-22, "Revisit").
    - [-] VRR / HDR below 1440p: dropped by the owner 2026-11-22 (decisions.md). What it would need: a per-flip
          trigger from the CPU copy, per-size VRR twins, and a Linux capture per HDR mode (the log has the plan).
- [x] 2026-11-20 **b140: the login screen gets its saved rate** (verified 21:54) (installed 20:55, EFI `OC` = b140 sha 8b5ead5b, `OC.prev`
      = b139; b138 kext in `~/vermilion-rollback/`). b139's login screen ran at the firmware's 60 Hz: its 240 Hz request
      (20:39:39) was refused "not ready" because the scanout lock only existed after GPU present's first acquire (20:39:46).
      **Test:** restart; the login screen at 240 (OSD). **Rollback:** swap `EFI/OC` and `EFI/OC.prev`
- [x] 2026-11-20 **b139 works on the box** (owner 20:45, "everything feels smooth"; log: 1080p120 gave the plane back
      (restore verified, 16645 presents 0 dropped), back at 1440p GPU present ON in 0.14 s, 120 -> 240 kept it). Was: **b139 installed** (EFI `OC` = b139 sha 50c846bf, `OC.prev` = b138; b137 kext in
      `~/vermilion-rollback/VermilionBoot.kext.b137`): before a size change the kernel gives the plane back from GPU
      present. **Test:** after the restart GPU present on at 2560x1440 (smooth); switch to 1080p and back, 1280x1024 and
      back, a rate change at 1440p (GPU present stays on). **Rollback:** black screen: from T3, `sudo sysctl -w
      debug.phxscan=0`; boot trouble: OpenCore > OC.prev is not offered by the picker, so from macOS / Linux swap
      `EFI/OC` and `EFI/OC.prev`
- [x] 2026-11-20 **b138 retry with the b139 plugin: GPU present only at its first size** (the suspected freeze path off;
      every other resolution on the CPU copy). Plugin built (sha 851310a0; a HEAD build = the installed 4296d4eb), EFI
      back to b138 (`OC.bad` -> `OC`, b137 -> `OC.prev`). `phxfb-squat` set from Linux (stays set), kill switch off.
      **Test:** 1920x1080 first (always CPU copy), then 1280x1024, 1024x768, 800x600, 640x480, back to 2560x1440 144/240.
      **Rollback:** a freeze: hold power, power on, macOS: the daemon reboots once, then the safe desktop comes up by
      itself (stuck squat). Then: EFI `OC` -> `OC.bad`, `OC.prev` (b137) -> `OC`; plugin back from
      `~/vermilion-rollback/NVMTLDriver.b138` (or `NVMTLDriver.pre-b138`); rm ~/gpu/phxboot-off ~/gpu/phxboot-failed
- [~] 2026-11-20 **b138: the other resolutions from System Settings, the desktop resizes** (built, host-tested; booted
      16:37, froze at 1280x1024, see above; EFI back on b137):
      VermilionBoot b138 (Linux's HUBP0 pitch per mode, phxPipe0SetMode takes keys, phx-p0-keys), PhxAccel b138 (head 0
      offers the EDID's established / standard / VIC modes on the 2560x1440 surface, per-mode geometry with Linux's
      256-pixel pitch, flip B placed by the boot size), Metal plugin (a size change restarts GPU present instead of
      ending it; below 1440p GPU present stays off: CPU copy). Rollback copies: `~/vermilion-rollback/` (PhxAccel
      `VermilionAccel.kext.b137-*`, plugin `NVMTLDriver.pre-b138`), EFI OC.prev = b137.
      **Test:** System Settings > Displays > the resolution list (or "Show all resolutions"): 1920x1080, then 1280x1024,
      1024x768, 800x600, 640x480, then back to 2560x1440 at 144 / 240. **If the desktop breaks at login (macOS
      re-applies a saved small mode):** boot OC.prev (b137: no phx-p0-keys, PhxAccel then lists only the 1440p rates and
      macOS falls back), then roll back PhxAccel / the plugin from the copies
- [x] 2026-11-20 **b137: the other EDID resolutions, kext side: every mode works on the box** (16:22-16:30, owner read
      the OSD: 1920x1080@60/120, 1280x1024@75, 1024x768@60, 800x600@60, 640x480@60/59.94, then back to 1440p144; 9 runs 9
      done, 0 fall-backs, 0 soft-wait timeouts, training ok at RBR x1/x2/x4 and HBR x4). Installed 16:15, OC.prev = b136. 1920x1080@60/120, 1280x1024@75, 1024x768@60, 800x600@60, 640x480@60/59.94 as Linux's
      switches through the 144 hub; `debug.phxp0odm` only (PhxAccel unchanged), so macOS keeps drawing 2560x1440 and the
      monitor should show the desktop's top-left corner at the real mode. **Test after the boot:** System Settings 144 (or
      240), then in Terminal `sudo sysctl -w debug.phxp0odm=1080060` (OSD 1920x1080 60 Hz, top-left of the desktop), then
      `=1080120`, `=1024075`, `=768060`, `=600060`, `=480060`, `=480059`, and back with `=144` (or System Settings).
      After each: `sysctl -n debug.phxp0odm_stat | head -2`. **Rollback:** OC.prev (b136). Next: PhxAccel resizes the
      desktop (per-mode geometry, pitch = mode width, the Metal plugin restarts GPU present at the new size)
- [x] 2026-11-20 **b136 = b135 + the DET / compressed-buffer waits: works on the box** (15:50, owner, System Settings: 240
      at login, then 60, 120, 144, 240, 60, 240 all landed; stat 7 runs 7 done, 0 fall-backs, 0 soft-wait timeouts, longest
      wait 7.9 ms). Installed 13:55, OC.prev = b135, b134's kext in
      `~/vermilion-rollback/VermilionBoot.kext.b134`). b135 on the box: 240 DONE, 240 -> 60 dark (DET1 never released,
      COMPBUF CONFIG_ERROR, DET0 0 segments; the model now reproduces it). Same test and rollback as b135 below; the stat has
      a new `det:` line (swait_timeouts should be 0)
- [~] 2026-11-20 **b135: every 1440p rate through one replay engine** (installed 13:40; 240 worked, 240 -> 60 dark).
      VermilionBoot sha256 `1c791a4e...`; PhxAccel and the daemon unchanged (PhxAccel b134 stays). Linux's own switches
      144 -> 60 / 120 / 240 and back (capture v3); from macOS's state only the entry to 240 (b133's plan), after that every
      rate as cur -> 144 -> target. Fall-back to 144 is not latched; a refusal before any write is not latched; the last
      failure stays in `debug.phxp0odm_stat` (`last_fail:` with the DAL response). DAL busy is retried (~2 s).
      **Test after the boot:** System Settings > Displays: 240, then 60, 120, 144, 240, 60 (each should land; the first
      240 takes ~2 s). After each: `sysctl -n debug.phxp0odm_stat | head -3`. A refusal or fall-back: keep that output.
      **Rollback:** a bad boot = OpenCore `OC.prev` (b134; from the Mac `sudo mv <ESP>/EFI/OC <ESP>/EFI/OC.bad && sudo mv
      <ESP>/EFI/OC.prev <ESP>/EFI/OC`), or `-phxbringupoff` for one boot. A bad switch: pick 144 or reboot (nothing is
      written at boot; the engine runs only when a rate is picked)
- [x] 2026-11-20 **240 Hz in System Settings: VermilionBoot b134 + PhxAccel head-0 row 6** (installed; 240 worked once
      from System Settings; then 60 / 120 refused after 240 and a DAL refusal latched the engine: fixed in b135).
      PhxAccel lists 2560x1440 @ 239.96 Hz when the full EDID (phxEdidReadFull) has the LG's DisplayID timing
- [~] 2026-11-20 Automatic login (owner): the daemon owns loginwindow's autoLoginUser (deleted at daemon start / shutdown /
      revert to GOP, written right before the Metal WindowServer restart; flag `~/gpu/phxboot-autologin` = user name,
      password in /etc/kcpassword from `sysadminctl -autologin set`). Plain macOS autologin would log in before the arm,
      which refuses with a user logged in. Mock 200/159. Installed 2026-11-20 08:18 (daemon `.prev` kept). Not yet seen on a
      boot. **Rollback:** `rm ~/gpu/phxboot-autologin` (password login again); remove the password store with
      `sudo sysadminctl -autologin off`
- [ ] 240 Hz (Phase 3): research the Linux DC mode-set + DP link training sequence first, plan before any write
  - [x] 2026-11-19 Read-only survey on macOS ([findings](findings/2026-11-19-refresh-and-hdr.md)): GOP link on DP B is
        **HBR2 x4 at 12 bpc**; 120 Hz (10 bpc) and 144 Hz (8 bpc) fit it without retraining, 240 Hz needs HBR3 (or
        DSC at 10 bpc). Head 0's mode table has no 1440p rows; DMCUB is `0x07008748` (gate wants `0x07008742`).
        HDR: monitor has PQ/BT.2020, the link has the bits at 60 Hz, Vermilion reports 8-bit only and sends no SDP
  - [x] 2026-11-19 Step 3 (read-only): Apple's AMD framebuffer offers HDR through `getTimingRange`
        (dynamic range SDR|HDR10, 8/10/12 bpc, BT.2020), `'hdrm'`, `'dsdp'` (HDR infoframe packet) and AGDC `0x711`;
        Vermilion has none of them ([findings](findings/2026-11-19-refresh-and-hdr.md))
  - [x] 2026-11-20 Step 1 verified on hardware (b128 includes it): full EDID read over AUX2, 3 blocks; block 2 = the 240 Hz
        timing (1056.75 MHz, 2720x1619). Old notes:
        VermilionBoot **b127** = b126 + `debug.phxedid_full` (whole EDID via the E-DDC segment pointer). Built
        on the Mac (sha256 `4e1c708e...`), host test 27/27, mock suites 189/159 green. **Not staged, not installed:
        needs a boot slot from the owner** (the EFI belongs to the "post-install" thread, the next restart to "GPU
        heat"). Install = the runbook's `efi/build.py build/install --stage vermilion` with this kext (keeps `OC.prev`).
        After boot: `sudo sysctl -w debug.phxedid_src=0x22; sudo sysctl -b debug.phxedid_full > edid.bin`.
        **Rollback:** nothing runs unless root reads the sysctl; a bad boot = OpenCore `OC.prev` (from the Mac:
        `sudo mv <ESP>/EFI/OC <ESP>/EFI/OC.bad && sudo mv <ESP>/EFI/OC.prev <ESP>/EFI/OC`), or `-phxbringupoff` for one boot
  - [x] 2026-11-20 Step 2 done: Linux capture read ([findings](findings/2026-11-19-refresh-and-hdr.md), "Capture read on macOS")
  - [x] 2026-11-20 **b128 works on hardware: 60 -> 120 -> 144 -> 60 from a shell, OSD 120/144 Hz, picture normal** ([findings](findings/2026-11-19-refresh-and-hdr.md)). Details:
        **b128 = b127 + 1440p 60/120/144 Hz on pipe 0** (plans from the capture, DET 4/0/0/0 step, DAL DISPCLK/DPPCLK hard
        mins 505/614 MHz with the owner's OK, depth 12 -> 10/8 bpc). Host tests: pipe0_1440 45/45 (4 mutations caught),
        edid_full 27/27, scan_layout 43/43; mock 189/159. Built on the Mac (VermilionBoot sha256 `bf4c3edd...`). EFI built
        from this branch: only `Kexts/VermilionBoot.kext/Contents/MacOS/VermilionBoot` differs from the installed EFI.
        **Installed 2026-11-20 with `efi/build.py install --stage vermilion`; `OC.prev` = the EFI of 2026-11-19 09:57 (b126,
        booted many times).** Nothing new runs at boot (the pipe-0 engine stays off until root sets `debug.phxpipe0_enable=1`).
        **Test after the boot (shell, no PhxAccel change yet; macOS still thinks 60 Hz):**
        `sudo sysctl debug.phxpipe0_enable=1 debug.phxpipe0_mode=120 debug.phxpipe0=4`, then `sysctl -n debug.phxpipe0_stat`
        (`last=done`, `frames=` ~120), the monitor's OSD shows 120 Hz; back: `sudo sysctl debug.phxpipe0_mode=60 debug.phxpipe0=4`.
        Then 144 the same way. A failed set falls back to 60 by itself; dark = reboot (the GOP relights 1440p60).
        **Rollback:** bad boot (no desktop, panic): power off, power on, OpenCore picker > (Space) > the previous entries are
        not separate, so: boot with `-phxbringupoff` once, or from Linux: `sudo mv /boot/EFI/OC /boot/EFI/OC.bad &&
        sudo mv /boot/EFI/OC.prev /boot/EFI/OC`; from macOS: `sudo mv <ESP>/EFI/OC <ESP>/EFI/OC.bad && sudo mv <ESP>/EFI/OC.prev <ESP>/EFI/OC`
  - [x] 2026-11-20 **120/144 Hz in System Settings works** (owner picked 144: OSD 144, less lag and tearing; `phxpipe0_stat`
        mode=144 done). Not tested: the rate after a reboot, sleep/wake, hours at 144. Install notes:
        **120/144 Hz in System Settings** (one change, one switch: the flag): PhxAccel with the LG's 1440p rows in
        head 0's mode table (ids 3-5, the EDID's DTDs; `target_hz` rounded, 59.951 -> 60; host test head0_1440 15/15; the
        installed PhxAccel was byte-identical to this branch without the change) + daemon flag `~/gpu/phxboot-p0modes`
        (`debug.phxpipe0_enable=1 debug.phxpipe0_modes=1` before the claim; mock 193/193). Installed 2026-11-20 from the
        Mac: rollback copy `~/vermilion-rollback/VermilionAccel.kext.20261120`, daemon `.prev` in `~/gpu`, `kmutil load -p`
        (AuxKC rebuild, applies at the next boot). After the boot: System Settings > Displays > Refresh Rate lists 60/120/144.
        **Rollback:** `rm ~/gpu/phxboot-p0modes` (or from Linux `touch /boot/vermilion/flags/phxboot-p0modes.rm`): one mode
        again at the next boot. Full PhxAccel rollback: `sudo rm -rf /Library/Extensions/VermilionAccel.kext && sudo cp -R
        ~/vermilion-rollback/VermilionAccel.kext.20261120 /Library/Extensions/VermilionAccel.kext && sudo kmutil load -p
        /Library/Extensions/VermilionAccel.kext`, reboot. Bad boot: `-phxacceloff` once
  - [x] 2026-11-20 240 Hz research, read-only ([findings](findings/2026-11-19-refresh-and-hdr.md), "240 Hz research"):
        Linux = HBR3 + FEC + DSC + ODM 2:1; PHY and AUX go through DMUB (payloads not in the register trace); 8 bpc fits
        HBR3 without FEC/DSC (inferred); HBR3 only in the extended DPCD caps (0x2201); 10 bpc at 240 needs DSC
  - [ ] **240 Hz HDR plan** (owner goal 2026-11-20; order, one boot per kext change):
    - [x] 2026-11-20 A done (Linux, `455afdd`): HBR3 TRANSMITTER_CONTROL = the trainer's own shape with symclk 81000;
          AUX via registers; 240 only ever lit with FEC + DSC + ODM 2:1. Plan: [240hz-plan](findings/2026-11-20-240hz-plan.md)
    - [ ] A. (done) Linux capture v2 (`--r240 --hdr`, tool now records DMUB commands): the HBR3 TRANSMITTER_CONTROL / AUX bytes;
          240 at 10 bpc and HDR; `240-nodsc` (does Linux light 240 on one pipe without DSC?). Read-only; Linux session
    - [ ] B. HDR at 60-144 Hz (no link change): 7a probe, then 10-bit scan-out + 10 bpc output + HDR infoframe + PQ path
    - [ ] C. DMUB on this card (asks the owner first): does DMCUB `0x07008748` answer Linux's QUERY_FEATURE_CAPS; gate
          gets `0x07008748` next to `0x07008742` (rev 0xC8 only)
    - [x] 2026-11-20 **D1 / D2 work on hardware (b130): DP B retrained HBR2 -> HBR3 -> HBR2, picture normal each time**
          ([240hz-plan](findings/2026-11-20-240hz-plan.md)). Install notes:
          **D1 / D2 = VermilionBoot b129** (owner OK for the DMUB ping + TRANSMITTER_CONTROL): `debug.phxdpb_retrain`
          1 survey, 2 retrain DP B at HBR2 x4, 3 at HBR3 x4 (falls back to HBR2). Inert until root writes it. Host test
          dpb_retrain 67/67; mock 193/159. Built on the Mac (VermilionBoot sha256 `5b61b345...`). Install with
          `efi/build.py install --stage vermilion` (OC.prev = the b128 EFI, booted 2026-11-20). **Installed 2026-11-20 07:16**
          from the Mac (only VermilionBoot differed; OC sha `5b61b345...`, OC.prev `bf4c3edd...` = b128).
          **Test after the boot** (desktop at 1440p, any rate; one command at a time):
          `sudo sysctl debug.phxdpb_retrain=1; sysctl -n debug.phxdpb_retrain_stat` (survey: `result=surveyed`, nothing
          written), then `sudo sysctl debug.phxdpb_retrain=2` (screen dark ~1 s, then the same picture; stat `result=PASS`),
          then `=3` (HBR3; the monitor's OSD may show the link rate), then `=2` again (back to HBR2: the 120/144 switch
          needs HBR2, its M check refuses at HBR3).
          **Rollback:** dark after a retrain = power off / on (the GOP trains HBR2 again; nothing persists). Bad boot: OpenCore
          `OC.prev` (from the Mac: `sudo mv <ESP>/EFI/OC <ESP>/EFI/OC.bad && sudo mv <ESP>/EFI/OC.prev <ESP>/EFI/OC`), or
          `-phxbringupoff` for one boot. The 120/144 flag and PhxAccel are unchanged
    - [~] 2026-11-20 07:20 b129 booted fine (macOS restored 144 Hz by itself at login). Survey refused with zero writes:
          DP1_DP_STEER_FIFO 0x3f000100 (sticky TU_OVERFLOW_FLAG; the gate compared the whole register). **b130** masks the
          status bits (dpb_retrain 69/69; VermilionBoot sha256 `0f2ceee9...`). Same test and rollback as b129 (OC.prev = b129)
    - [ ] D. 240 Hz SDR: retrain DP B to HBR3 (TRANSMITTER_CONTROL bytes from A, TPS4, extended caps), 2-pixel-per-cycle
          plan, head-0 row; fall back to HBR2 + the 60 Hz plan on any failure
    - [~] 2026-11-20 E1 research: Linux's 144 -> 240 mode set decoded (incl. AUX / DPCD); one pipe is a blank interim only,
          the picture runs on ODM 2:1 + DSC x2 + FEC. Build plan (generator, engine, flip on both HUBPs, head-0 row 240) in
          [240hz-plan](findings/2026-11-20-240hz-plan.md)
    - [x] 2026-11-20 08:08 **240 Hz works on hardware (b133): OSD 240 Hz, both halves correct** ([240hz-plan](findings/2026-11-20-240hz-plan.md)).
          b131 / b132 tries: a FIFO wait Linux never ran, then OTG0's pixel-rate divider (both fixed). Next: 240 in System
          Settings (head-0 row + PhxAccel -> debug.phxp0odm path), the way back on hardware, stability, then HDR. Notes:
          **E1 = VermilionBoot b131** (`debug.phxp0odm`: 1 survey, 2 = 240 Hz, 3 = back to 144; inert until root
          writes it; not in System Settings yet). Host test p0odm 1203/1203. VermilionBoot sha256 `f462415c...`.
          **Test after the boot:** survey, then 2 (screen dark ~0.2 s, then 240 Hz: monitor OSD), then 3 (back to 144).
          After 2 or 3 the 120/144 switch in System Settings refuses until reboot (pipe 0 is on Linux's 2 ppc setup).
          **Rollback:** dark or garbled after 2 / 3 = power off / on (nothing persists; the GOP relights 60 Hz). Bad boot:
          `OC.prev` (= b130) or `-phxbringupoff`
    - [ ] E. 240 Hz HDR: FEC + DSC (+ ODM 2:1 as Linux) at 10 bpc
  - [ ] Step 7a (proposal): report-only HDR probe in PhxAccel behind a boot-arg (timing range + 10 bpc depths, log
        `'hdrm'`/`'dsdp'`), to see whether System Settings offers HDR before any hardware work
  - [~] Step 2: Linux capture. Tool `tools/capture/linux_display_capture.py` (test
        `tests/mock/linux_display_capture_test.py` 12/12), handoff [HANDOFF-LINUX-DISPLAY-CAPTURE.md](HANDOFF-LINUX-DISPLAY-CAPTURE.md),
        copy on the EFI partition `vermilion/capture-tool/`. 2026-11-20: owner boots Omarchy; output goes to
        `<ESP>/vermilion/capture/display-<date>/`. Back to macOS = full power-off

## Earlier (2026-11-19, 10:05): Wi-Fi works; next Bluetooth

**Wi-Fi works on macOS** (MtkWirelessWifi 1.1.1 + boot-arg `pci=0x8000000`, [findings](findings/2026-11-19-wifi-mt7922.md)).
The cause was macOS setting 10-Bit Tag Requester Enable on the chipset root port, which the MT7922 and the I225-V
can't complete. EFI = `vermilion` stage (inherits `base`), built and installed from the Mac; `OC.prev` = the same
without `pci=0x8000000`. Next: Bluetooth (USB `0489:e0e2`), then Ethernet (its capability list is back).

## Earlier (09:00): on the Mac; Wi-Fi blocked by an empty PCI capability list

**Next: one Linux capture** ([HANDOFF-LINUX-CAPTURE.md](HANDOFF-LINUX-CAPTURE.md), for the Claude on Omarchy): the
owner boots Linux once, `tools/capture/linux_pci_capture.sh` writes `/boot/vermilion/capture/linux-pci-*.txt`,
then back to macOS, which reads it from the EFI partition. EFI for that macOS boot: `vermilion` stage (installed
2026-11-19 09:16 from the Mac), `OC.prev` = `vermilion-pcilog`.

Wi-Fi: both MediaTek drivers fail because macOS gives the card (and the I225-V) an empty PCI capability list
([findings](findings/2026-11-19-wifi-mt7922.md), boot 2). Boot 3 (`vermilion-pcilog`, logging only) is installed.
EFI builds now work on the Mac (`iasl` 20251212 in `~/.local/bin`, config in `~/.config/vermilion`).

## Earlier: work moves to macOS itself

**From here on the owner works in macOS on the XTX machine** (Claude Code + T3 Code installed there over Wi-Fi), not
from Linux. If you are that macOS session, start here:

1. **Wi-Fi boot 1 failed** (AirportMtkWlan loaded, never attached; [findings](findings/2026-11-19-wifi-mt7922.md)).
   **Boot 2** (staged 07:15): `MtkWirelessWifi` 1.1.1 instead, `run-once.sh` = `wifilog.sh` with deep snapshots to
   `/var/log/vermilion-wifi-*.log` and `*.log.ioreg`. Boot 1's `run-once.sh` turned **sleep off** (`pmset -a sleep 0 displaysleep 0
   disksleep 0 hibernatemode 0 standby 0 powernap 0 tcpkeepalive 0 womp 0`; result lines in `/var/log/phxboot.log`
   under `run-once:`; it stays off). On the Mac itself, `ioreg -l -w0 -r -n DP58` answers directly whether a driver
   attached to the card.
2. **The EFI partition from macOS:** `diskutil list` (the NVMe's EFI partition, under Recovery it was `disk2s1`),
   `sudo mkdir -p /tmp/efi && sudo diskutil mount -mountPoint /tmp/efi <diskXs1>`. On it: `EFI/OC` (live),
   `EFI/OC.prev` (rollback), `opencore-*.txt` (OpenCore log of every boot), `vermilion/` (payload, flags, run-once).
3. **EFI builds on the Mac** need what Linux kept outside git, now in `<EFI>/vermilion/handoff/config-vermilion/`:
   copy it to `~/.config/vermilion/` (mode 0700; the SMBIOS serials and the ACPI dump, which includes `VFCT.aml` =
   the VBIOS: **never commit or publish any of it**). `build/VermilionBoot.kext` (b126) = `<EFI>/EFI/OC/Kexts/
   VermilionBoot.kext`. `build.py` needs `python3` and `iasl` (Command Line Tools; acpica). Install with
   `sudo efi/build.py install --esp /tmp/efi --stage vermilion`; one EFI change per boot still.
4. **NVRAM:** `DisableVariableWrite` is on, so macOS can't change NVRAM or the startup disk. The default OpenCore
   entry is set in the picker (Ctrl+Enter on Macintosh HD, 2026-11-19, unverified that it sticks).
5. **Boot path:** firmware boot order is OpenCore first (`0006,0000,...`), so power-on boots macOS after the picker's
   10 s. A Linux **reboot** goes back to Linux (`tools/linux/vermilion-reboot-to-linux.service` sets BootNext=Limine
   on reboot only; tested by a real run and cleared, the reboot path itself unverified). Linux -> macOS is still a full
   power-off. macOS restarts are fine. Linux is still reachable with F8 > Limine.
6. **Next, in order:** Wi-Fi verified -> Bluetooth (`0489:e0e2`) -> Ethernet -> USB map -> audio -> AirDrop/Continuity
   -> sleep/wake (only with the driver off, `-phxbringupoff`, one boot, after asking the owner). Graphics stays paused.

The lines below predate this and describe the machine from Linux.

## Handoff (2026-11-19, late night)

**Graphics is paused here; the next thread works on everything else (Phase 1 rest: sleep, USB, Ethernet, Wi-Fi,
Bluetooth, AirDrop, audio), from a Claude Code / T3 Code install on the Mac itself.** Graphics state to come back to:
GPU present at 1440p60 works (b126; no CPU copy, median 40 frames/s) but flickers, shows occasional artifacts and the
CPU runs hotter ([findings](findings/2026-11-19-1440p-gpu-present.md), boot 2). Then 144/240 Hz (Phase 3).

Working on the Mac itself: the hard rules still hold (cold power-off from Linux, one EFI change per boot with a
rollback copy, no SMBIOS/serials in git). **Sleep:** Vermilion has no resume path (Phase 4); a real S3 sleep with the
driver up will most likely lose the GPU (unverified), so keep `pmset` sleep off and test ACPI sleep/wake only on a boot
with the driver off (`-phxbringupoff`, one boot). Builds on the Mac need the build-host setup of
[runbooks.md](runbooks.md) (Command Line Tools, MacKernelSDK, ...); installing a kext on the live system still goes
through the EFI (VermilionBoot) or the AuxKC (VermilionAccel), one change per boot.


- **The accelerated desktop runs on the XTX** (Vermilion, Metal compositor, one display at 2560x1440) on macOS 26.7.1
  bare metal. Every OpenCore boot is a squat boot and arms the desktop by itself
  ([findings](findings/2026-11-19-first-vermilion-boot.md)). The GPU bring-up needed no XTX change.
- **Boot routine:** Linux via Limine (default). macOS: full power-off, F8 > OpenCore > Macintosh HD. **No Reset
  NVRAM** any more: it deletes `phxfb-squat` (then the next boot is a non-squat boot with a phantom "UNKNOWN" IONDRV
  display). Reset NVRAM is the escape hatch back to the plain firmware display. A failed desktop attempt reboots, and
  the default entry is Limine, so it lands in Linux. Warm macOS restarts are fine on this card.
- **Machine state:**
  - EFI: `vermilion` stage with **VermilionBoot b126** on `/boot/EFI/OC` (2026-11-19 05:30; `OC.prev` = the same stage
    with b125, the last booted one; the `install` stage is backed up in `~/.cache/vermilion/efi-backup/`);
    `DisableVariableWrite` on, so macOS can't write firmware NVRAM.
  - GPU present is on (`/Library/GPUBundles/nvmtl/ws-gpu-present`, created by boot 2's run-once). Nothing is staged
    on the EFI partition (`/boot/vermilion/perf/` holds the logger for a later run-once).
  - `phxfb-squat` is set as an EFI variable from Linux
    (`/sys/firmware/efi/efivars/phxfb-squat-7c436110-ab2a-4bbb-a880-fe41995c9f82`, value "1").
  - `~/gpu` flags: `sdma`, `cg`, `uclk=456`.
  - `~/gpu/phxboot-pre.sh` = `tools/phxctl/esp_flags.sh` (takes `<EFI>/vermilion/flags/phxboot-*` and runs
    `<EFI>/vermilion/run-once.sh` once as root): **this is how files and flags reach macOS without a desktop**.
  - The AIR bitcode reader (`air-runtime/libLLVM.dylib`, from NullMoth 3.0) is installed.
- **Observability from Linux (no macOS shell needed):** `apfs-fuse` read-only mount of the Data volume
  (`~/.local/src/apfs-fuse`), `/var/log/phxboot.log`, crash reports, the unified log decoded with
  `~/.local/src/macos-UnifiedLogs` ([runbooks](runbooks.md)). Build host: macbox ([runbooks](runbooks.md)).
- **Next steps, in order:**
  1. **Input lag at 1440p** ([findings](findings/2026-11-19-1440p-gpu-present.md)): every frame (14.7 MB) is CPU-copied
     into phxfb's flip buffer B (B fits at 1440p; only the GPU-present slots were 8 MiB). b126 gives frames over 8 MiB
     three GPU-present slots at 256/288/320 MiB (past BAR0, GPU only), the XT's 1080p slots unchanged. Boot 1 done: no
     regression, layout `large`, baseline 10-12 ms copy + 15-20 ms flip wait per frame, ~30 frames/s cap. Boot 2
     (staged): `run-once.sh` creates `/Library/GPUBundles/nvmtl/ws-gpu-present`, same logger; read
     `/var/log/vermilion-perf-*.log` and the kernel's `scanout acquired` line.
  2. The monitor's refresh: **60 Hz** at 2560x1440 (OTG0 totals 2720x1481 in the kernel log, boot 7). 144/240 Hz
     needs mode switching + DP link training (Phase 3).
  3. Phase 1's rest: sleep off (`pmset`), delete p4 and grow the container, Wi-Fi, Ethernet (E1000 dext fails its
     entitlement check: I210 + `e1000=0` fallback), Bluetooth, USB map.
  4. Small fixes: `recovery_install.sh`'s Data-volume search (found 3 in Recovery), the reader in
     `esp_stage.sh`/`recovery_install.sh`, the canned profile still says "RX 7900 XT" / 84 CU (docs/XTX.md item 1).
- **Root:** never guess the password; T3's `request_secret` with file delivery.

## Phase 0: Housekeeping

- [x] 2026-11-18 Explore upstream and the machine; audit XT vs XTX ([findings](findings/2026-11-18-xtx-port-audit.md))
- [x] 2026-11-18 Fork to `ownback/vermilion` (remote `fork`; `origin` = leolivm upstream)
- [x] 2026-11-18 100 GiB partition for macOS on the NVMe (exFAT `MACOS`, to erase as APFS in the installer)
- [x] 2026-11-18 `CLAUDE.md`, `AGENTS.md`, `docs/README.md`, `docs/ARCHITECTURE.md`, `docs/GLOSSARY.md`,
      `docs/XTX.md`, `docs/REFERENCE-MACHINES.md`, this `.agents/` folder
- [ ] Make the mock tests run on Linux ([findings](findings/2026-11-18-mock-tests-on-linux.md)): `/private/tmp`
      becomes `${TMPDIR:-/tmp}`, then the BSD tool differences (`stat -f`, `date -j`, PlistBuddy, `sed -i ''`). Target:
      every suite green on both macOS and Linux
- [ ] CI: GitHub Actions running the mock tests (Linux runner), plus a macOS runner that builds the kexts, plugin
      and translator (no signing)
- [x] 2026-11-19 Set up the build host (macbox, macOS 26.7.3 + Xcode): MacKernelSDK, Rust, Python 3.12 via uv,
      meson/ninja, glslang, Mesa `f5cb8ee0`, SPIRV-Tools; every component built; steps in [runbooks.md](runbooks.md)

## Phase 1: Bare-metal macOS without GPU acceleration

Goal: macOS 26.7.1 boots from OpenCore on the GOP framebuffer, everything except GPU acceleration works.

- [ ] BIOS checklist for macOS (dGPU as boot display, Above 4G + ReBAR on, CSM off, Secure Boot off). Record the ASUS
      menu paths in [runbooks.md](runbooks.md)
- [x] 2026-11-18 Which macOS: Apple still serves the full 26.7.1 installer (build 25G241, catalog product
      `142-28081`); recovery would give the newest, 26.7.3 ([findings](findings/2026-11-18-efi-and-installer.md))
- [x] 2026-11-18 Installer media: macOS partition split into p3 78 GiB (`MACOS`) + p4 22 GiB (`macOS Installer`,
      HFS+ type). 26.7.1 (25G241) made in the macbox VM with `createinstallmedia` onto a bare HFS+ image, streamed onto p4
      and SHA-256 verified (`efi/installer/`)
- [ ] After the install: delete p4, grow the APFS container back to 100 GiB
- [x] 2026-11-18 Reproducible EFI builder: [`efi/`](../efi/README.md) (pinned + hashed inputs, config generated from
      `Sample.plist` + `efi/machines/b650e-i-xtx.json`, stages install/base/vermilion, `ocvalidate` clean)
  - [x] OpenCore 1.0.8, Lilu 1.7.2, VirtualSMC 1.3.8, NVMeFix 1.1.3, RestrictEvents 1.1.6, AppleMCEReporterDisabler
        (codeless, in repo), AMFIPass 1.4.1, AirportMtkWlan 2.1.6 (Tahoe), USBToolBox 1.2.0 (unused until a map exists)
  - [x] AMD_Vanilla `eaf52ef2` with core count `0x0C` and the Zen 4 IOPCIFamily patch
  - [x] `SSDT-EC-USBX-B650EI` written against this board's DSDT (EC0 at `\_SB.PCI0.SBRG.EC0`, no `_STA`)
  - [x] SMBIOS MacPro7,1 generated locally (`~/.config/vermilion/`, not in git)
  - [x] 2026-11-18 First boot of the `install` stage: the installer kernel started, then macOS rejected the DSDT
        (`GPP7.UP00.DP58` defined in a module-level `If`) ([findings](findings/2026-11-18-first-boot-acpi.md))
  - [x] 2026-11-18 DSDT fix: `efi/acpi/fix_dsdt.py` + `acpi-dump` + `RebaseRegions`; verified with `acpiexec`
  - [x] 2026-11-18 Second boot: the DSDT loaded (no ACPI errors) but it hung at the same line. Cause: the chipset
        devices are still conditional (nullmoth/1401 issue 21, same symptom on ASUS AM5)
  - [x] 2026-11-18 Fix v2: `fix_dsdt.py resolve` with the Linux namespace (unwrap active blocks, remove inactive ones)
  - [x] 2026-11-19 Then: in-place DSDT NOP patches, SSDT-CPUR, iGPU off, AGDP pikera patch, DisableVariableWrite.
        The installer ran to the end (see Current state)
  - [ ] USB map: the chipset controller `0c:00.0` has 17 ports (12 USB 2 + 5 USB 3), over macOS's 15 per controller.
        Map after the install (USBToolBox), or generate from Linux sysfs
- [x] 2026-11-18 `install` stage on the EFI partition (`/boot/EFI/OC`), firmware entry Boot0006 "OpenCore",
      boot order Limine (0000) first, OpenCore second (pick it with F8). `EFI/BOOT` untouched
- [ ] Optional: a Limine menu entry that chainloads OpenCore
- [x] 2026-11-19 macOS 26.7.1 installed on `Macintosh HD` (p3); all installer phases done, last one "Boot 1"
      ([findings](findings/2026-11-19-install-phases.md))
- [x] 2026-11-19 Read the macOS disk from Linux: `apfs-fuse` build steps in [runbooks.md](runbooks.md)
- [x] 2026-11-19 First normal boot of Macintosh HD: Setup Assistant drew on the GOP framebuffer, account created, desktop
      (slow, no acceleration) ([findings](findings/2026-11-19-install-phases.md))
- [ ] Post-install, one item at a time, each tested and logged:
  - [ ] Ethernet I225-V: try the built-in driver first. Fallback documented on Tahoe 26.7.1 with an I225-V:
        AppleIntelI210Ethernet 2.3.1 + `device-id 0x1533` + `e1000=0`
        ([GeekAudio/intel-i225-tahoe-fix](https://github.com/GeekAudio/intel-i225-tahoe-fix)); the B650E-F config
        uses the same kext + `e1000=0`
  - [x] 2026-11-19 Wi-Fi MT7922 (`14c3:0616`, subsystem `105b:e0cd`) ([findings](findings/2026-11-19-wifi-mt7922.md)):
    - [x] 2026-11-19 Boot 1: AirportMtkWlan 2.1.6 loaded but never attached (no driver object, no Wi-Fi port)
    - [x] 2026-11-19 Boot 2: MtkWirelessWifi 1.1.1 loaded, not attached either. Cause: the card's IOPCIDevice has
          an empty capability list (no MSI) because its config space didn't answer when IOPCIFamily published it;
          the I225-V has the same symptom. Hotplug ruled out
    - [x] 2026-11-19 Boot 3 (`vermilion-pcilog`) + live reads: the Wi-Fi and Ethernet endpoints stop answering
          config requests after the 0.5 s scan (reading them froze the machine); L1.2, default ASPM, BAR moves,
          ACPI power ruled out
    - [x] 2026-11-19 Linux-side capture ([handoff](HANDOFF-LINUX-CAPTURE.md)): under Linux every link on the path has
          Common Clock set on both ends (Link Control `0x0c42` on DP58, `0x0042` on the card); in macOS DP58 read
          `0x0002` (Common Clock clear) with retrain status bits set ([findings](findings/2026-11-19-wifi-mt7922.md))
    - [x] 2026-11-19 Nobody clears it in macOS: Common Clock was already clear at the 0.47 s scan on the whole chipset
          tree and no ACPI code writes link control; Linux sets CCC itself. Hypothesis: BIOS-enabled ASPM L1 with CCC
          clear, the endpoints never leave L1 in macOS ([findings](findings/2026-11-19-wifi-mt7922.md))
    - [x] 2026-11-19 BIOS PCIe ASPM off: no change (ASPM ruled out)
    - [x] 2026-11-19 Cause: macOS sets 10-Bit Tag Requester Enable on the GPP7 root port; MT7922 and I225-V can't
          complete 10-bit tags (Linux leaves the requester off)
    - [x] 2026-11-19 **Wi-Fi works** (boot 4): boot-arg `pci=0x8000000` in the `base` stage; MtkWirelessWifi 1.1.1,
          802.11ax 5 GHz 80 MHz, 780 Mbit/s, WPA2/WPA3; Vermilion unaffected
    - [ ] Fix it (then Wi-Fi and probably Ethernet work)
  - [ ] AirDrop/Continuity over Wi-Fi: AirportMtkWlan's AWDL is off unless `-mtkawdl` (2.2.x AWDL builds panicked
        and were pulled); only after Wi-Fi is stable
  - [ ] Bluetooth MT7922 (USB **`0489:e0e2`** on this board, read 2026-11-19; not `0e8d:0616`): AirportMtkWlan's BT companion (announced, not in the v2.1.6 release)
        or SimpleMtkBtFw (add the USB ID if missing; only one loader). The companion 2.2.1 panicked or hung on Tahoe
        (AirportMtkWlan issues #14, #16)
  - [ ] USB audio (ASUS `0b05:1a5c`), front and rear USB ports (USB map), NVMe, CPU power management, shutdown/restart
  - [ ] Disable sleep for now (`pmset`, as in INSTALL.md section 10)
  - [ ] Back up the working EFI (outside git, without SMBIOS in any public place)

## Phase 2: Vermilion on the XTX, first light

Goal: an accelerated desktop at 1440p60 on the GOP mode, stable across many cold boots. Details:
[docs/XTX.md](../docs/XTX.md).

- [ ] Quick test before any rebuild: `RADV_DARWIN_CANNED=navi31` (upstream's XTX profile)
- [ ] `navi31_xtx` canned profile in a new Mesa patch; pick the profile by PCI revision (the kext reports it, e.g. in a
      spare `n48n_info.reserved[]` word); fix the exact-name checks in 0016/0017; marketing name "RX 7900 XTX"
- [ ] `mesboot.c`: derive `SYSTEM_APERTURE_HIGH` from `FB_LOCATION_TOP` instead of `0x00213fbf`
- [ ] Pipe-1 plans: HUBP1 aperture derived or per board (`0x00217fff` on the XTX)
- [ ] Metal core count from the profile (`nvmtl_corecount.c`)
- [ ] A boot log line (daemon or kext) with: PCI revision, DMCUB version (SCRATCH1), OTG0 totals, DP link
      rate/lanes, DPCD 0x100/0x101, `phx-bar0-len`, VRAM size
- [ ] Docs: name rev `0xC8` and `0xCC` in README, INSTALL, script headers
- [x] 2026-11-19 Build everything on the build host (macbox; [runbooks](runbooks.md))
- [x] 2026-11-19 Install path without a desktop: `esp_stage.sh` (Linux) + `recovery_install.sh` (Recovery), flags
      from Linux through `esp_flags.sh` (daemon pre-hook) ([findings](findings/2026-11-19-phase2-build-and-stage.md))
- [x] 2026-11-19 Install with flags `sdma`, `cg`, `uclk=456` only (Recovery, `VERMILION_DATA` override)
- [x] 2026-11-19 **First boot: the bring-up works on the XTX unchanged** (queues ready, SDMA idle, CG, UCLK pin)
      ([findings](findings/2026-11-19-first-vermilion-boot.md))
- [x] 2026-11-19 VermilionAccel into the AuxKC (one boot with a login; syspolicyd rebuilt it), `NMXAccel up`,
      Metal for apps ([findings](findings/2026-11-19-first-vermilion-boot.md))
- [x] 2026-11-19 First desktop-arm boot: arm path OK at 1440p, WindowServer aborted (no AIR bitcode reader)
- [x] 2026-11-19 **Accelerated desktop on the XTX: SUCCESS** (reader installed through run-once; Metal compositor)
- [x] 2026-11-19 Phantom "UNKNOWN" display fixed by squat boots: `phxfb-squat` written as an EFI variable from Linux
      (macOS can't write NVRAM here); it stays set, so every OpenCore boot is a squat boot. Delete it with Reset NVRAM
      or from Linux (`/sys/firmware/efi/efivars/phxfb-squat-7c436110-ab2a-4bbb-a880-fe41995c9f82`)
- [~] **Input lag at 1440p: frames are CPU-copied through the BAR** ([findings](findings/2026-11-19-1440p-gpu-present.md))
  - [x] 2026-11-19 VRAM layout audited (code + Linux read-only); flip B already fits 1440p; DCN FB TOP is `0x3936`
  - [x] 2026-11-19 VermilionBoot b126: GPU-present slots by frame size (large: 256/288/320 MiB), `debug.phxscan_layout`;
        host test 43/43, mock suites green on macbox, installed on the EFI (unverified on hardware)
  - [x] 2026-11-19 `tools/phxctl/perflog.sh` + `perflog_install.sh`: the display counters to a log, one boot only
  - [x] 2026-11-19 Boot 1: b126 + GPU present off: no regression; layout `large`, DCN window = all VRAM; baseline
        copy 10-12.6 ms/frame + 15-20 ms flip wait, ~20-33 frames/s (two refreshes per frame)
  - [x] 2026-11-19 Boot 2: GPU present on: large slots acquired, no CPU copy, median 40 / max 59.5 frames/s; owner:
        "way better" but flicker, occasional artifacts, CPU hotter
  - [ ] Flicker/artifacts with GPU present at 1440p (try `debug.phxscan_latchfix=1`, capture `phxscan_race_stat`)
  - [ ] CPU heat with GPU present (measure: `powermetrics`, WindowServer CPU; the plugin's per-present fence wait)
  - [ ] If it holds: make GPU present the default at 1440p (daemon or install), docs; consider `debug.phxscan_latchfix=1`
        (b74 UI-shake fix, default 0)
- [~] Refresh rate / input lag at 1440p: the GOP mode is 2560x1440 at 60 Hz (2026-11-19, OTG0 totals 2720x1481);
      frames go through the CPU copy into flip B (see the item above). DP link rate/lanes: not logged yet
- [ ] `recovery_install.sh` and `esp_stage.sh`: include the air-runtime reader (from NullMoth's package, md5-pinned)
- [ ] `recovery_install.sh`: its Data-volume search found 3 candidates in Recovery (the system volume shows the
      Data volume's folders through firmlinks); pick the APFS Data role (`diskutil apfs list`) or the volume whose
      users directory is on its own device; test on macbox
- [ ] First boot without desktop arm: bring-up log, `metaltest`, temperatures and power
- [ ] Squat boot: accelerated desktop at 1440p60
- [ ] Soak: 20 cold boots in a row reach `SUCCESS`; a day of normal use with no GPU hang (log every failure in
      findings)
- [ ] Decide GPU present vs CPU copy at 1440p after measuring (see Phase 3 scan slots)

## Phase 3: 1440p high refresh (120/144/165/240 Hz)

- [ ] Linux capture tooling (root, read-only), kept in `tools/capture/` without machine data in git:
  - [ ] DCN register dump script (register list in [docs/XTX.md](../docs/XTX.md)), output `.inc`-ready
  - [ ] `amdgpu_dm_dtn_log` per mode, DPCD dump from `/dev/drm_dp_aux*`, ftrace of `amdgpu_device_wreg` during mode
        changes
  - [ ] Install `umr` (optional)
- [x] 2026-11-21 Captures at 1440p 60/120/144/240 on this card and monitor, SDR and HDR, plus the other EDID modes
      (capture v2-v4, `tools/capture/linux_display_capture.py`; the LG has no 165 Hz mode)
- [x] 2026-11-20 A plan generator (upstream's is not public): `tools/capture/p0odm_from_trace.py`, capture to
      `phx_p0odm_*.inc`; all 29 plans regenerate byte for byte (checked 2026-11-22). Its only test is that comparison
      and `tests/kext/p0odm_test.cpp` running the plans; the comparison is not scripted in the repo yet
- [x] 2026-11-19 Scan slots for 1440p and up (b126, `PhxScanLayout.h`: 32 MiB slots at 256 MiB, unverified on hardware)
- [ ] EDID: E-DDC segment pointer, serve all 384 bytes (`PhxDdc.h`, `PhxEdid.h`)
- [ ] Mode matching by EDID timings instead of the two fixed 1080p entries (`PhxPipe0.h`, `PhxPipe2.h`, the scripts)
- [x] 2026-11-20 DP link retraining on the GOP connector (DP B / DIG1) to HBR2 and HBR3 (`PhxDpLink.h`, `PhxDpbRetrain.h`)
- [x] 2026-11-20 240 Hz: HBR3 x4 with FEC + DSC + ODM 2:1, as Linux (`PhxP0Odm.h`)
- [ ] Watermarks/DET/DAL tables for each mode (re-captured on the 384-bit XTX)
- [ ] Display power recipe (`b15`) for one 1440p monitor, with its underflow watchdog
- [ ] Mock tests for every new engine path

## Phase 4: Stability and the gaps upstream left

- [ ] Idle power and temperature at parity with Linux (measure both)
- [ ] Sleep/wake: research a resume path (replay the bring-up from the kext, DMUB/DCN re-init) or a safe "display
      sleep only" mode
- [ ] DP audio (HDMI/DP audio is not implemented anywhere in the stack)
- [ ] VCN video decode on the XTX (`phxboot-vcn`, `vcn_up.sh` checks `GB_ADDR_CONFIG`)
- [ ] App compatibility list: Safari/WebKit, Chromium/Electron, VS Code, Blender, games; file and fix crashes
- [ ] macOS update policy (pinned to 26.7.1; what breaks on an update, how to roll back)
- [ ] Hot-plug of the DP monitor (`phxboot-hotplug0`) and input switching. In Phase 5 the monitor switches inputs
      all the time

## Phase 5: Virtualization (dGPU passthrough)

Goal: Linux runs on the iGPU, can use the dGPU (PRIME offload), and a `macos` command hands the dGPU to a macOS VM.
When the VM shuts down, the dGPU goes back to Linux.

- [ ] BIOS: iGPU as the primary display; check the dGPU GOP still runs in the VM (OVMF with the card's ROM)
- [ ] Linux display on the iGPU only (Hyprland: `AQ_DRM_DEVICES`), dGPU used via PRIME render offload
- [ ] Reset strategy for Navi 31: test unbind amdgpu, bind vfio-pci, VM, rebind (BACO/mode1/FLR). Vermilion needs
      a cold GPU, so check whether amdgpu's unload leaves the PSP/SMU in a state the bring-up accepts. If not:
      reserve the card for vfio-pci at boot, or add a "warm" bring-up path
- [ ] VM definition: QEMU + OVMF + OpenCore (reuse macbox's QEMU/OpenCore work), dGPU + its audio function
      passed through, CPU pinning for the 7900X3D (cache CCD), hugepages, USB controller or device passthrough for
      keyboard and mouse
- [ ] The `macos` launcher script (start/stop, rebind on exit, safety checks that nothing on the host uses the dGPU)
- [ ] Same macOS install bare-metal and in the VM? (a decision: share the APFS partition or use separate disks)

## Phase 6: Clean up and hand over

- [x] 2026-11-22 Clean-up pass on the display code of `ownback/gpu-refresh-hdr` before the merge: comments say what
      the code does (history in CHANGELOG / findings), docs/XTX.md and docs/INSTALL.md list the display knobs. No
      behaviour change: both kexts and the plugin rebuild byte for byte
- [ ] A script in the repo that regenerates every replay plan from a capture directory and compares (today by hand)
- [ ] Replace the hardcoded connector table with the VBIOS parser (`PhxVbios.h` already produces the same table)
- [ ] Split the remaining machine-specific values into per-board / per-monitor data files with detection
- [ ] Refactor the largest files where it makes reviews easier (`nvmtl_vk.c`, `NVMTLObjects.m`, `nvrm-accel.cpp`),
      behaviour unchanged, in separate commits
- [ ] Upstream PRs to leolivm: XTX profile and detection, derived apertures, portable tests, docs
- [ ] A public install guide for the XTX machine class, issue templates, CONTRIBUTING.md

## Open questions

- Is the XFX VBIOS's DMCUB `0x07008742`? (first macOS boot)
- Which link rate does the GOP train for 1440p60? (first macOS boot)
- Does CoreDisplay accept the truncated 256-byte EDID?
- Does the AM5 new-firmware hang hit BIOS 4005?
- Is the I225-V stable on Tahoe without a boot-arg?
