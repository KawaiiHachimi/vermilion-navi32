# Project knowledge: the XTX Hackintosh

The one shared map of this project for every workstream (T3 thread) and every agent. **Read this first**, then only
the files your scope needs. It doesn't copy what the other files already say. It says where each fact lives, how
the parts depend on each other, and what state the machine is in.

Maintained by thread **01 — Shared Repo Intelligence**. Last full survey: **2026-11-22**, on `fork/main` `a177e96`
and the live XTX machine (macOS 26.7.1 25G241).

## How to use and update this file

- **Before you start:** read the sections "Workstreams", "Machine state" and "Cross-thread rules", then your own
  workstream's row and the files it links. Do not survey the whole repository again. If this file is wrong or
  missing something you need, fix it here (or tell thread 01) instead of keeping a private index.
- **Where details go** (unchanged, see [`.agents/README.md`](../.agents/README.md)):
  - a dated investigation: [`.agents/findings/`](../.agents/findings/);
  - the plan and its checkboxes: [`.agents/TODO.md`](../.agents/TODO.md);
  - a design choice: [`.agents/decisions.md`](../.agents/decisions.md);
  - an exact procedure: [`.agents/runbooks.md`](../.agents/runbooks.md).
- **What goes here:** a change to the known-good configuration, a subsystem changing state (works / broken), a new
  cross-thread dependency or rule, a performance baseline or bottleneck. Keep each one to a line or two, with a
  date and a link to the finding. Mark anything not tested on the machine *(unverified)*.
- **Branches:** this file lives on `fork/main`. A thread working on a side branch updates it there and the change
  reaches the others when the branch merges. Changes that other threads need *now* (a new rule, a staged boot) go
  straight to `main` through thread 01.
- No serials, MLB, ROM, UUIDs, MAC addresses, passwords or e-mail addresses here ([CLAUDE.md](../CLAUDE.md) rules 3 and 4).

## Workstreams

