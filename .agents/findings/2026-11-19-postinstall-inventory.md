# 2026-11-19: post-install inventory (ACPI, CPU, USB) on the XTX machine

Read-only, from the running macOS 26.7.1 (25G241) on the XTX machine, `vermilion` stage booted
(`-v keepsyms=1 debug=0x100 pci=0x8000000 amfi=0x80 ipc_control_port_options=0 phxdebug=1`). Sources: `ioreg`,
`system_profiler`, `kextstat`, the BIOS 4005 tables from `~/.config/vermilion/acpi/` decompiled with iasl 20251212,
and a placeholder-SMBIOS build of the `base` stage. The live `EFI/OC` itself was not read yet (needs root).

## ACPI

| Item | State | Needed on this machine? |
|---|---|---|
| EC / USBX | `SSDT-EC-USBX-B650EI`: EC0 hidden, fake `EC`, USBX; the xHCI drivers show `kUSB*PortCurrentLimit = 2100` | done |
| CPUs | `SSDT-CPUR-B650EI`: 24 `Processor ()` objects (firmware has only ACPI0007 devices); 12 cores / 24 threads seen | done |
| RTC / AWAC | Legacy `RTC0` (PNP0B00) under `SBRG`, no AWAC device; `AppleRTC` attached | nothing to do |
| HPET | `\HPET` exists, but its `_STA` depends on `OSVR >= 0x0C` (a Windows version check); no `AppleHPET` attached | probably nothing (no symptom); check `_STA` under Darwin before any change |
| PLUG, PMCR, MCHC, SBUS | Intel-only SSDTs (X86PlatformPlugin, PCH PMC, Intel MCH/SMBus) | not used on AMD |
| xHCI names | `XHC0`, `XHC1` (CPU, `GP17`), `XHC2` (`GP19`, USB 2 only), `XH00` (chipset, behind `GPP7.UP00.DP60`) | no renames needed: USBToolBox matches by PCI path |
| DSDT | 5 in-place NOP patches (chipset blocks), each matched once | done |

`efi/README.md` still describes the replaced DSDT (`fix_dsdt.py resolve`, `RebaseRegions`): stale, to fix.

## CPU

- AMD_Vanilla `eaf52ef2`: the four `cpuid_cores_per_package` patches carry `0x0C` (12 physical cores, correct for the
  7900X3D); the Zen 4 PAT patch for 15.0+ and `Disable 10 bit tags` are on; `IOPCIIsHotplugPort` is off (as
  shipped). `ProvideCurrentCpuInfo` and `DummyPowerManagement` on. Nothing wrong found.
- **No CPU sensor kext is loaded** (`kextstat`: Lilu, VirtualSMC, NVMeFix, RestrictEvents, MtkWirelessWifi,
  AMFIPass, VermilionBoot, VermilionAccel). VirtualSMC's own SMCProcessor is Intel-only, so there is no temperature
  or package power anywhere. Candidates: trulyspinach/SMCAMDProcessor (`AMDRyzenCPUPowerManagement` + the
  `SMCAMDProcessor` VirtualSMC plugin, last release 2021) or a Tahoe fork (kodeaqua/SMCAMDProcessor-personal, claims
  Zen 4 / Ryzen 7000, unverified). They read temperature through SMN (host bridge index/data registers, as Linux
  `k10temp` does) and power through the RAPL energy MSR. Nothing in Vermilion uses SMN on the host bridge or RAPL
  (grep of `src/`, `tools/`), so no overlap with the GPU side. The code still needs reading before it goes in: what
  it writes by default (P-state MSRs, CPB) must be nothing.

## USB

Four controllers, all AMD:

| Controller | ACPI | Ports macOS shows | Devices today |
|---|---|---|---|
| `XH00` (chipset, USB 3.2) | `GPP7.UP00.DP60.XH00` | 13: `POT1,3,4,5,6` (USB 3), `POT7,9,A,B,C,F,G,H` (USB 2) | gaming mouse on `POTH` |
| `XHC0` (CPU, USB 3.1) | `GP17.XHC0` | 4: `PRT1-2` (USB 2), `PRT3-4` (USB 3) | none |
| `XHC1` (CPU, USB 3.1) | `GP17.XHC1` | 4: `PRT1-2`, `PRT3-4` | keyboard (Hama) on `PRT2` |
| `XHC2` (USB 2.0) | `GP19.XHC2` | 1: `PRT1` | USB stick on `PRT1` |

- The chipset `XH00` RHUB declares **17 ports** (`_ADR` 1..0x11) in the DSDT; macOS creates 13 of them. Missing:
  `POT8` (`_ADR` 0x10), `POTD` (0x0B), `POTE` (0x0C), `POTI` (0x11) (which ones are really cut by the 15-port limit
  vs. reported not connectable by `GUPC` is unverified).
- **Neither the Bluetooth device (`0489:e0e2`, Linux: bus 1 port 11, USB 2) nor the ASUS USB audio (`0b05:1a5c`) is
  enumerated at all.** No audio device exists in `system_profiler SPAudioDataType`. Most likely both sit on chipset
  ports macOS dropped (unverified until a map includes them). So the USB map comes before Bluetooth and audio.

## Coordination with the GPU heat thread

As of 2026-11-19 22:38 the EFI partition holds the GPU heat thread's change for the **next restart**:
`vermilion/run-once.sh`, `vermilion/daemon/` (rollback `vermilion/daemon.prev`), `vermilion/flags/phxboot-waitstep`
(branch `ownback/gpu-heat`, commit 6ece9c9, TODO item "Heat: make 200 permanent"). That restart keeps `EFI/OC`
unchanged; this thread's first `EFI/OC` change is staged for the restart after it, and none of those files are
touched.

## Ethernet

The I225-V (`8086:15f3`) is bound (`driver-child-bundle` = `com.apple.DriverKit-AppleEthernetE1000`) and has an
interface `en3` with `status: inactive`, `media: autoselect (none)`: no link, maybe no cable (ask the owner).
