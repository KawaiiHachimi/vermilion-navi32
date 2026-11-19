# TODO

The goal: a fully working, stable macOS Tahoe on the XTX machine ([docs/REFERENCE-MACHINES.md](../docs/REFERENCE-MACHINES.md)).
That means the GPU accelerated at 1440p high refresh, plus Wi-Fi, Bluetooth, Ethernet, audio and USB. After that, the
same install as a VFIO VM. Then clean up, document and hand over to the community.

Rules: tick items when done (with the date), add new ones where they belong, link findings. `[ ]` open, `[~]` in
progress, `[x]` done, `[-]` dropped (say why).

## Current state (handoff, 2026-11-19)

- **macOS 26.7.1 is installed** on p3 (`Macintosh HD`, APFS, 78 GiB) with the `install` stage EFI (`/boot/EFI/OC`,
  firmware entry "OpenCore", Limine first in the boot order; the owner picks OpenCore with F8). The install finished
  all its phases: the restart after a short Apple logo was the installer's own "Triggering reboot" after "Boot 1"
  ([findings](findings/2026-11-19-install-phases.md)). The first normal boot reached Setup Assistant and the desktop
  (2026-11-19): slow, as expected without GPU acceleration. (For the record: nullmoth/1401 documents a black screen with only a cursor where
  Setup should start on macOS 26 without a GPU driver; their fix creates the account from Recovery's Terminal and skips
  Setup (docs/TROUBLESHOOTING.md, `finish-setup.sh`). It did not happen here.)
- **What it took** ([findings](findings/2026-11-18-first-boot-acpi.md)):
  1. 5 in-place DSDT NOP patches (AM5 chipset `If (G002 ...)` blocks);
  2. SSDT-CPUR (the CPUs exist only as ACPI0007 devices);
  3. iGPU disabled in the BIOS;
  4. the AGDP pikera kernel patch (Navi);
  5. `DisableVariableWrite`.
- **BIOS now:** iGPU disabled (re-enable for Phase 5), CSM off, Above 4G + ReBAR on.
- **Observability:** photos of the verbose screen; the OpenCore logs on `/boot`; and since 2026-11-19 the macOS disk
  itself via `apfs-fuse` from Linux (`install.log`, panic reports; [runbooks](runbooks.md)). `log show` once the
  desktop works.
- **Next steps, in order:**
  1. ~~First boot of Macintosh HD to the desktop~~ done 2026-11-19. Too slow to use: WindowServer runs its CPU
     "Software compositor" at 30 Hz, no IOGPU device ([findings](findings/2026-11-19-first-desktop-logs.md)).
  2. **Phase 2 first (owner's decision, 2026-11-19):** built and staged
     ([findings](findings/2026-11-19-phase2-build-and-stage.md)). `vermilion` EFI stage on `/boot/EFI/OC`
     (`OC.prev` = `install`), payload in `/boot/vermilion`. **Next boot:** Reset NVRAM > Recovery > Terminal >
     `mkdir -p /tmp/efi && diskutil mount -mountPoint /tmp/efi disk2s1 && sh /tmp/efi/vermilion/install.sh` > restart into Macintosh HD, wait ~3 min
     (no desktop arm: bring-up + Metal for apps), shut down, then read `/var/log/phxboot.log` from Linux. Then stage
     `phxboot-desktop` from Linux for the accelerated-desktop boot ([runbooks](runbooks.md)).
  3. Then Phase 1's rest: sleep off (`pmset`), delete p4 and grow the container, Wi-Fi, Ethernet (Apple's E1000 dext
     fails its entitlement check, so the I210 + `e1000=0` fallback), Bluetooth, USB map.

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
  - [ ] Wi-Fi MT7922: Arxusion/AirportMtkWlan (Tahoe build). Fallbacks: qiqco/MT7922-macOS-Driver,
        GeekAudio/MtkWireless
  - [ ] Bluetooth MT7922 (USB `0e8d:0616`): AirportMtkWlan's BT companion (announced, not in the v2.1.6 release)
        or SimpleMtkBtFw (add the USB ID if missing; only one loader)
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
- [ ] **Input lag at 1440p: frames are CPU-copied through the BAR** (flip buffer B and GPU-present slots are 8 MiB,
      a 1440p frame is 14.7 MB). Next code work: 16 MiB (or mode-sized) scan-out regions; needs a VRAM layout change
      (`phxhead.h`, `PhxNativeABI.h`; the PSP ring sits at 48 MiB, BAR0 window 160 MiB)
- [ ] Refresh rate / input lag at 1440p: measure what the GOP mode and DP link are, and whether frames go through the
      CPU copy (no GPU present at 1440p: 8 MiB scan slots, docs/XTX.md)
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
- [ ] Captures at 1440p 60/120/144/165/240 on this card and monitor
- [ ] A plan generator (upstream's is not public): capture to `phx_p0_plan_*.inc`, with self-tests
- [ ] Scan slots 16 MiB (`PhxNativeABI.h`, `phx_scan.cpp`)
- [ ] EDID: E-DDC segment pointer, serve all 384 bytes (`PhxDdc.h`, `PhxEdid.h`)
- [ ] Mode matching by EDID timings instead of the two fixed 1080p entries (`PhxPipe0.h`, `PhxPipe2.h`, the scripts)
- [ ] DP link retraining on the GOP connector (DP B / DIG1) to HBR2, later HBR3 (`PhxDpLink.h`)
- [ ] 240 Hz: HBR3 x4 or DSC (research: does DCN 3.2 DSC need DMUB support we don't have?)
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
