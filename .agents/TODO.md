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

Nothing. Nothing is staged on the ESP (`<ESP>/vermilion/flags/` empty, no `run-once.sh`), and no EFI change is
pending (2026-11-23, thread 00).

## 01 GPU stability

- [ ] **Failed DP link training at a login mode switch** (1440p60 HDR, 2026-11-22 20:19, once, b150): black screen,
      and every later switch refused until reboot. Keep the trainer's CR / EQ details for a failed run, retry once,
      fall back to the 144 hub instead of locking the engine; lock only on repeated failures. State saved:
      `~/vermilion-captures/black-20261122-2025.txt`
- [ ] The daemon reported `SUCCESS` during that black screen: read `debug.phxp0odm_stat` after login and recover or
      report
- [ ] **Panic reports:** none is saved, because `DisableVariableWrite` blocks the kernel's NVRAM panic log (from the
      config; not watched failing here). Test one boot without `DisableVariableWrite` (`sudo nvram vermilion-test=1`,
      read it back); if writes work and nothing hangs, keep it off. This also lets macOS clear `phxfb-squat`.
      Fallback: photos of the panic screen. Needed first for the restart panic of 2026-11-19 22:44 (cause unknown)
- [ ] **Physical size 903 x 508 mm** (= 2560 x 1440 px at 72 dpi), where the EDID says 590 x 333 mm: PhxAMDFB doesn't
      give WindowServer the EDID size / display attributes *(cause inferred)*. Blocks HiDPI / scaled modes. Then
      check whether macOS offers "looks like" modes ([finding](findings/2026-11-23-macos-integration-inventory.md))
- [ ] Changing the VRR floor gives macOS a new display identity (colour profile and settings reset), because the
      floor label is patched into the EDID PhxAccel serves
- [ ] Tearing at 1440p60 with GPU present; input lag at 60 Hz. Measure first (a phone video; pointer acceleration
      off). Plugin: the schedule-time copy of the non-async GPU present (present only after completion)
- [ ] DRR double buffer for the whole boot as on Linux, not only while Variable is on (decisions 2026-11-22,
      "Revisit"), then re-verify every plan
- [ ] Display sleep (`pmset displaysleepnow`, wake with a key; GPU power while dark), from the old thread 07's
      inventory. Rollback: Apple menu > Restart
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
      Results go into PROJECT_KNOWLEDGE's performance record
- [ ] **CPU never above 5.15 GHz, idle package 40-50 W** (2026-11-23). Suspect: `HLT`-only idle, the firmware's `_CST`
      C2 / C3 (`inb 0x414` / `0x415`) unused, so no CC6 *(unverified)*. If Linux reaches ~5.5 GHz and idles 15+ W
      lower: a minimal `_CST` idle kext (never AMDRyzenCPUPowerManagement). BIOS C-state / CPPC settings first
      ([OWNER-TESTS](OWNER-TESTS.md))
- [ ] Real-app test set: WebGL / WebGPU in Safari and Chromium, a Metal game for 20 min, Minecraft (F3 renderer line)
- [ ] XTX RADV profile chosen by PCI revision: 96 CU, 384-bit, the name "RX 7900 XTX"; fix the exact-name checks in
      `patches/mesa/0016` / `0017` (the VCN shim). `RADV_DARWIN_CANNED=navi31` is **not** a safe quick test: the
      VCN shim's name check would disable decode
- [ ] Working set from VRAM instead of the fixed 6 GiB (`phx_globals.cpp`), together with the next item only
- [ ] Where buffers live: every BO is system memory today (`phx_client.cpp`). Measure with a memory-bound
      `metalbench` mode, then VRAM-backed BOs (weeks; a design note first)
- [ ] Fence waits by interrupt (`debug.phxih`, `debug.phxwait_irq`, off and unverified on the XTX): 101 us per waited
      submit today, 6 us of it GPU
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

      Background in [the finding](findings/2026-11-22-audio-connectivity-inventory.md).
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
      with 09 *(unverified, web research 2026-11-23)*
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

- [ ] Installer partition `disk0s4`: keep it as the 26.7.1 reinstall path (Recovery would install the newest macOS),
      or delete it and grow the container. The owner keeps it for now (2026-11-22); decide, then fix `efi/README.md`
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
- [ ] Upstream PRs to leolivm (VermilionTrust first, the XTX profile and detection, derived apertures, portable tests)

## Later: virtualization (after the series)

- [ ] macOS as a VM with the dGPU passed through (VFIO), Linux on the iGPU: reset strategy for Navi 31 (Vermilion
      needs a cold GPU), VM definition, the `macos` launcher. The macOS guest still has no Hypervisor.framework