| Thread | Scope | Branch | State (2026-11-22) |
|---|---|---|---|
| **00 — GPU & Display** | Vermilion on the XTX, the LG at every mode, rate, HDR, VRR | `ownback/gpu-refresh-hdr` (merged: PR #1) | 1440p target done; open items in TODO "Display polish" |
| **00.1 — GPU Performance** | GPU and CPU performance, power, heat, graphics APIs | `ownback/gpu-performance` (merged: PR #2) | baseline measured, then **paused** by the owner until the platform work is done |
| **01 — Shared Repo Intelligence** | this file, the dependency map, the performance record | `ownback/hackintosh-thread-setup` -> `main` | active |
| **02 — Platform Foundation** | ACPI, CPU power management and sensors, USB map (then BT / audio / Ethernet bring-up) | `ownback/postinstall-acpi-cpu-usb` | inventory done 2026-11-19 ([finding](../.agents/findings/2026-11-19-postinstall-inventory.md)), plan waiting for the owner; branch fast-forwarded to `main` 2026-11-22 |
| **03 — Storage & Filesystems** | NVMe, PCIe link, NVMeFix, APFS, TRIM / SMART, temperatures, Time Machine | `ownback/storage` | inventory and baseline done 2026-11-22 ([finding](../.agents/findings/2026-11-22-storage-baseline.md)); proposals waiting for the owner |
| **04 — Audio & Connectivity** | USB audio (no HDA codec: AppleALC doesn't apply), DP / HDMI audio, Ethernet, Wi-Fi, Bluetooth, Continuity | `ownback/audio-connectivity` | inventory and first baselines done 2026-11-22 ([finding](../.agents/findings/2026-11-22-audio-connectivity-inventory.md)); Wi-Fi fix booted and verified (EFI change 4) |
| **05 — Apple Services, DRM & Security** | SMBIOS, iServices, FairPlay / Widevine, SIP / AMFI / Gatekeeper, TCC, FileVault | `ownback/apple-services` | inventory done 2026-11-22 ([Apple services / security](../.agents/findings/2026-11-22-apple-services-security-inventory.md)); owner tests and proposals pending |
| **06 — macOS Integration & Peripherals** | system apps, window management, input devices, sharing, dev workflows | `ownback/macos-integration` | inventory done 2026-11-23 ([finding](../.agents/findings/2026-11-23-macos-integration-inventory.md)); proposals waiting for the owner |
| **07 — Sleep, Power & Final Validation** | sleep / wake, power and thermals, stress, benchmarks, regression, updates | none yet | dormant |
| Legacy Wi-Fi | the Wi-Fi bring-up of 2026-11-19 | `ownback/phase1-wifi` (1 commit not in `main`) | history only |

Older branches `explore-7900xtx-efi-passthrough`, `gpu-1440p-polish`, `gpu-heat` are fully merged into `main`.

## The machine

Hardware: [REFERENCE-MACHINES.md](REFERENCE-MACHINES.md) (ASUS ROG STRIX B650E-I, BIOS 4005, Ryzen 9 7900X3D, 48 GB,
XFX RX 7900 XTX rev `0xC8`, LG 27GS95QE-B on DP, MT7922 Wi-Fi / BT, I225-V, one NVMe shared with Linux). Corrections
from the 2026-11-22 survey: the NVMe is a **Corsair MP700 PRO** (Phison E26, firmware EQFM22.3).

Disk (macOS view, 2026-11-22): `disk0s1` EFI (2 GB, shared with Linux's Limine), `disk0s2` Linux LUKS, `disk0s3` APFS
container (83.8 GB; Macintosh HD + Data), `disk0s4` "Install macOS Tahoe" (23.6 GB HFS+, still present: TODO Phase 1
says delete it and grow the container; the owner keeps it for now, 2026-11-22).

## Machine state (2026-11-22, verified on the machine unless marked)

| Area | State | Evidence | Owner |
|---|---|---|---|
| Boot (OpenCore 1.0.8, `vermilion` stage) | works | live `EFI/OC` = repo build (below) | 02 |
| GPU acceleration (Metal) | works, Metal 3 | [baseline](../.agents/findings/2026-11-22-gpu-performance-baseline.md) | 00 / 00.1 |
| Display 1440p: 60/120/144/240, Variable 60-240, 10-bit HDR, HDR + VRR | works from System Settings | [XTX.md](XTX.md), TODO "Display polish" | 00 |
| Other LG modes | fixed rates only, no VRR / HDR (owner's choice) | decisions 2026-11-22 | 00 |
| OpenGL / OpenCL / Vulkan | software GL, CPU-only CL, no Vulkan loader | baseline | 00.1 |
| Video decode (VCN) | off | baseline | 00.1 |
| Wi-Fi MT7922 | works (MtkWirelessWifi 1.1.1 + `pci=0x8000000`), 802.11ax 2x2, 867 Mbit/s link; **AWDL latency stalls fixed** with `amtk_awdl_offchan=0` (2026-11-22 boot 22:23: router ping max 4 ms, was 456 ms; AirDrop with a phone untested) | [wifi finding](../.agents/findings/2026-11-19-wifi-mt7922.md), [audio / connectivity](../.agents/findings/2026-11-22-audio-connectivity-inventory.md) | 04 |
| Bluetooth MT7922 (USB `0489:e0e2`) | works since 2026-11-22: MtkWirelessBT 1.1.1 + BlueToolFixup 2.7.2 + 2 NVRAM variables; AirPods pair and play (owner). AirDrop / BLE advertising untested | [Bluetooth](../.agents/findings/2026-11-22-bluetooth.md) | 02 -> 04 |
| Audio (ASUS USB `0b05:1a5c`, Realtek ALC4080, UAC2, `usbaudiod`) | works on the line-out (owner hears it, 2026-11-22). The ALC4080 offers only jacks **plugged in at boot** (jack sensing), plus S/PDIF and one input. macOS volume = codec hardware gain, 0 dB at 100 % (bass clips on an aux speaker at 100 %: gain staging). S/PDIF output stable to a 32-frame buffer | [audio / connectivity](../.agents/findings/2026-11-22-audio-connectivity-inventory.md) | 04 |
| DP / HDMI audio | not implemented anywhere in the stack | TODO Phase 4 | 00 / 04 |
| Ethernet I225-V | Apple's DriverKit E1000 bound, `en3`, offers up to 2500BASE-T; no link (cable? owner) | [audio / connectivity](../.agents/findings/2026-11-22-audio-connectivity-inventory.md) | 04 |
| Continuity | iCloud signed in; **AirDrop: Mac not visible on the iPhone** (2026-11-22; suspects: `amtk_awdl_offchan=0`, MtkWireless AirDrop incomplete upstream); Handoff and AirPlay to Mac work; Universal Clipboard fails; owner keeps the Wi-Fi fix over AirDrop (2026-11-22) | same | 04 |
| USB | mapped (2026-11-22): USBToolBox 1.2.0 + `efi/kexts/UTBMap.kext`, all 8 rear ports + internal audio / Bluetooth; USB 3 halves unverified | [USB map](../.agents/findings/2026-11-22-usb-map.md) | 02 |
| CPU sensors / package power | work since 2026-11-22: SMCProcessorAMD 1.0.1 (read-only), `tools/sensors/cpusensors.py`; per-core clocks not published | [CPU sensors](../.agents/findings/2026-11-22-cpu-sensors.md) | 02 |
| CPU power management | AMD_Vanilla patches correct (`0x0C`), `DummyPowerManagement`; real P-state behaviour unmeasured | inventory | 02 / 07 |
| NVMe | works: Apple driver + NVMeFix 1.1.3 (APST on), **Gen 5 x4** (System Information's empty "Link Speed" is only a missing label), TRIM / Unmap yes, SMART clean (2 % used), no thermal throttling (max 65 C sustained, limit 87 C). Cosmetic: shown as "external" (no `built-in` property; fix proposed) | [storage baseline](../.agents/findings/2026-11-22-storage-baseline.md) | 03 |
| APFS / backups | System volume sealed (booted from its snapshot), Data separate, FileVault off; no Time Machine destination | same | 03 |
| Sleep | **off on purpose** (`pmset` sleep 0, displaysleep 0, hibernatemode 0): Vermilion has no GPU resume path | TODO, pmset | 07 (with 00) |
| NVRAM | read-only from macOS (`DisableVariableWrite`) | efi/machines | 02 / 05 |
| SIP / AMFI / Gatekeeper | SIP custom `0x0A03` (authenticated root off, system volume still sealed); **AMFI on since 2026-11-22 23:45** (`amfi=0x80` replaced by VermilionTrust, a cdhash allowlist for Vermilion's code; AMFIPass still loaded); Gatekeeper on; XProtect 5377 and security data updates on since 2026-11-22 23:11 (`ConfigDataInstall = 1`; macOS / Rapid Security Responses still pinned), launch scans off | [Apple services / security](../.agents/findings/2026-11-22-apple-services-security-inventory.md) | 05 |
| iServices | APNs connected, iMessage and FaceTime registered (IDS HTTP 200, 2026-11-22), iCloud signed in; SMBIOS well-formed, ROM = I225-V MAC. **en0 is the iPhone's USB interface** (not built-in; works anyway; optional fix) | [Apple services / security](../.agents/findings/2026-11-22-apple-services-security-inventory.md) | 05 |
| DRM | FairPlay 2.x-4.x (TV app, Safari video) blocked by the missing hardware decode (TV app 2026-11-23: previews play, the film fails with `-12906` no decoder for the protected stream); **Apple Music (FairPlay 1.x) works since 2026-11-23** (full songs; it needed a built-in en0: Ethernet is en0 now; AMFI wasn't the cause). Music crashed on some tracks in the Metal plugin; fix installed 2026-11-23 (EFI change 8), untested. Widevine untested | same | 05 (00.1 for VCN) |
| Input, screenshots, Spotlight | keyboard and mouse work (mouse 1 kHz); `screencapture` returns the real GPU desktop (needs Screen Recording permission); Spotlight indexes and answers (2026-11-23) | [macOS integration](../.agents/findings/2026-11-23-macos-integration-inventory.md) | 06 |
| Display scaling | no HiDPI / scaled modes offered; the display reports 903 x 508 mm (wrong for 27") | same | 06 / 00 |
| Virtualization | **`kern.hv_support = 0`**: no Hypervisor / Virtualization.framework on AMD (Docker Desktop, UTM-HVF, Parallels can't run); a platform limit | same | 06 |
| Sharing (SSH, Screen Sharing, SMB), printers, Xcode | all off / not set up / not installed (2026-11-23) | same | 06 |
| FileVault / login | FileVault off, Secure Token on; automatic login via the daemon (`/etc/kcpassword`), which FileVault would end | same | 05 / 00 |

## Known-good configuration (2026-11-22)

> **Network interface names changed 2026-11-23 00:01 (thread 05):** Ethernet `en3` -> **`en0`**, Wi-Fi `en4` ->
> **`en1`** (verified: Wi-Fi up, default route), iPhone / iPad USB interfaces get the next free numbers. Scripts and
> notes that name an interface must use the new names. Rollback: TODO "Apple services, DRM and security".

This is the configuration the owner uses every day. Change it only through the owning thread.

- **EFI:** `efi/build.py install --stage vermilion` (machine `b650e-i-xtx`). On 2026-11-22 thread 01 built
  `fork/main` `a177e96` with placeholder serials and compared it with the live `EFI/OC`. Every file is identical,
  and `config.plist` is identical apart from `PlatformInfo` (the serials). **The repo is the source of truth for
  the EFI.**
  - OpenCore 1.0.8. Kexts: Lilu 1.7.2, VirtualSMC 1.3.8, NVMeFix 1.1.3, RestrictEvents 1.1.6,
    AppleMCEReporterDisabler, MtkWirelessWifi 1.1.1, USBToolBox 1.2.0 + UTBMap, SMCProcessorAMD 1.0.1, BlueToolFixup 2.7.2, MtkWirelessBT 1.1.1 (all since 2026-11-22), AMFIPass 1.4.1, VermilionBoot b150. All pinned in
    [`efi/components.json`](../efi/components.json).
  - ACPI: `SSDT-CPUR-B650EI`, `SSDT-EC-USBX-B650EI`, plus 5 DSDT NOP patches.
  - NVRAM (Apple GUID, set by OpenCore every boot): `bluetoothInternalControllerInfo`, `bluetoothExternalDongleFailed` (Bluetooth).
  - boot-args: `-v keepsyms=1 debug=0x100 pci=0x8000000 amfi=0x80 ipc_control_port_options=0 phxdebug=1`;
    `csr-active-config` `030A0000`. SMBIOS MacPro7,1.
  - Since 2026-11-22 20:00 `EFI/OC.prev` = the same b150 EFI without the USB map (b149's EFI is in `~/vermilion-rollback/EFI-OC.b149-20261122`). Before that: `EFI/OC.prev` held b149, the rollback. VermilionBoot sha256 starts `3806555e` (b150), and `d34093c0` in
    `OC.prev`.
- **Since 2026-11-22 22:23 (thread 04, EFI change 4):** the same EFI plus boot-arg `amtk_awdl_offchan=0` (only
  `boot-args` differ), verified. `EFI/OC.prev` = the Bluetooth EFI without it; rollback in TODO "Audio and
  connectivity".
- **Since 2026-11-22 23:45 (thread 05, EFI change 7): stage `vermilion-trust`** = the same EFI plus
  `VermilionTrust.kext` 1.0.2 (after VermilionBoot) and **without `amfi=0x80`**. AMFI enforces library validation
  again; the kext lets only the cdhashes of the installed Vermilion code through. Verified: accelerated desktop as
  before, third-party apps no longer platform binaries. **Every plugin / translator / RADV install now needs
  `scripts/build/trust.sh` + an EFI rebuild** (new cdhashes), or WindowServer refuses the new build. `EFI/OC.prev` =
  change 6 (log-only, with `amfi=0x80`); the old everyday EFI: `~/vermilion-rollback/EFI-OC.awdl-20261122`. The everyday EFI is in
  `~/vermilion-rollback/EFI-OC.awdl-20261122` (= `efi/build.py build --stage vermilion`). Checks and rollback: TODO
  "Apple services, DRM and security".
- **Outside the EFI:** VermilionAccel (mode id 18) in `/Library/Extensions` (AuxKC). Metal plugin b149 in
  `/Library/GPUBundles/`. Boot daemon `~/gpu/phxboot-navi31.sh` with flags `sdma`, `cg`, `uclk` (456), `p0modes`,
  `wscdcopy`, `waitstep` (200), `vrrmin` (60), `autologin`. Hashes and rollback commands are in TODO
  "Display polish".
- **EFI partition staging area** `<ESP>/vermilion/`: on 2026-11-22 nothing was staged (no `run-once.sh`,
  `flags/` empty). The partition holds 95 OpenCore logs (`Target = 67`, about 24 MB).
- **Rollback copies:** `~/vermilion-rollback/`, `EFI/OC.prev`, `~/gpu/*.prev`.
- **Outside git, never to commit:** `~/.config/vermilion/` (SMBIOS serials, ACPI dump incl. VBIOS),
  `~/vermilion-captures/`, `build/`.

## Dependency map

### Boot chain

```
UEFI (BIOS 4005: dGPU = boot display, ReBAR on, CSM off)
 └─ firmware boot order: OpenCore first ─ (F8 -> Limine for Linux; never Linux through OpenCore)
     └─ OpenCore: DSDT NOP patches + SSDT-CPUR + SSDT-EC-USBX, AMD_Vanilla, quirks, boot-args, SMBIOS
         └─ XNU 25.6.0 + injected kexts (Lilu -> VirtualSMC / NVMeFix / RestrictEvents / AMFIPass, MtkWirelessWifi,
            AppleMCEReporterDisabler, VermilionBoot)
             └─ boot daemon (~/gpu/phxboot-navi31.sh, LaunchDaemon) + pre-hook esp_flags.sh (<ESP>/vermilion flags, run-once)
                 └─ GPU bring-up (SMU / PSP / DMUB from ~/gpu/fw) -> VermilionAccel (AuxKC) PhxAMDFB / NMXAccel
                     └─ WindowServer restart -> Metal plugin NVMTLDriver -> translator (AIR -> SPIR-V) -> RADV -> N48N user client
```

How each piece works: [ARCHITECTURE.md](ARCHITECTURE.md). Install paths: [INSTALL.md](INSTALL.md). Source layout:
[CLAUDE.md](../CLAUDE.md) "Layout".

### Configuration relationships (cause -> effect)

| Setting | Why | What depends on it / what it costs | Threads |
|---|---|---|---|
| `pci=0x8000000` | macOS otherwise sets 10-bit tags on the GPP7 root port | Wi-Fi and I225-V answer at all; must stay while either is used | 02, 04 |
| `DisableVariableWrite` | the installer hangs on Ryzen 7000 boards while writing NVRAM | macOS cannot write NVRAM: no startup-disk change, **no NVRAM panic log**, `phxfb-squat` must be set from Linux | 02, 05, 07 |
| `phxfb-squat` EFI variable (set from Linux) | every OpenCore boot is a squat boot (no phantom display) | **Reset NVRAM deletes it**: never Reset NVRAM | 00 |
| SIP `0x0A03` + VermilionTrust (was `amfi=0x80` + AMFIPass until 2026-11-22) | the ad-hoc signed plugin loads into platform processes; AMFI stays on for everything else | the cdhash list is compiled into the kext: **a new plugin / translator / RADV build needs a VermilionTrust + EFI rebuild** | 00, 05 |
| `SecureBootModel = Disabled` | injected kexts are unsigned | no Apple Secure Boot | 05 |
| macOS pinned to 26.7.1 | the plugin uses private Metal interfaces of that build | no macOS updates without a plan (`BlacklistAppleUpdate`) | 05, 07 |
| SMBIOS MacPro7,1 | upstream's choice | needs RestrictEvents (memory-slot warnings) and AppleMCEReporterDisabler (MCE panic on AMD); also decides iServices | 05 |
| `ResizeAppleGpuBars = 0` | Vermilion was validated with BAR0 256 MiB; ReBAR stays on for Linux | VRAM window size in the kext | 00 |
| AGDP board-id patch (pikera) | Navi cards hang without it on Tahoe; WhateverGreen breaks on 26 | graphics takeover | 00 |
| USB: 17 ports on `XH00` > 15 limit, no map | | Bluetooth and USB audio not enumerated: USB map (02) before BT / audio (04) | 02, 04 |
| SMCProcessorAMD (read-only CPU sensor) | CPU temperature / package power as SMC keys; AMDRyzenCPUPowerManagement rejected (replaces CPU PM, rewrites a P-state MSR; its Tahoe fork polls GPU registers) | CPU heat and power work (00.1, 07) can measure; don't add AMDRyzenCPUPowerManagement | 02, 00.1, 07 |
| Sleep off (`pmset`) | Vermilion has no GPU resume | sleep work only on a `-phxbringupoff` boot, with the owner | 07, 00 |
| Debug: `-v`, `debug=0x100`, `keepsyms=1`, OpenCore log `Target = 67` | bring-up diagnostics | slower boot, a log file per boot on the ESP; keep until the platform is stable, then revisit | 02, 07 |
| Linux shares the GPU and the ESP | dual boot | Linux -> macOS needs a **full power-off** (cold GPU); Limine files on the ESP must not be touched | all |

## Project goal (owner, 2026-11-22)

**The machine must behave like a fully supported, native Mac.** A lost real-Mac feature (AirDrop, FairPlay DRM,
privacy prompts, FileVault, ...) is a bug to fix, not a documented cost. Forking and patching kexts is welcome:
Vermilion is forked already, and the MtkWireless Wi-Fi / Bluetooth kexts may be forked for AirDrop (thread 04).
Prefer real fixes over blunt global relaxations such as `amfi=0x80` (thread 05's plan: decisions 2026-11-22).

## Cross-thread rules

The full rules are in [CLAUDE.md](../CLAUDE.md) "Hard rules" and in the owner's instructions for each thread. The
ones that cross workstreams:

1. **One EFI change per boot**, built and installed only with `efi/build.py install` (keeps `EFI/OC.prev`). Write
   the rollback steps into `.agents/TODO.md` before the boot.
2. **The ESP staging area is shared.** Before staging `<ESP>/vermilion/run-once.sh` or `flags/`, check what is
   already there. Never overwrite another thread's staged files. If two threads need the same boot, ask the owner
   which goes first.
3. **The GPU side** (Vermilion kexts, plugin, daemon, `~/gpu` flags, GPU registers / SMU / PSP / DAL messages)
   belongs to threads 00 / 00.1. Other threads don't touch it, and the GPU must keep working after every change.
4. **Boot routine:** Apple menu > Restart, OpenCore > Macintosh HD, **no Reset NVRAM**. Linux -> macOS is always a
   full power-off. Don't reboot while the owner is away.
5. **Research first, then propose, then act.** Measure before and after (see "Performance record").
6. **Root:** follow the owner's current instructions on sudo. Never write a password into the repo, docs, logs or
   commits.
7. **Commit or push only when the owner asks.** No personal data in git (CLAUDE.md rules 3 and 4).

## Known problems and open issues (index)

The details are in TODO. This list is for finding the right owner quickly.

| Issue | Since | Owner | Where |
|---|---|---|---|
| A failed DP link training at a login mode switch leaves a black image and blocks later switches until reboot (seen once, 2026-11-22 20:19) | 2026-11-22 | 00 | [Bluetooth findings, Boots](../.agents/findings/2026-11-22-bluetooth.md) |
| Ethernet: no link (driver bound; cable?) | 2026-11-19 | 04 | [audio / connectivity](../.agents/findings/2026-11-22-audio-connectivity-inventory.md) |
| Wi-Fi latency stalls every ~1.05 s (AWDL off-channel; cause proved) | 2026-11-19 | 04 | same |
| Onboard audio: analog outputs not published by `usbaudiod` | 2026-11-22 | 04 | same |
| Kernel panic at a restart, no panic report saved (`DisableVariableWrite` blocks the NVRAM log) | 2026-11-19 | 07 | TODO, [flicker / heat finding](../.agents/findings/2026-11-19-gpu-present-flicker-heat.md) |
| b138 froze the machine at 1280x1024 (cause open) | 2026-11-20 | 00 | [finding](../.agents/findings/2026-11-20-b138-freeze-and-squat-loop.md) |
| Tearing at 1440p60 with GPU present; input lag at 60 Hz | 2026-11-19 | 00.1 | TODO |
| Residual VRR flicker (the OLED panel's; accepted) | 2026-11-22 | 00 | TODO "Display polish" |
| A VRR floor change resets the display identity (colour profile) | 2026-11-22 | 00 | TODO "Display polish" |
| Metal reports the XT's name, 6 GiB working set; apps get no Mesa shader disk cache | 2026-11-22 | 00.1 | baseline |
| GPU idle 62-70 W board, no Linux comparison yet | 2026-11-19 | 00.1 | baseline |
| Installer partition `disk0s4` not deleted yet | 2026-11-19 | 03 | TODO Phase 1 |
| NVMe shown as "external" (cosmetic) | 2026-11-22 | 03 | [storage baseline](../.agents/findings/2026-11-22-storage-baseline.md) |
| WebKit's GPU process doesn't map the plugin (sandbox: executables only from system paths; INSTALL.md 5.6) | 2026-11-22 | 00 / 00.1 | [Apple services / security](../.agents/findings/2026-11-22-apple-services-security-inventory.md) |
| Music.app crashes in the Metal plugin: `nvmtl_vk_image_read` writes to an IOSurface whose memory (ImageIO's) is no longer mapped in the process; reproducible with "Drake - Jaded"; fixed 2026-11-23 by thread 05 (`nvmtl_plane_mapped`, plugin `33b9a186`, VermilionTrust 1.0.3), untested | 2026-11-22 | 00 | [Apple services / security](../.agents/findings/2026-11-22-apple-services-security-inventory.md) |
| Mock tests run on macOS only; no CI | 2026-11-18 | (unassigned) | TODO Phase 0 |

## What was tried and failed

The reasons behind each one are in decisions and findings. Don't repeat these without a new reason:

- **Wi-Fi:** AirportMtkWlan 2.1.6 loaded but never attached; BIOS ASPM off made no difference. The cause was
  10-bit tags (`pci=0x8000000`).
- **DSDT:** a recompiled, resolved DSDT (`fix_dsdt.py resolve`) loaded but still hung at CoreAnalyticsHub. It was
  replaced by in-place NOP patches.
- **Display:** b135's 240 -> 60 Hz switch went dark (fixed in b136). b138 froze at 1280x1024, and the recovery
  looped on `phxfb-squat`. The `ws-cdcopy-hold` tweak made no difference to tearing.
- **Heat:** wait step 200 us vs 50 us made no visible difference to the owner in a blind A/B test, but cut
  WindowServer wakeups about 10x, so 200 was kept.
- The run-once sysctl at boot fails with `unknown oid`, because PhxBringup registers its sysctls only with the first
  user client. Set it from the daemon instead.

## Performance record

The one place for baselines, bottlenecks and optimization decisions across all workstreams. Detailed measurements
stay in findings; each one gets one row here.

### Method (for every measurement)

Record each of these:

- the date and time;
- the exact installed state: EFI build / VermilionBoot build, PhxAccel, plugin, daemon flags, boot-args;
- the display mode: resolution, rate, HDR, Variable;
- the time since boot, and what else was running;
- the tool and its exact command;
- the number of samples and their spread (median and 99th percentile, not just the mean).

Compare only runs taken under the same conditions, and put each before / after pair in one finding. Prefer real
workloads, and say whether the path is hardware-accelerated or a fallback (software GL, CPU copy, CPU-only CL).

### Tools in the repo

| Tool | Measures |
|---|---|
| `tools/metaltest/metalbench.m` | off-screen Metal render / compute / submit round trip |
| `tools/metaltest/apiprobe.m` | what each graphics API reports to an app |
| `~/gpu/smumetrics.py` | the SMU metrics table: GPU clocks, power, temperatures, fan |
| `tools/phxctl/perflog.sh` | display / present counters to a log for one boot |
| `sysctl debug.phxtrust_stat` (VermilionTrust) | AMFI library-validation calls, refusals and overrides of Vermilion's code |
| `tools/sensors/cpusensors.py` | CPU package temperature (Tctl) and power, from SMCProcessorAMD's SMC keys |
| `tools/storage/nvmebench.sh` | NVMe / APFS throughput, latency, flush cost, SSD temperature, SMART before / after (fio, smartctl; writes 60-80 GB per run). Pitfalls in its README |
| `tools/storage/nvmeior.c` | IOReport `NVMe` group: power-state residency, Apple's bandwidth-throttle tiers |
| `tools/audio/audioprobe.m`, `uacdesc.c` | CoreAudio devices, formats, latency; I/O-cycle jitter, dropouts, overloads; USB audio descriptors (which device is which jack) |
| `powermetrics` (macOS) | per-task CPU and wakeups. Its CPU power, thermal and C-state figures are **meaningless on this AMD CPU** (Intel counters) |
| kext sysctls `debug.phxwait_lat`, `debug.phxvrr_stat`, ... | fence-wait latency, VRR, mode-set state ([XTX.md](XTX.md)) |

### Baselines

| Date | Area | Conditions | Result | Source |
|---|---|---|---|---|
| 2026-11-19 | GPU idle power | 1440p60 SDR desktop, b126 | 66-67 W board / 53 W socket | TODO "GPU idle power" |
| 2026-11-19 | Present path | 1440p60, CPU copy (b126) | 10-12.6 ms copy + 15-20 ms flip wait per frame, ~20-33 frames/s | [1440p finding](../.agents/findings/2026-11-19-1440p-gpu-present.md) |
| 2026-11-19 | Present path | 1440p60, GPU present | median 40 / max 59.5 frames/s | same |
| 2026-11-19 | Wi-Fi | 802.11ax 5 GHz 80 MHz | 780 Mbit/s link rate (not a throughput test) | wifi finding |
| 2026-11-22 | GPU idle | 1440p240 HDR, b150, 30 min after boot | 62-70 W board, 49-56 W socket; gfxclk avg ~180-210 MHz; uclk pinned 456, fclk 2301; hotspot 43-52 C, memory 64-70 C; fan stopped | [baseline](../.agents/findings/2026-11-22-gpu-performance-baseline.md) |
| 2026-11-22 | GPU load | `metalbench render 75 2000`, 2560x1440 off-screen | 192 frames/s, 4.82 ms GPU per frame; 389 W board, hotspot peak 88 C, fan 49 %; idle again within 3 s | same |
| 2026-11-22 | Submit round trip | `metalbench submit` | 101 us average per waited submit (median 89, p99 566), 6 us of it on the GPU | same |
| 2026-11-22 | CPU wakeups | idle desktop, `powermetrics --samplers tasks` | WindowServer 123-127 ms/s CPU, ~280 wakeups/s; all tasks ~1760 wakeups/s | same |
| 2026-11-22 | CPU package | idle desktop 1-4 min after boot, 1440p, b150; then 24 busy shell loops for 10 s | idle 53-57 C / 39-41 W; all-thread 85-87 C / 120-121 W | [CPU sensors](../.agents/findings/2026-11-22-cpu-sensors.md) |
| 2026-11-22 | Storage, APFS | `nvmebench.sh`, 1 h after boot, b150, 1440p desktop idle; SLC cache not rested | seq read 9.13 GB/s; seq write 9.54 GB/s fresh / 1.73 GB/s while folding; 4 KiB QD1 read 19.9 k IOPS (p50 48 us); 4 KiB 8 threads 133 k IOPS read; `F_FULLFSYNC` p50 1.50 ms (668/s) | [storage baseline](../.agents/findings/2026-11-22-storage-baseline.md) |
| 2026-11-22 | Storage, raw device | same run, `/dev/rdisk0s2` read-only | seq read 7.0 GB/s (the raw path's limit, not the drive's); 4 KiB QD1 20.9 k IOPS; 64 threads 558 k IOPS at 5 cores of system CPU | same |
| 2026-11-22 | Storage, sustained write | 180 s, 8 x 1 MiB, 886 GB | 9.6 GB/s for ~190 GB, then 1.46-9.6 GB/s (SLC folding); 54-65 C, no throttling | same |
| 2026-11-22 | Audio output | S/PDIF stream of the ALC4080, 48 kHz, silence, `audioprobe run 85 20 <frames>`, 1 h after boot, b150 | buffer 512 / 128 / 32 frames: reported latency 11.25 / 3.25 / 1.25 ms; callback period p99.9 within +-0.1 %; 0 overloads, 0 gaps in 60 s; device clock +24 ppm | [audio / connectivity](../.agents/findings/2026-11-22-audio-connectivity-inventory.md) |
| 2026-11-22 | Wi-Fi latency | router ping 10 Hz, 300 s, 5 GHz ch 52 / 80 MHz, RSSI -56 to -61 | AWDL up: p50 47, p90 308, p99 418, max 456 ms; AWDL down: avg 1.8, max 5 ms | same |
| 2026-11-22 | Wi-Fi latency, fixed | same, boot 22:23 with `amtk_awdl_offchan=0`, AWDL up, 60 s | avg 1.7, max 4.2 ms; 99.6 / 50.4 Mbit/s, idle 14.9 ms | same |
| 2026-11-22 | Internet over Wi-Fi | `networkQuality -s` | AWDL up: 64 / 32 Mbit/s, idle latency 143 ms; AWDL down: 101 / 48 Mbit/s, 14 ms (line-limited) | same |
| 2026-11-22 | Security / iServices daemons | 39 min after boot, b150, idle desktop; `ps -axo time,comm` | ~37 s CPU in total (trustd 9.7, cloudd 7.1, tccd 6.0, identityservicesd 2.7 s): ~1.6 % of one thread; no daemon crash loop | [Apple services / security](../.agents/findings/2026-11-22-apple-services-security-inventory.md) |
| 2026-11-23 | Quick Look / image decode | 13 min after boot, b150, 1440p240; `qlmanage -t -s 512` vs `sips -Z 512` of a 6016x6016 HEIC; no hardware decoder for any codec | Quick Look 12.2 s (11.4 s CPU); sips 1.6 s | [macOS integration](../.agents/findings/2026-11-23-macos-integration-inventory.md) |
| — | LAN throughput, Ethernet, sleep | — | **no baseline yet** (needs an `iperf3` peer, a cable) | 04, 07 |

### Known bottlenecks and limits

- CPU package temperature and power are measurable since 2026-11-22 (`tools/sensors/cpusensors.py`); per-core
  clocks aren't.
- Buffer objects live in system memory, not VRAM (the kext's comment, `phx_globals.cpp`). The cost for
  memory-bound loads is unmeasured.
- Fence waits poll: a 20 us spin, then 200 us steps. Interrupt waits exist but are off and unverified on the XTX.
- The UCLK hard minimum of 456 MHz is never released, and FCLK stays at 2301 MHz. Whether Linux idles lower is
  unknown.
- OpenGL apps run on the CPU (Apple Software Renderer).
- Debug boot settings (verbose boot, an OpenCore log file per boot) cost boot time *(unmeasured)*.
- Storage: random 4 KiB I/O is CPU-bound in Apple's NVMe stack (~9 us of kernel CPU per I/O, 558 k IOPS at 64
  threads). Every barrier / full fsync is a ~1.5 ms cache flush (the drive has no barrier support). APFS copy on
  write fragments randomly overwritten files: sequential reads fell from 9.1 to 2.0 GB/s. Write speed depends on
  the SLC cache, which may be smaller than it could be if Linux's LUKS doesn't pass discards *(unverified)*. How
  far macOS is from the drive's ceiling needs a Linux run of the same jobs.

### Optimization decisions

| Date | Decision | Measured reason | Source |
|---|---|---|---|
| 2026-11-19 | Fence-wait step 200 us (daemon flag `phxboot-waitstep`) | WindowServer wakeups 137-352/s vs 1673-2458/s at 50 us; no visible regression in a blind A/B | decisions.md |
| 2026-11-19 | GPU present via E3 (CoreDisplay's display surfaces) | fixed the flicker; WindowServer wakeups 700-1050/s from 1300-1400 | decisions.md |
| 2026-11-19 | GPU-present slots above BAR0 for frames over 8 MiB | removes the per-frame CPU copy at 1440p | decisions.md |
| 2026-11-22 | MtkWireless `amtk_awdl_offchan=0` | router ping max 456 -> 4 ms, internet 64 -> 100 Mbit/s, AWDL kept | [audio / connectivity](../.agents/findings/2026-11-22-audio-connectivity-inventory.md) |
| 2026-11-22 | VRR floor 60 Hz, ramp 5 % | the OLED flickers near 48 Hz; the ramp reduces desktop flicker | decisions.md |

## Shared-knowledge tooling (evaluated 2026-11-22)

- **Claude Code auto memory** (`~/.claude/projects/<the main checkout's path>/memory/`): every Claude
  session in any worktree of this repo loads the same folder. Only Claude sessions see it, it isn't versioned, and
  it holds personal and machine notes. **Use:** short owner preferences and pointers to this file. Never put
  project facts there that aren't also here.
- **T3 thread tools** (`t3_thread_list` / `t3_thread_read`): read another thread's history when needed. Thread 01
  used them to collect the workstream scopes.
- **Code graph:** none is installed. The tracked code is 823 files: C / C++ / Objective-C kexts and plugin, the
  Rust translator, shell tools and Mesa patches. The architecture is already documented, and agents find code with
  `grep` and the layout table. A graph tool (an MCP server or an indexer) would need installing and maintaining for
  little gain. **Not recommended now.** Revisit if the large refactors in TODO Phase 6 start.
- **Claude Docs connector:** available, but a doc outside git doesn't reach Codex or other providers and can't be
  versioned with the code. Not used.

## Not yet in `main`

- `ownback/phase1-wifi` `da78524`: the TODO note on Wi-Fi throughput drops (summarised above).

## Changelog of this file

- 2026-11-22: created by thread 01. Covers the full survey of the repo, the live EFI and the system.
- 2026-11-22: PR #2 merged (GPU baseline, `metalbench`, `apiprobe`); thread 02's inventory committed to `main`
  and its branch fast-forwarded to `main` (owner's OK).
- 2026-11-22: thread 04 audio / connectivity inventory, first audio and Wi-Fi baselines (`tools/audio/`, branch `ownback/audio-connectivity`).
- 2026-11-22: thread 03 storage inventory and first storage baseline (`tools/storage/`, branch `ownback/storage`).
- 2026-11-22: thread 05 Apple services / security inventory (branch `ownback/apple-services`).
- 2026-11-23: thread 06 macOS integration inventory (branch `ownback/macos-integration`).
