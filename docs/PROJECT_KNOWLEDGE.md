# Project knowledge: the XTX Hackintosh

The shared map of this project for every thread and every agent: **what is true now** on the reference machine. Read
it first, then only the files your scope needs. It doesn't copy other files; it says where each fact lives, how the
parts depend on each other, and what state the machine is in.

Last full survey: **2026-11-23**, thread 00 (Housekeeping), against the live machine (macOS 26.7.1 25G241, EFI change 8).

## How to use and update this file

One source of truth for each kind of fact:

| File | Holds |
|---|---|
| [ROADMAP.md](../ROADMAP.md) | the goal, the thread series 00-12 and its status, the rules, the known limits |
| [.agents/TODO.md](../.agents/TODO.md) | open tasks, grouped by series thread; check and rollback steps while a change is staged |
| **this file** | the machine's state per area, the known-good configuration and its rollback, dependencies, failed attempts, the performance record |
| [.agents/OWNER-TESTS.md](../.agents/OWNER-TESTS.md) | tests only the owner can do |
| [.agents/findings/](../.agents/findings/) / [decisions.md](../.agents/decisions.md) / [runbooks.md](../.agents/runbooks.md) | evidence (dated, not edited later) / reasons (append-only) / procedures |

- **What goes here:** a change to the known-good configuration (every EFI change, with the thread's number), a
  subsystem changing state, a new cross-thread dependency, a baseline. One or two lines, a date, a link to the
  finding. Anything not tested on the machine is marked *(unverified)*.
- This file lives on `fork/main` and changes through each thread's PR.
- No serials, MLB, ROM, UUIDs, MAC addresses, passwords or e-mail addresses here ([CLAUDE.md](../CLAUDE.md) rules 3, 4).

## The machine

Hardware: [REFERENCE-MACHINES.md](REFERENCE-MACHINES.md): ASUS ROG STRIX B650E-I (BIOS 4005), Ryzen 9 7900X3D, 48 GB,
XFX RX 7900 XTX rev `0xC8`, LG 27GS95QE-B on DP, MT7922 Wi-Fi / Bluetooth, I225-V, one NVMe (Corsair MP700 PRO,
Phison E26) shared with Linux.

Disk (macOS view): `disk0s1` EFI (2 GB FAT32, shared with Linux's Limine), `disk0s2` Linux LUKS, `disk0s3` APFS
container (107.4 GB; Macintosh HD + Data). The installer partition `disk0s4` ("Install macOS Tahoe", 23.6 GB HFS+)
was deleted on the owner's request on 2026-11-23 06:04 (the Data volume was full: 125 MB free) and the container grown
into its space (`diskutil eraseVolume free none disk0s4`, `diskutil apfs resizeContainer disk0s3 0`; 22 GiB free
after). There is no 26.7.1 reinstall path on the disk any more (TODO 10).

## Machine state (2026-11-23, verified on the machine unless marked)

The "Thread" column is the series thread in [ROADMAP.md](../ROADMAP.md) that owns the next step.

| Area | State | Evidence | Thread |
|---|---|---|---|
| Boot | OpenCore 1.0.8, stage `vermilion-trust` (EFI change 15 = change 8 with VermilionBoot b155, VermilionTrust 1.0.4); live `EFI/OC` = this repo's build (below) | change 8 rebuilt and compared 2026-11-23; changes 9-15 installed with `efi/build.py install` | 09 |
| GPU acceleration (Metal) | works: the device reports Metal 3; one render and one compute shader exercised. Ray tracing, MetalFX, BC textures, function pointers: flags only, never exercised | [GPU baseline](../.agents/findings/2026-11-22-gpu-performance-baseline.md) | 02 |
| Display 2560x1440 | 60 / 120 / 144 / 240 Hz, Variable 60-240 with the ramp, 10-bit HDR, HDR + Variable, from System Settings; the login screen comes up at the saved rate. 10-bit banding not checked | [XTX.md](XTX.md) | 01 |
| Other LG modes | fixed rates only (1080p60/120, 1280x1024@75, 1024x768, 800x600, 640x480), no VRR / HDR (owner's choice, decisions 2026-11-22) | same | 01 |
| Display identity, scaling | reports 591 x 332 mm since 2026-11-23 04:15 (PhxAccel gives every mode the EDID's size; before: 903 x 508 mm = 2560 x 1440 at 72 dpi). macOS lists no HiDPI / scaled modes | [GPU stability](../.agents/findings/2026-11-23-gpu-stability.md) sections 3 and 8 | 01 |
| Failed DP link training | once on b150 (2026-11-22 20:19, login switch to 1440p60 HDR): black screen, switching locked until reboot; the daemon still said `SUCCESS`. Since 2026-11-23 (b151-b155): Linux's training retries, a failed from plan runs once more, a switch whose last link window needed a retry runs again, and the daemon reports `DARK` for a latched engine; about a dozen login switches at 1440p60 HDR landed. The LG still misses the first AUX access after D3 at times (the retry and the re-switch cover it) | [Bluetooth finding, "Boots"](../.agents/findings/2026-11-22-bluetooth.md) (seen during that boot), [GPU stability](../.agents/findings/2026-11-23-gpu-stability.md) | 01 |
| OpenGL / OpenCL / Vulkan | Apple Software Renderer; CPU-only OpenCL; no Vulkan loader | GPU baseline | 03 |
| Video decode | no hardware decoder for any codec (VCN off): HEIC, Quick Look, Photos, video on the CPU | GPU baseline, macOS integration | 03 |
| Shader cache | apps started normally have Mesa's disk cache (`~/.cache/mesa_shader_cache` fills). Processes started from T3 Code inherit a `MESA_SHADER_CACHE_DIR` pointing at root's cache and get `Permission denied` | thread 00.1's final report (2026-11-23) | 02 |
| Wi-Fi MT7922 | MtkWirelessWifi 1.1.1 + `pci=0x8000000`, 802.11ax 2x2, 867 Mbit/s link, WPA2 / WPA3. AWDL stalls fixed by `amtk_awdl_offchan=0` (router ping max 4 ms, was 456 ms) | [wifi](../.agents/findings/2026-11-19-wifi-mt7922.md), [audio / connectivity](../.agents/findings/2026-11-22-audio-connectivity-inventory.md) | 07 |
| Bluetooth MT7922 (USB `0489:e0e2`) | MtkWirelessBT 1.1.1 + BlueToolFixup 2.7.2 + 2 NVRAM variables; AirPods pair and play; BLE advertising works (Handoff) | [Bluetooth](../.agents/findings/2026-11-22-bluetooth.md) | 07 |
| Continuity | Handoff and AirPlay iPhone -> Mac work. **AirDrop fails** (Mac not visible): with `amtk_awdl_offchan=0` and the router on DFS channel 52 the Mac never transmits in AWDL (source + observed channel sequence). Universal Clipboard fails (probably the same link *(unverified)*); Continuity Camera over USB gets no frames | audio / connectivity | 07 |
| Audio (ASUS USB `0b05:1a5c`, ALC4080, UAC2, `usbaudiod`) | the line-out works (owner). The codec offers only jacks plugged in **at boot** (jack sensing at `usbaudiod` start), plus S/PDIF and one input; hot-plug untested. Volume = codec hardware gain, 0 dB at 100 % | same | 11 |
| DP / HDMI audio, HDMI output | not implemented anywhere in the stack | | 04 |
| Ethernet I225-V | Apple's DriverKit E1000 bound as **`en0`** (built-in), up to 2500BASE-T; **no link ever tested** (no cable) | same | 11 |
| USB | USBToolBox 1.2.0 + `efi/kexts/UTBMap.kext`: all 8 rear ports (USB 2 halves walked), internal audio (port 11) and Bluetooth (port 16). USB 3 halves untested. Why macOS didn't create ports 11 / 16 / 17 unmapped (`_UPC` or the 15-port limit) is unproven | [USB map](../.agents/findings/2026-11-22-usb-map.md) | 11 |
| CPU sensors | SMCProcessorAMD 1.0.1 (read-only): package temperature and power; per-core clock keys not published | [CPU sensors](../.agents/findings/2026-11-22-cpu-sensors.md) | 02 |
| CPU power management | AMD_Vanilla (`0x0C` cores), `DummyPowerManagement`. Never above 5.15 GHz, idle package ~40 W: **Linux the same** (5.16 GHz, 37.8 W, BIOS all Auto), so no idle kext | [sleep / power](../.agents/findings/2026-11-23-sleep-power-validation-inventory.md) | 02 |
| Fans | GPU fan from SMU metrics; no motherboard / CPU fan readings (no Super I/O plugin) | same | 02 |
| NVMe | Apple's driver + NVMeFix 1.1.3 (APST on), Gen 5 x4, TRIM capability yes (runtime TRIM not seen), SMART clean, no thermal throttling. Shown as "external" (cosmetic) | [storage](../.agents/findings/2026-11-22-storage-baseline.md) | 10 |
| APFS / backups | System volume sealed (booted from its snapshot), Data separate, FileVault off; no Time Machine destination | same | 11 |
| Sleep | **off on purpose** (`pmset` sleep 0, displaysleep 0, hibernatemode 0) and **manual sleep blocked** since 2026-11-23 00:58 (`pmset -a disablesleep 1`; undo `sudo pmset -a disablesleep 0`): Vermilion has no GPU resume path. Display sleep tested 2026-11-23: the picture comes back, but the signal stays up and the LG never goes to standby (TODO 05). S3 untested | sleep / power | 05 |
| Shutdown / restart | clean: 15 boots with previous-shutdown cause 5, SMART "Unsafe Shutdowns" unchanged over 6+ restarts (cold shutdown not checked); restart to accelerated desktop ~67 s | same | 11 |
| Panics | one at a restart (2026-11-19 22:44), cause unknown; no report is ever saved (`DisableVariableWrite` blocks the NVRAM panic log: from the config, not watched failing) | [flicker / heat](../.agents/findings/2026-11-19-gpu-present-flicker-heat.md) | 01 |
| NVRAM | read-only from macOS (`DisableVariableWrite`, against the Ryzen 7000 installer hang) | efi/machines | 01 |
| SIP / AMFI / Gatekeeper | SIP custom `0x0A03` (system volume still sealed). **AMFI enforcing** since 2026-11-22 23:45: no `amfi=0x80`; VermilionTrust lets only Vermilion's cdhashes through (`debug.phxtrust_stat`: `mode=enforce list=6 denied_unlisted=0`); third-party apps no longer platform binaries. AMFIPass still loaded (need unknown). Gatekeeper on (status only; no app assessed). XProtect data updates on (`ConfigDataInstall = 1`); macOS and Rapid Security Responses pinned | [Apple services / security](../.agents/findings/2026-11-22-apple-services-security-inventory.md) | 09 |
| Privacy prompts (TCC) | the Screen Recording prompt appeared and was granted (2026-11-23); camera / microphone prompts untested | macOS integration | 09 |
| iServices | APNs connected; iMessage and FaceTime **registered** (IDS HTTP 200; nothing sent or called yet); iCloud signed in; SMBIOS well-formed (MacPro7,1), ROM = the I225-V's MAC; `en0` built-in Ethernet since 2026-11-23 00:01 | Apple services / security | 09 |
| DRM | Apple Music (FairPlay 1.x) plays full songs since 2026-11-23 (it needed a built-in `en0`); the Music crash in the plugin is fixed (plugin `33b9a186`, owner: plays "consistently"). Apple TV films and Safari FairPlay video fail (`-12906`, no decoder for the protected stream): a known limit (ROADMAP). Widevine untested | same | 09 |
| Input, screenshots, Spotlight | keyboard and mouse work (mouse 1 kHz); `screencapture` returns the real GPU desktop; Spotlight indexes and answers | macOS integration | 11 |
| Virtualization | `kern.hv_support = 0`: no Hypervisor / Virtualization.framework on AMD (a known limit) | same | — |
| Sharing, printers, Xcode | off / not set up / not installed | same | 11 |
| FileVault / login | FileVault off, Secure Token on; automatic login by the daemon (`/etc/kcpassword`), which FileVault would end | same | 06 |

## Known-good configuration (2026-11-23)

The configuration the owner uses every day. Change it only through the active thread, one EFI change per boot, and
add each change here with the thread's number.

- **EFI: `efi/build.py build` (machine `b650e-i-xtx`, default stage `vermilion-trust`) = EFI change 15** (change 8
  with VermilionBoot b155, thread 01; VermilionTrust 1.0.4, thread 02; 2026-11-23). On 2026-11-23 thread 00 rebuilt
  change 8's VermilionBoot and VermilionTrust from source (both byte-identical to the live ones) and the EFI with placeholder serials: every file
  equalled the live `EFI/OC`, and `config.plist` was identical apart from `PlatformInfo` (the serials). Changes 9-15
  were built and installed from the repo. **The repo is the source of truth for the EFI.**
  - OpenCore 1.0.8. Kexts, in load order: Lilu 1.7.2, VirtualSMC 1.3.8, NVMeFix 1.1.3, RestrictEvents 1.1.6,
    AppleMCEReporterDisabler, MtkWirelessWifi 1.1.1, USBToolBox 1.2.0, UTBMap, SMCProcessorAMD 1.0.1, BlueToolFixup
    2.7.2, MtkWirelessBT 1.1.1, AMFIPass 1.4.1, VermilionBoot **b155** (sha256 `f95b4baf`), VermilionTrust **1.0.4**
    (`63481dc4`; 8 cdhashes, among them RADV `801ae178` and the plugin `3be1ec3c`). Pinned in [`efi/components.json`](../efi/components.json).
  - ACPI: `SSDT-CPUR-B650EI`, `SSDT-EC-USBX-B650EI`, 5 DSDT NOP patches (derived for BIOS 4005; `efi/build.py`
    refuses another version).
  - NVRAM (Apple GUID, set by OpenCore every boot): `bluetoothInternalControllerInfo`, `bluetoothExternalDongleFailed`.
  - boot-args: `-v keepsyms=1 debug=0x100 pci=0x8000000 amtk_awdl_offchan=0 ipc_control_port_options=0 phxdebug=1`;
    `csr-active-config` `030A0000`. SMBIOS MacPro7,1.
  - `EFI/OC.prev` = change 13: the same EFI with VermilionBoot b154 (`78f560cc`). `EFI/OC.bad` = change 12 (the
    NVRAM test stage, inconclusive). Change 7 (VermilionTrust 1.0.2 `2e4f3562`, which trusts only the previous plugin
    build) is kept as `~/vermilion-rollback/EFI-OC.change7-20261122`. To boot change 7, first restore that plugin: `sudo cp ~/vermilion-rollback/plugin-20261123/NVMTLDriver
    /Library/GPUBundles/NVMTLDriver.bundle/Contents/MacOS/`.
  - **Rollback of the EFI** (from macOS): `sudo diskutil mount disk0s1`, then in `/Volumes/NO NAME/EFI`:
    `sudo mv OC OC.bad && sudo mv OC.prev OC`, restart (while `OC.bad` exists, as it does since change 12, pick
    another name: `mv` would put `OC` inside it). If macOS doesn't boot: hold power, power on, F8 > Limine, the
    same renames under `/boot/EFI`, **power off fully**, power on.
  - Older EFIs (contain serials, never publish): `~/vermilion-rollback/EFI-OC.awdl-20261122` (stage `vermilion`, with
    `amfi=0x80`, no VermilionTrust: the fallback before change 7), `EFI-OC.bt-20261122` (Bluetooth, before
    `amtk_awdl_offchan=0`), `EFI-OC.prev-before-awdl-20261122` (CPU sensors, before Bluetooth).
- **EFI change log** (one boot each):

  | # | Date | Thread (old numbering) | Change |
  |---|---|---|---|
  | 1 | 2026-11-22 20:00 | 02 | USB map (USBToolBox + UTBMap) |
  | 2 | 2026-11-22 20:10 | 02 | SMCProcessorAMD 1.0.1 |
  | 3 | 2026-11-22 20:27 | 02 | Bluetooth (MtkWirelessBT, BlueToolFixup, 2 NVRAM variables) |
  | 4 | 2026-11-22 22:23 | 04 | `amtk_awdl_offchan=0` |
  | 5 | 2026-11-22 23:31 | 05 | `vermilion-trustlog`, VermilionTrust 1.0.0 (log only, `amfi=0x80` kept) |
  | 6 | 2026-11-22 23:40 | 05 | VermilionTrust 1.0.1 (log only) |
  | 7 | 2026-11-22 23:45 | 05 | `vermilion-trust`: VermilionTrust 1.0.2 enforcing, `amfi=0x80` removed |
  | 8 | 2026-11-23 00:20 | 05 | VermilionTrust 1.0.3 (trusts the Music-fix plugin and the previous one) |
  | 9 | 2026-11-23 03:11 | 01 | VermilionBoot b151 (`a23a2766`): Linux's link-training retries, a failed from plan runs once more. Same boot, outside the EFI: the daemon's display check |
  | 10 | 2026-11-23 03:42 | 01 | VermilionBoot b152 (`9ca53007`): each link window's attempts and timing in `debug.phxp0odm_stat`. Same boot: the daemon logs each engine run |
  | 11 | 2026-11-23 03:51 | 01 | VermilionBoot b153 (`bd3f72fa`): the HDR plans wait Linux's 100-180 ms before their second link window |
  | 12 | 2026-11-23 ~04:00 | 01 | `vermilion-trust-nvram` (`DisableVariableWrite` false), as a test; rolled back from Linux at an unknown time (kept as `EFI/OC.bad`), so whether it ever booted is unknown: NVRAM write test still open |
  | 13 | 2026-11-23 04:36 | 01 | VermilionBoot b154 (`78f560cc`) on change 11's config: a switch whose last link window needed a retry runs once more (the LG stayed black after such a retry at 04:15) |
  | 14 | 2026-11-23 04:52 | 01 | VermilionBoot b155 (`f95b4baf`): drm's 32-try rule for the trainers' DPCD accesses; the live switch `debug.phxp0odm_dbuf` (0 at boot) |
  | 15 | 2026-11-23 06:31 (first boot 07:10, after the Linux run; verified: `list=8 denied_unlisted=0`, `profile navi31`, Metal "AMD Radeon RX 7900 XTX", no leaked environment) | 02 | VermilionTrust 1.0.4 (`63481dc4`) for RADV with the XTX profile (patches 0018-0019, `801ae178`) and the plugin without the environment leak (`3be1ec3c`), both installed outside the EFI the same minute |
  | 16 | 2026-11-23 (installed; not booted yet) | 02 B | VermilionBoot b156 (`d8f5948a`): VRAM-backed BOs, opt-in per process, off at boot |

  Before change 1: VermilionBoot b149 -> b150 (old thread 00, 2026-11-22). History:
  [bring-up log](../.agents/findings/2026-11-19..23-bringup-log.md).
- **Outside the EFI:**
  - VermilionAccel with mode id 18 and the EDID's physical size in every mode (thread 01) in `/Library/Extensions`
    (AuxKC; sha256 `26685255`, since 2026-11-23 04:15). Rollback: `~/vermilion-rollback/VermilionAccel.kext.pre-thread01`
    (`c6341e87`), then `~/vermilion-rollback/VermilionAccel.kext.pre-hdrvrr` (procedure: [INSTALL.md](INSTALL.md) section 4, "To update
    the kext": the **Allow** click rebuilds the AuxKC, not the restart).
  - Metal plugin in `/Library/GPUBundles/NVMTLDriver.bundle`: cdhash `3be1ec3c` since EFI change 15 (thread 02: its
    two environment variables no longer reach child processes). Before it: NVMTLDriver sha256 `6840fbe3`, cdhash
    `33b9a186` (the old thread 05's Music fix on top of b149), still in the trust list. Older rollbacks:
    `~/vermilion-rollback/plugin-20261123/` (b149), then `NVMTLDriver.b139`.
  - RADV `/Library/GPUBundles/nvmtl/libvulkan_radeon.dylib`: cdhash `801ae178` since EFI change 15 (Mesa patches
    0001-0019: the XTX profile by PCI revision). Before it: `8054fbe4` (0001-0017, the XT profile), still in the trust
    list.
  - Boot daemon `~/gpu/phxboot-navi31.sh` (LaunchDaemon `local.phxboot`; equal to the repo, thread 02), flags
    `sdma`, `cg`, `uclk` (456; released at the desktop by `idledpm`), `p0modes`, `wscdcopy`, `waitstep` (200), `vrrmin`
    (60), `irqwait`, `idledpm`, `idledpm-dcf` (thread 02, since 2026-11-23 07:51), `autologin`, `auto` (re-arms
    `phxfb-squat` at SUCCESS; created 2026-11-23 for the NVRAM test). Thread 01's version (display check `DARK`, one
    restart; an `engine run` record per run) plus thread 02's `irqwait_boot` and `idledpm_boot`. Previous daemons:
    `~/vermilion-rollback/phxboot-navi31.sh.pre-thread02`, `~/vermilion-rollback/phxboot-navi31.sh.pre-thread01`,
    `~/gpu/phxboot-navi31.sh.prev`, `~/vermilion-rollback/phxboot-navi31.sh.pre-vrrmin`.
  - `/Library/GPUBundles/nvmtl/ws-gpu-present` and `ws-gpu-present-async` (GPU present on).
  - Network: `NetworkInterfaces.plist` / `preferences.plist` with Ethernet `en0`, Wi-Fi `en1` (2026-11-23; backup
    `~/vermilion-rollback/netcfg-20261122/`).
  - `pmset -a disablesleep 1`; `ConfigDataInstall = 1`.
- **Every plugin, translator or RADV install needs `PHXTRUST_KEEP=1 scripts/build/trust.sh` + an EFI rebuild and
  install** (new cdhashes), or AMFI refuses the new build in WindowServer: [runbooks](../.agents/runbooks.md),
  "Deploying a plugin, translator or RADV build".
- **ESP staging area** `<ESP>/vermilion/`: only `flags/` (empty); no `run-once.sh`. The daemon's pre-hook
  (`esp_flags.sh`) reads nothing else there. Thread 00 deleted the old `handoff/`, `payload/`, `capture/`, `daemon*/`,
  `perf/`, `wifi/` and all but the newest 5 OpenCore logs on 2026-11-23. OpenCore writes a log at every boot
  (`Misc > Debug > Target` 67); the file name is the firmware clock in **UTC** (`opencore-2026-11-23-041020.txt` = the
  05:10 CET boot), which made it look as if logging had stopped.
- **Rollback copies:** `~/vermilion-rollback/` (pruned 2026-11-23 to: the 3 EFIs above, `plugin-20261123`,
  `NVMTLDriver.b139`, `VermilionBoot.kext.b149`, `VermilionAccel.kext.pre-hdrvrr`, `AuxiliaryKernelExtensions.kc.0222`,
  `phxboot-navi31.sh.pre-vrrmin`, `netcfg-20261122`; thread 01 added `EFI-OC.change7-20261122`,
  `EFI-OC.change8-20261123`, `EFI-OC.change11-20261123`, `EFI-OC.change12-nvram-20261123`, `VermilionBoot.kext.b151`,
  `.b152`, `.b154`, `VermilionAccel.kext.pre-thread01`, `phxboot-navi31.sh.pre-thread01`), `EFI/OC.prev`,
  `~/gpu/*.prev`.
- **Outside git, never to commit:** `~/.config/vermilion/` (SMBIOS serials, ACPI dump incl. the VBIOS),
  `~/vermilion-captures/` (display captures, EDIDs, crash reports), every `build/` folder (built EFIs contain the
  serials).

## Dependency map

### Boot chain

```
UEFI (BIOS 4005: dGPU = boot display, ReBAR on, CSM off)
 └─ firmware boot order: OpenCore first ─ (F8 -> Limine for Linux; never Linux through OpenCore)
     └─ OpenCore: DSDT NOP patches + SSDT-CPUR + SSDT-EC-USBX, AMD_Vanilla, quirks, boot-args, SMBIOS, NVRAM
         └─ XNU 25.6.0 + injected kexts: Lilu -> VirtualSMC (+ SMCProcessorAMD), NVMeFix, RestrictEvents, AMFIPass,
            VermilionTrust; AppleMCEReporterDisabler; MtkWirelessWifi; USBToolBox + UTBMap; BlueToolFixup +
            MtkWirelessBT; VermilionBoot
             └─ boot daemon (~/gpu/phxboot-navi31.sh, LaunchDaemon) + pre-hook esp_flags.sh (<ESP>/vermilion flags, run-once)
                 └─ GPU bring-up (SMU / PSP / DMUB from ~/gpu/fw) -> VermilionAccel (AuxKC) PhxAMDFB / NMXAccel
                     └─ WindowServer restart -> Metal plugin NVMTLDriver (AMFI: VermilionTrust's cdhash list)
                         -> translator (AIR -> SPIR-V) -> RADV -> N48N user client
```

How each piece works: [ARCHITECTURE.md](ARCHITECTURE.md). Install paths: [INSTALL.md](INSTALL.md). Source layout:
[CLAUDE.md](../CLAUDE.md) "Layout".

### Configuration relationships (cause -> effect)

| Setting | Why | What depends on it / what it costs | Thread |
|---|---|---|---|
| `pci=0x8000000` | macOS otherwise sets 10-bit tags on the GPP7 root port | Wi-Fi and the I225-V answer at all; must stay while either is used | 07 |
| `amtk_awdl_offchan=0` | AWDL's off-channel windows stalled Wi-Fi for ~400 ms every ~1 s | AirDrop fails while the router is on a channel AWDL doesn't use (52 here) | 07 |
| `DisableVariableWrite` | the installer hangs on Ryzen 7000 boards while writing NVRAM | macOS cannot write NVRAM: no startup-disk change, **no panic report** *(inferred from the config)*, `phxfb-squat` must be set from Linux | 01 |
| `phxfb-squat` EFI variable (set from Linux) | every OpenCore boot is a squat boot (no phantom display) | **Reset NVRAM deletes it**: never Reset NVRAM | 06 |
| SIP `0x0A03` + VermilionTrust (was `amfi=0x80` until 2026-11-22) | the ad-hoc signed plugin loads into platform processes; AMFI stays on for everything else | the cdhash list is compiled into the kext: **a new plugin / translator / RADV build needs `trust.sh` + an EFI rebuild**. Its hook offsets were read from 25G241 only: **no macOS update** until thread 09's layout check | 09, 06 |
| `SecureBootModel = Disabled` | injected kexts are unsigned; VermilionAccel is in the AuxKC | no Apple Secure Boot | 09 |
| macOS pinned to 26.7.1 | the plugin uses private Metal interfaces of that build | `BlacklistAppleUpdate`; a known limit | 09 |
| SMBIOS MacPro7,1 | upstream's choice | needs RestrictEvents (memory-slot warnings) and AppleMCEReporterDisabler (MCE panic on AMD); decides iServices | 09 |
| `en0` = built-in Ethernet | FairPlay (Apple Music) and iServices take the identity from `en0` | an iPhone / iPad plugged in at first boot took `en0` and broke Music (fixed 2026-11-23) | 10 |
| `ResizeAppleGpuBars = 0` | Vermilion was validated with BAR0 256 MiB; ReBAR stays on for Linux | the VRAM window size in the kext | 02 |
| AGDP board-id patch (pikera) | Navi cards hang without it on Tahoe; WhateverGreen breaks on 26 | graphics takeover | — |
| USB map (UTBMap) | macOS didn't create the audio and Bluetooth ports unmapped (`_UPC`, or the controller's 17 ports over macOS's 15: unproven) | Bluetooth (port 16) and USB audio (port 11) enumerate only with it; per board | 10 |
| SMCProcessorAMD (read-only) | CPU temperature / package power as SMC keys; AMDRyzenCPUPowerManagement rejected (replaces CPU PM, rewrites a P-state MSR; its Tahoe fork polls GPU registers) | CPU heat and power are measurable; don't add AMDRyzenCPUPowerManagement | 02 |
| Sleep off + `disablesleep 1` | no GPU resume | sleep work only on a `-phxbringupoff` boot, with the owner | 05 |
| Debug: `-v`, `debug=0x100`, `keepsyms=1`, OpenCore log `Target = 67` | bring-up diagnostics | slower boot *(unmeasured)*; revisit once stable | 10 |
| Linux shares the GPU and the ESP | dual boot | Linux -> macOS needs a **full power-off** (cold GPU); Limine's files on the ESP must not be touched | all |

## Cross-thread rules

The full rules are in [ROADMAP.md](../ROADMAP.md) "Rules for every thread" and [CLAUDE.md](../CLAUDE.md) "Hard rules".
The ones that touch the machine:

1. **One EFI change per boot**, built and installed only with `efi/build.py install` (keeps `EFI/OC.prev`). Rollback
   steps go into `.agents/TODO.md` before the boot. Only the active thread installs to the ESP.
2. **The ESP staging area is shared.** Before staging `<ESP>/vermilion/run-once.sh` or `flags/`, check what is there.
3. **The GPU must keep working after every change.** Vermilion's kexts, plugin, daemon, `~/gpu` flags and any GPU
   register, SMU, PSP or DAL message belong to the GPU thread of the series that is active.
4. **Boot routine:** Apple menu > Restart, OpenCore > Macintosh HD, **no Reset NVRAM**. Linux -> macOS is always a full
   power-off. Don't reboot while the owner is away.
5. **Root:** follow the owner's current instructions on sudo. Never write a password into the repo, docs, logs or
   commits. No personal data in git.

## What was tried and failed

The reasons are in decisions and findings. Don't repeat these without a new reason:

- **Wi-Fi:** AirportMtkWlan 2.1.6 loaded but never attached; BIOS ASPM off made no difference. The cause was 10-bit
  tags (`pci=0x8000000`).
- **DSDT:** a recompiled, resolved DSDT (`fix_dsdt.py resolve`) loaded but still hung at CoreAnalyticsHub. Replaced
  by in-place NOP patches.
- **Display:** b135's 240 -> 60 Hz switch went dark (fixed in b136). b138 froze at 1280x1024, and the recovery looped
  on `phxfb-squat`. The `ws-cdcopy-hold` tweak made no difference to tearing.
- **Heat:** wait step 200 us vs 50 us made no visible difference in a blind A/B test, but cut WindowServer wakeups
  about 10x, so 200 was kept.
- The run-once sysctl at boot fails with `unknown oid`: PhxBringup registers its sysctls only with the first user
  client. Set it from the daemon instead.
- **AMFIPass alone** doesn't let the ad-hoc plugin into platform processes (VermilionTrust README).

## Performance record

The one place for baselines, bottlenecks and optimization decisions. Detailed measurements stay in findings; each
gets one row here.

### Method (for every measurement)

Record: the date and time; the exact installed state (EFI change / VermilionBoot build, PhxAccel, plugin, daemon
flags, boot-args); the display mode (resolution, rate, HDR, Variable); the time since boot and what else ran; the tool
and its exact command; the number of samples and their spread (median and 99th percentile, not just the mean).
Compare only runs under the same conditions, put each before / after pair in one finding, prefer real workloads, and
say whether the path is accelerated or a fallback.

### Tools in the repo

| Tool | Measures |
|---|---|
| `tools/metaltest/metalbench.m` | off-screen Metal render / compute / submit round trip |
| `tools/metaltest/apiprobe.m` | what each graphics API reports to an app |
| `~/gpu/smumetrics.py` (`tools/phxctl/`) | the SMU metrics table: GPU clocks, power, temperatures, fan |
| `tools/phxctl/perflog.sh` | display / present counters to a log for one boot |
| `sysctl debug.phxtrust_stat` (VermilionTrust) | AMFI library-validation calls, refusals and overrides of Vermilion's code |
| `tools/sensors/cpusensors.py` | CPU package temperature (Tctl) and power, from SMCProcessorAMD's SMC keys |
| `tools/sensors/cpuclk.c` | effective core clock from dependent adds (macOS and Linux; affinity tags or `taskset` to pick a CCD) |
| `tools/validation/bootcheck.sh` | one read-only report per boot: shutdown cause, new crash / stall / panic reports, daemon result, sleep settings, display mode, SMART (with sudo) |
| `tools/storage/nvmebench.sh` | NVMe / APFS throughput, latency, flush cost, SSD temperature, SMART (fio, smartctl; writes tens of GB per run: don't run it routinely). Pitfalls in its README |
| `tools/storage/nvmeior.c` | IOReport `NVMe` group: power-state residency, Apple's bandwidth-throttle tiers |
| `tools/audio/audioprobe.m`, `uacdesc.c` | CoreAudio devices, formats, latency; I/O-cycle jitter, dropouts; USB audio descriptors |
| `tools/usbmap/usbwatch.py` | USB ports as macOS sees them; a live port walk |
| `powermetrics` (macOS) | per-task CPU and wakeups. Its CPU power, thermal and C-state figures are **meaningless on this AMD CPU** |
| kext sysctls `debug.phxwait_lat`, `debug.phxvrr_stat`, ... | fence-wait latency, VRR, mode-set state ([XTX.md](XTX.md)) |

### Baselines

Linux reference: [linux-reference](../.agents/findings/2026-11-23-linux-reference.md) (2026-11-23, one Omarchy boot); the
side-by-side reading is in [perf / power](../.agents/findings/2026-11-23-perf-power.md) section 8.

| Date | Area | Conditions | Result | Source |
|---|---|---|---|---|
| 2026-11-19 | GPU idle power | 1440p60 SDR desktop, b126 | 66-67 W board / 53 W socket | [bring-up log](../.agents/findings/2026-11-19..23-bringup-log.md) |
| 2026-11-19 | Present path | 1440p60, CPU copy (b126) | 10-12.6 ms copy + 15-20 ms flip wait per frame, ~20-33 frames/s | [1440p finding](../.agents/findings/2026-11-19-1440p-gpu-present.md) |
| 2026-11-19 | Present path | 1440p60, GPU present | median 40 / max 59.5 frames/s | same |
| 2026-11-22 | GPU idle | 1440p240 HDR, b150, 30 min after boot | 62-70 W board, 49-56 W socket; gfxclk avg ~180-210 MHz; uclk pinned 456, fclk 2301; hotspot 43-52 C, memory 64-70 C; fan stopped | [baseline](../.agents/findings/2026-11-22-gpu-performance-baseline.md) |
| 2026-11-22 | GPU load | `metalbench render 75 2000`, 2560x1440 off-screen | 192 frames/s, 4.82 ms GPU per frame; 389 W board, hotspot peak 88 C, fan 49 %; idle again within 3 s | same |
| 2026-11-22 | Submit round trip | `metalbench submit` | 101 us average per waited submit (median 89, p99 566), 6 us of it on the GPU | same |
| 2026-11-22 | CPU wakeups | idle desktop, `powermetrics --samplers tasks` | WindowServer 123-127 ms/s CPU, ~280 wakeups/s; all tasks ~1760 wakeups/s | same |
| 2026-11-22 | CPU package | idle desktop 1-4 min after boot, 1440p, b150; then 24 busy shell loops for 10 s | idle 53-57 C / 39-41 W; all-thread 85-87 C / 120-121 W | [CPU sensors](../.agents/findings/2026-11-22-cpu-sensors.md) |
| 2026-11-22 | Storage, APFS | `nvmebench.sh`, 1 h after boot, b150, idle desktop; SLC cache not rested | seq read 9.13 GB/s; seq write **depends on the SLC cache state: 2.79 GB/s in the exploratory run, 9.54 GB/s in the final one, 1.73 GB/s while folding** (quote neither as typical); 4 KiB QD1 read 19.9 k IOPS (p50 48 us); 4 KiB 8 threads 133 k IOPS read; `F_FULLFSYNC` p50 1.50 ms (668/s) | [storage baseline](../.agents/findings/2026-11-22-storage-baseline.md) |
| 2026-11-22 | Storage, raw device | same run, `/dev/rdisk0s2` read-only | seq read 7.0 GB/s (the raw path's limit, not the drive's); 4 KiB QD1 20.9 k IOPS; 64 threads 558 k IOPS at 5 cores of system CPU | same |
| 2026-11-22 | Storage, sustained write | 180 s, 8 x 1 MiB, after ~0.3 TB of earlier writes | 9.6 GB/s for ~190 GB (one sample), then 1.46-9.6 GB/s (SLC folding); 54-65 C, no throttling | same |
| 2026-11-22 | Audio output | S/PDIF stream, 48 kHz, silence, `audioprobe run 85 20 <frames>`, 1 h after boot, b150 | buffer 512 / 128 / 32 frames: reported latency 11.25 / 3.25 / 1.25 ms; callback period p99.9 within +-0.1 %; 0 overloads, 0 gaps in 60 s; device clock +24 ppm | [audio / connectivity](../.agents/findings/2026-11-22-audio-connectivity-inventory.md) |
| 2026-11-22 | Wi-Fi link | 802.11ax 5 GHz ch 52 / 80 MHz | link rate 780 Mbit/s on 2026-11-19, 867 Mbit/s on 2026-11-22 (not throughput tests) | wifi finding, same |
| 2026-11-22 | Wi-Fi latency | router ping 10 Hz, 300 s, RSSI -56 to -61 | AWDL up: p50 47, p90 308, p99 418, max 456 ms; AWDL down: avg 1.8, max 5 ms | same |
| 2026-11-22 | Wi-Fi latency, fixed | same, boot 22:23 with `amtk_awdl_offchan=0`, AWDL up, 60 s | avg 1.7, max 4.2 ms; 99.6 / 50.4 Mbit/s, idle 14.9 ms | same |
| 2026-11-22 | Internet over Wi-Fi | `networkQuality -s` | AWDL up: 64 / 32 Mbit/s, idle latency 143 ms; AWDL down: 101 / 48 Mbit/s, 14 ms (line-limited) | same |
| 2026-11-22 | Security / iServices daemons | 39 min after boot, b150, idle desktop; `ps -axo time,comm` | ~37 s CPU in total (trustd 9.7, cloudd 7.1, tccd 6.0, identityservicesd 2.7 s): ~1.6 % of one thread | [Apple services / security](../.agents/findings/2026-11-22-apple-services-security-inventory.md) |
| 2026-11-23 | Quick Look / image decode | 13 min after boot, b150, 1440p240; `qlmanage -t -s 512` vs `sips -Z 512` of a 6016x6016 HEIC; no hardware decoder | Quick Look 12.2 s (11.4 s CPU); sips 1.6 s. **One run each, Quick Look first on a cold cache**: the gap is not established | [macOS integration](../.agents/findings/2026-11-23-macos-integration-inventory.md) |
| 2026-11-23 | CPU clock | 20-30 min after boot, change 8, 1440p240 30-bit; `tools/sensors/cpuclk.c` | 1 thread median 5.02 GHz (max 5.12); 2 threads on separate L3s 5.12 (max 5.14); 12 threads 4.97; 24 threads 4.77 (min 3.72, max 5.02) at Tctl 65 C | [sleep / power](../.agents/findings/2026-11-23-sleep-power-validation-inventory.md) |
| 2026-11-23 | Metal vs RADV directly | b155, 1440p240 HDR, quiet desktop; `metalbench` / `vkbench` (same Mandelbrot loop) | compute 2000 iterations: 4.24 / 3.95 ms GPU; waited submit median 89 / 34 us (the plugin adds ~55 us) | [perf / power](../.agents/findings/2026-11-23-perf-power.md) |
| 2026-11-23 | Buffer bandwidth | same; `metalbench` / `vkbench bandwidth 8 256` | **44.7 GB/s** read + write in both: system memory over PCIe (VRAM ~960 GB/s) | same |
| 2026-11-23 | Waited short job | `vkbench submit 8 200 512 512` (GPU 51 us) | poll: median 265 us, p99 0.92 ms; interrupt waits (live test, reverted): median 115 us, p99 2.4 ms | same |
| 2026-11-23 | RADV XTX profile | patch 0018 side-loaded, A/B interleaved | `metalbench render 2000`: 4.86 -> 3.93 ms GPU per frame; compute, Vulkan and bandwidth unchanged | same |
| 2026-11-23 | **Linux:** GPU idle | Omarchy, kernel 7.2.15, 1440p; PPT (= macOS socket) | 9.9 W at 60 SDR (UCLK 96, FCLK 1000, DCEFCLK 200), 22-23 W at 240 SDR, 35 W at 240 HDR; macOS 50-51 W socket at 240 HDR (UCLK 456 pinned, FCLK 2301, DCEFCLK 661) | linux-reference, perf / power 8 |
| 2026-11-23 | **Linux:** vkbench | same boot, 240 SDR | compute 2000: 4.05 ms; compute 64: 0.134 ms (macOS 0.41); bandwidth 823 GB/s (macOS 44.7); submit 37 us; 512x512 job 114 us (macOS 265 poll / 114 interrupt) | same |
| 2026-11-23 | **Linux:** CPU | same boot | 1 thread 5.16 GHz on the frequency CCD (macOS 5.02-5.12); idle package 37.8 W, CC6 98 % (macOS 39.5-41.3 W, 07:14) | same |
| 2026-11-23 | **Linux:** NVMe raw read | same boot, `fio --readonly` | 6.8 GB/s, 4 KiB QD1 21.3 k IOPS (macOS 7.0 GB/s, ~20 k) | same |
| 2026-11-23 | GPU idle, Linux's PMFW messages (live) | b155, 1440p240 HDR, light desktop use; `idletest.sh` | socket / board 53.5 / 67.1 W pinned -> 30.2 / 39.9 W (UCLK unpinned, FCLK switch) -> **23.5 / 31.8 W** (+ DCFCLK messages); UCLK 96, FCLK 1000, DCEFCLK 200; 0 underflows in 20 min | [perf / power](../.agents/findings/2026-11-23-perf-power.md) 9 |
| 2026-11-23 | Same, under load | `vkbench bandwidth / compute`, `metalbench bandwidth`, 5 s each, pinned vs free | 44.1 / 43.7 GB/s, 0.407 / 0.405 ms, 44.2 / 43.8 GB/s; board at 100 % 206-304 / 172-270 W; UCLK stays 96 (buffers are system memory) | same |
| 2026-11-23 | After the restart with the thread 02 daemon | 07:55, 4 min after boot, idle desktop | socket / board 23 / 31 W (UCLK 96, FCLK 1000, DCEFCLK 200), no underflow; `vkbench submit 5 200 512 512` median 124 us, p99 0.46 ms; `metalbench` same 207 us, p99 0.66 ms; IH 89 k IVs, 0 overflow | [perf / power](../.agents/findings/2026-11-23-perf-power.md) 9 |
| 2026-11-23 | Restart | Apple menu > Restart, same state | ~3 s to `shutdown`, ~45 s POST + picker, ~15 s to the accelerated WindowServer: ~67 s in total | same |
| — | LAN throughput, Ethernet, sleep | — | **no baseline yet** (needs an `iperf3` peer, a cable; sleep needs a resume path) | |

### Known bottlenecks and limits

- CPU: no clock above 5.15 GHz, idle package ~40 W. Linux does the same (5.16 GHz, 37.8 W) with the BIOS at Auto: not a
  macOS problem; a `_CST` idle kext would gain ~2-3 W (decisions 2026-11-23). Per-core clocks only through `cpuclk.c`.
- Buffer objects live in system memory, not VRAM (`phx_client.cpp` allocates every BO as an
  `IOBufferMemoryDescriptor`): a buffer copy runs at 44.7 GB/s (PCIe), against ~960 GB/s for VRAM. The 1440p target
  written at ~50 GB/s (2026-11-22) is probably DCC compression *(unverified)*.
- Fence waits: by interrupt since 2026-11-23 07:51 (`phxboot-irqwait`; a short waited job 265 -> 113-115 us median, as
  Linux). Without the flag they poll: a 20 us spin, then 200 us steps. Open: a day of use; the tail with a second GPU
  client (perf / power section 4).
- Idle GPU power: with the UCLK pin, 51-54 W socket at 1440p240 HDR. Linux's PMFW messages, live, gave 23.5 W with no
  underflow and no loss under load (perf / power section 9); `phxboot-idledpm` makes it a boot step (thread 02).
- RADV ran the XT's canned profile on the XTX (84 CU, XT name) until EFI change 15: patch 0018 picks Mesa's `navi31`
  for revision 0xC8 (pixel shading 19 % faster side-loaded). The Metal working set is a fixed 6 GiB.
- OpenGL apps run on the CPU (Apple Software Renderer).
- Debug boot settings (verbose boot, OpenCore logging) cost boot time *(unmeasured)*.
- Storage: random 4 KiB I/O is CPU-bound in Apple's NVMe stack (~9 us of kernel CPU per I/O). Every barrier / full
  fsync is a ~1.5 ms cache flush (the drive has no barrier support). APFS copy-on-write fragments randomly overwritten
  files (sequential reads fell from 9.1 to 2.0 GB/s). Write speed depends on the SLC cache, which may be smaller than
  it could be if Linux's LUKS doesn't pass discards *(unverified)*.

### Optimization decisions

| Date | Decision | Measured reason | Source |
|---|---|---|---|
| 2026-11-19 | Fence-wait step 200 us (daemon flag `phxboot-waitstep`) | WindowServer wakeups 137-352/s vs 1673-2458/s at 50 us; no visible regression in a blind A/B | decisions.md |
| 2026-11-19 | GPU present via E3 (CoreDisplay's display surfaces) | fixed the flicker; WindowServer wakeups 700-1050/s from 1300-1400 | decisions.md |
| 2026-11-19 | GPU-present slots above BAR0 for frames over 8 MiB | removes the per-frame CPU copy at 1440p | decisions.md |
| 2026-11-22 | MtkWireless `amtk_awdl_offchan=0` | router ping max 456 -> 4 ms, internet 64 -> 100 Mbit/s; costs AirDrop while the router is on channel 52 | [audio / connectivity](../.agents/findings/2026-11-22-audio-connectivity-inventory.md) |
| 2026-11-22 | VRR floor 60 Hz, ramp 5 % | the OLED flickers near 48 Hz; the ramp reduces desktop flicker | decisions.md |

## Shared-knowledge tooling (evaluated 2026-11-22)

- **Claude Code auto memory** (`~/.claude/projects/<the main checkout's path>/memory/`): every Claude session in any
  worktree of this repo loads it. Only Claude sees it, it isn't versioned, and it holds personal and machine notes.
  **Use:** owner preferences and pointers to this file; no project facts that aren't also here.
- **T3 thread tools** (`t3_thread_list` / `t3_thread_read`): read another thread's history when needed.
- **Code graph:** none installed; the architecture is documented and `grep` plus the layout table find code. Not
  recommended now; revisit if the refactors of thread 12 start.
- **Claude Docs connector:** a doc outside git doesn't reach other providers and isn't versioned. Not used.

## Changelog of this file

- 2026-11-22: created by the old thread 01 (Shared Repo Intelligence), from a full survey of the repo, the live EFI
  and the system; rows added by the old threads 02-07 the same and the next day.
- 2026-11-23: thread 00 (Housekeeping) reconciled it to the live machine (EFI change 8), fixed every stale claim
  from the first round's final reports, dropped the workstream table (now ROADMAP.md) and the issue index (now
  TODO.md), and renumbered the owners to the series.
- 2026-11-23: thread 01 (GPU stability): EFI changes 9-14 (VermilionBoot b151-b155), PhxAccel with the physical
  size, the daemon's display check; the display rows of the machine state.
