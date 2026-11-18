# TODO

The goal: a fully working, stable macOS Tahoe on the XTX machine ([docs/REFERENCE-MACHINES.md](../docs/REFERENCE-MACHINES.md)).
That means the GPU accelerated at 1440p high refresh, plus Wi-Fi, Bluetooth, Ethernet, audio and USB. After that, the
same install as a VFIO VM. Then clean up, document and hand over to the community.

Rules: tick items when done (with the date), add new ones where they belong, link findings. `[ ]` open, `[~]` in
progress, `[x]` done, `[-]` dropped (say why).

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
- [ ] Set up the build host: check macbox's macOS version; install the Command Line Tools, MacKernelSDK, Rust with
      `x86_64-apple-darwin`, Mesa build deps (meson, ninja, glslang, mako). Build every component once and write
      down the exact steps in [runbooks.md](runbooks.md)

## Phase 1: Bare-metal macOS without GPU acceleration

Goal: macOS 26.7.1 boots from OpenCore on the GOP framebuffer, everything except GPU acceleration works.

- [ ] BIOS checklist for macOS (dGPU as boot display, Above 4G + ReBAR, CSM off, Secure Boot off, IOMMU on or off as
      macOS needs (`DisableIoMapper`), SVM). Record the ASUS menu paths in [runbooks.md](runbooks.md)
- [ ] Get macOS 26.7.1 exactly. `macrecovery` may serve a newer recovery, so check what it offers; alternatively a full
      InstallAssistant for 26.7.1. Decide and record in [decisions.md](decisions.md)
- [ ] A USB stick for the installer (the 8 GB SanDisk holds BIOS files, so keep those or use another stick)
- [ ] OpenCore 1.0.8 EFI, built by the Dortania AMD (Zen) guide:
  - [ ] ACPI: SSDT-EC-USBX (desktop), plus whatever the B650E-I DSDT needs (the B650E-F needed a DSDT fix, see the
        [kext findings](findings/2026-11-18-macos-hardware-kexts.md))
  - [ ] Kernel: AMD_Vanilla patches with core count `0x0C`, the Zen 4 IOPCIFamily patch, `ProvideCurrentCpuInfo`
  - [ ] Kexts: Lilu, VirtualSMC, AMDRyzenCPUPowerManagement + SMCAMDProcessor, AppleMCEReporterDisabler, NVMeFix,
        RestrictEvents, USB map (USBToolBox + UTBMap), AMFIPass (needed later by Vermilion)
  - [ ] SMBIOS MacPro7,1, generated locally, kept out of git
  - [ ] `config.plist` validated with `ocvalidate`
- [ ] Boot entry: OpenCore in `EFI/OC` with its own `efibootmgr` entry. Leave Limine and `EFI/BOOT/BOOTX64.EFI`
      alone. Optional: a Limine menu entry that chainloads OpenCore
- [ ] Install macOS to the `MACOS` partition (erase it as APFS, nothing else)
- [ ] Post-install, one item at a time, each tested and logged:
  - [ ] Ethernet I225-V (built-in driver, boot-arg or SSDT only if needed)
  - [ ] Wi-Fi MT7922: Arxusion/AirportMtkWlan (Tahoe build). Fallbacks: qiqco/MT7922-macOS-Driver,
        GeekAudio/MtkWireless
  - [ ] Bluetooth MT7922 (USB `0e8d:0616`): AirportMtkWlan's BT companion or SimpleMtkBtFw (add the USB ID if
        missing; only one loader)
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
- [ ] Build everything on the build host, install by INSTALL.md with flags `sdma`, `cg`, `uclk=456` only
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
- Does macrecovery still serve 26.7.1?
- Is the I225-V stable on Tahoe without a boot-arg?
