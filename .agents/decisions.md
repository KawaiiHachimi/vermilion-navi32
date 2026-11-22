# Decisions

Append-only log of choices that shape the work. Each entry has a date, the decision, why, and what would make us
revisit it. Newest at the bottom.

## 2026-11-18: Fork, keep upstream's XT working

**Decision:** work in `ownback/vermilion` (git remote `fork`). `origin` stays leolivm's upstream, read-only for us.
XTX support is added next to the XT values and chosen by detected hardware; no XT value is replaced.
**Why:** upstream is the only Navi 31 macOS driver, it was last updated 2026-11-10, and its XT path is known to work.
Detection keeps our changes usable upstream.
**Revisit:** if upstream becomes active again, rebase onto it and send PRs early.

## 2026-11-18: Bare metal first, VFIO second

**Decision:** get macOS stable on bare metal with the dGPU as the boot display first. Only then build the passthrough
VM.
**Why:** the driver depends on the GOP having lit the monitor and on a cold GPU. Both are easiest to guarantee on bare
metal, and a VM adds reset and OVMF problems on top of driver problems.
**Revisit:** never for ordering. The BIOS primary display changes to the iGPU in Phase 5.

## 2026-11-18: macOS Tahoe 26.7.1, pinned

**Decision:** install exactly 26.7.1 and block updates. Get it as Apple's full installer (build 25G241, still served on
2026-11-18), not through recovery, which installs the newest version (26.7.3).
**Why:** the Metal plugin uses private interfaces of that build (upstream).
**Revisit:** when the plugin is verified on a newer build.

## 2026-11-18: macOS on a 100 GiB partition of the existing NVMe

**Decision:** shrink the Linux LUKS/btrfs partition by 100 GiB and use the space for macOS (p3, exFAT placeholder,
erased as APFS by the installer). The partition table and LUKS header from before are backed up outside the repo.
**Why:** the owner accepted the risk; no second drive yet; 100 GiB is enough to prove the install.
**Revisit:** when macOS becomes a daily system, or for the VFIO VM (a whole disk passed through is simpler).

## 2026-11-18: One monitor at 1440p is the target

**Decision:** target one 2560x1440 DP monitor (60 Hz first, then 120/144/165/240 Hz). Upstream's second-monitor
paths stay untouched but unused.
**Why:** that's the machine's monitor. The second cable goes to the iGPU for the Linux host.

## 2026-11-18: Wi-Fi via AirportMtkWlan first

**Decision:** try Arxusion/AirportMtkWlan (Tahoe build) first for the MT7922, then the alternatives in
[findings](findings/2026-11-18-macos-hardware-kexts.md).
**Why:** the only candidate that is native (Wi-Fi menu), Tahoe-ready, lists `14c3:0616` and is actively maintained.
**Revisit:** if it is unstable, or if a card swap becomes an option.

## 2026-11-18: Docs and agent workspace

**Decision:** `docs/` is for humans and LLMs (public, stable). `.agents/` is the agents' working area (TODO,
findings, decisions, runbooks). `CLAUDE.md` holds the rules, and `AGENTS.md` is a symlink to it for other agents.

## 2026-11-18: The EFI is generated, not hand-edited

**Decision:** `efi/build.py` builds the EFI from pinned, hashed downloads, OpenCore's own `Sample.plist` and a
machine JSON with stages (`install` -> `base` -> `vermilion`). SMBIOS serials live in `~/.config/vermilion` only.
Codeless kexts are kept as plist source in the repo.
**Why:** reviewable diffs, reproducible builds, one-change-per-boot stages, and no secrets in git. Other machines get
their own JSON.

## 2026-11-18: `ResizeAppleGpuBars = 0`, Resizable BAR stays on

**Decision:** keep ReBAR on in the BIOS (Linux uses the 32 GB BAR). OpenCore makes macOS see the default BAR sizes.
**Why:** Vermilion maps BAR0 as its VRAM window and was validated with 256 MiB. OpenCore 1.0.8 accepts only -1 or 0.
**Revisit:** if macOS hangs at PCI configuration, turn ReBAR off as a test.

## 2026-11-18: Fix the DSDT from the machine's own dump, at build time

**Decision:** replace the firmware DSDT with a fixed copy that `efi/build.py` makes from the machine's own dump
(`acpi-dump`) using `efi/acpi/fix_dsdt.py`, with `RebaseRegions` on. The dump stays in `~/.config/vermilion`.
**Why:** an SSDT can't help when macOS rejects the DSDT itself. A DSDT is ASUS's copyrighted table and is tied to one BIOS
version, so the repo keeps the generic fixer, not the table. The fixer is generic, so other AM5 boards with the same
firmware pattern can use it.
**Revisit:** after a BIOS update (dump again), or if OpenCore gains a quirk for deferred module-level code.

## 2026-11-19: GPU before the rest of Phase 1; install Vermilion from Recovery via the EFI partition

**Decision:** the owner put Phase 2 (Vermilion) ahead of Wi-Fi, Ethernet, Bluetooth and the USB map, because the
desktop without a GPU driver is unusable (WindowServer's software compositor at 30 Hz,
[findings](findings/2026-11-19-first-desktop-logs.md)). The files reach the macOS Data volume without the desktop:
Linux stages them on the EFI partition (`tools/phxctl/esp_stage.sh`), and one command in Recovery's Terminal installs
them (`tools/phxctl/recovery_install.sh`). Flags for later boots come the same way: the boot daemon got a generic
pre-start hook (`~/gpu/phxboot-pre.sh`), and on this machine that hook is `tools/phxctl/esp_flags.sh`, which takes
`phxboot-*` files from `<EFI>/vermilion/flags/`. The `vermilion` EFI stage inherits `install`, not `base`, so the
first Vermilion boot doesn't also bring in the untested Wi-Fi kext.
**Why:** Linux can't write APFS, and Recovery can (it has every tool the script uses, checked in BaseSystem.dmg). The
hook keeps the daemon unchanged when absent (169/169 mock scenarios, plus 3 new ones), and lets Linux arm a desktop
boot or set a kill switch between boots without anyone typing in macOS.
**Revisit:** once the accelerated desktop works, flags can be set in macOS directly again; the hook stays useful for
soak tests from Linux.

## 2026-11-19: Desktop-arm boots by file, not by NVRAM, while DisableVariableWrite is on

**Decision:** arm the accelerated desktop with `~/gpu/phxboot-desktop` (staged through the EFI partition), not with
`nvram phxfb-squat=1`. Never inject `phxfb-squat` through OpenCore's `NVRAM > Add`.
**Why:** `DisableVariableWrite` (needed for the installer on this board) blocks macOS's NVRAM writes to the firmware,
so a squat request set in macOS does not survive the reboot (unverified whether it even reads back in the same boot).
An OpenCore-injected `phxfb-squat` would come back at every boot, and the daemon's failure path, which clears it and
reboots onto the firmware framebuffer, would loop. The file-based arm is a supported daemon path (`DESK=1` without
`SQUAT`); `phxfb-boot`, which the daemon sets in the same boot, is the open question for the first desktop-arm boot.
**Revisit:** test whether macOS runs stable with `DisableVariableWrite` off after the install; then the squat path and
`phxboot-auto` work as upstream documents.

## 2026-11-19: GPU-present slots for frames over 8 MiB go above BAR0 (256 MiB), chosen by frame size

**Decision:** VermilionBoot b126 keeps b44's three 8 MiB slots at 24/32/40 MiB for frames up to 8 MiB and gives
larger frames (up to 32 MiB) three 32 MiB slots at 256/288/320 MiB, only when the VRAM and the display's FB window
(DCN_VM_FB_LOCATION_TOP, as the GOP left it) reach 352 MiB (`PhxScanLayout.h`).
**Why:** three 14.06 MiB slots fit no hole inside the 256 MiB BAR without moving the PSP ring, the TMR, mesboot or the
page-table pool, which the XT's working layout relies on. A slot needs no CPU view (the GPU writes it through the
client's PTEs, HUBP0 reads it by MC address), and nothing in Vermilion uses VRAM between 240 MiB and the firmware's
regions at the top. Choosing by frame size leaves the XT at 1080p exactly as before (detect, don't overwrite).
**Cost:** `debug.phxshot` can't read a large slot through the BAR (it returns EINVAL while GPU present shows one).
**Revisit:** if the GOP's DCN window turns out smaller than 352 MiB, program DCN_VM_FB_LOCATION_TOP as Linux's
`hubbub3_init_dchub_sys_ctx` does, or fit the slots below 256 MiB by shrinking the page-table pool.

## 2026-11-19: The `vermilion` stage inherits `base`; AirportMtkWlan 2.1.6 with MaxKernel 25.99.99

**Decision:** re-parent the `vermilion` EFI stage from `install` onto `base`, so the booted EFI gains exactly one
kext, AirportMtkWlan 2.1.6 (Tahoe build), pinned to kernel 25.x (`max_kernel` in `components.json`, now honoured by
`build.py`). Use 2.1.6, not the 2.2.x that the kext's README describes. Its Bluetooth companion is a separate,
later step.
**Why:** the owner wants Wi-Fi first; `base` exists exactly for this, and the stage chain keeps the one-change-per-
boot rule visible in the diff. 2.2.x was pulled by its maintainer after panics; the Tahoe build is not meant for
other kernels.
**Revisit:** if Wi-Fi destabilises the GPU path (then `vermilion` back onto `install`), or when a newer release is
recommended.

## 2026-11-19: Work moves to macOS; boot straight into it, with a Linux reboot guard

**Decision:** the owner works in macOS from now on. Firmware boot order OpenCore first, default OpenCore entry
Macintosh HD, auto-login and T3 Code at login (set by the owner in macOS). A systemd unit on Linux sets
BootNext=Limine on every Linux reboot. Sleep is turned off by `pmset` from a run-once.
**Why:** faster iteration without the Linux round trip. With OpenCore first, a warm Linux reboot would otherwise
start macOS on a GPU amdgpu left initialised (hard rule 1); the guard keeps a reboot in Linux and leaves a power-off
alone. No GPU resume path, so no sleep.
**Revisit:** when the VFIO phase starts (the iGPU becomes the host display), or if the firmware ignores BootOrder.

## 2026-11-19: Wi-Fi driver: MtkWirelessWifi 1.1.1 after AirportMtkWlan 2.1.6 didn't attach

**Decision:** the `base` stage adds Dino-VN/MtkWireless's `MtkWirelessWifi` 1.1.1 instead of AirportMtkWlan 2.1.6.
The boot that tries it also captures the IORegistry of the card's port, so a second failure is diagnosable.
**Why:** AirportMtkWlan loaded but never attached on boot 1, and an injected kext leaves no readable text in the
unified log. MtkWireless is the only candidate tested on this exact macOS build and SMBIOS. The alternative fix on
the table, AMD_Vanilla's `IOPCIIsHotplugPort`, changes PCI enumeration for the GPU too and needs evidence first.
**Revisit:** if MtkWireless doesn't attach either (then the IORegistry decides), or once AirportMtkWlan reports an
RZ616 working on 26.7.1.

## 2026-11-19: `pci=0x8000000` on this machine: macOS keeps the firmware's PCIe tag settings

**Decision:** the `base` stage boots with `pci=0x8000000` (`kIOPCIConfiguratorDefaultETF`), and Wi-Fi uses
MtkWirelessWifi 1.1.1.
**Why:** IOPCIFamily otherwise sets 10-Bit Tag Requester Enable on the chipset root port (GPP7); the MT7922 and the
I225-V can't complete 10-bit tags and stop answering. Linux leaves that bit off. AMD_Vanilla's binary patch for the
same thing is enabled but didn't keep the bit off on 26.7.1. A boot-arg that uses Apple's own switch is simpler than
fixing a byte pattern. Verified: Wi-Fi at 802.11ax, Vermilion unaffected.
**Revisit:** if a future IOPCIFamily drops the flag, or if the GPU path turns out to want 10-bit tags (then a
per-device fix instead). Other AM5 boards with these two chips likely need the same; worth upstreaming as a note.

## 2026-11-19: GPU present uses E3 (CoreDisplay's display surfaces) on this machine, behind a daemon flag

**Decision:** the XTX machine runs GPU present with `ws-gpu-present-async` and `NVMTL_WS_PRESENT_CDCOPY=1`. The boot
daemon sets the variable from the flag `~/gpu/phxboot-wscdcopy` before its WindowServer restart. The plugin default
stays as it is, so the upstream XT keeps its behaviour.
**Why:** the default mode presents SkyLight's composite targets, which only have the changed regions redrawn: those
regions flickered (verified, 1440p60). E3 fixed it on the first boot it ran
([findings](findings/2026-11-19-gpu-present-flicker-heat.md)). A flag was the smallest change that survives reboots:
`launchctl setenv` is how the daemon already passes `CI_USE_MTL_DAG_FOR_CIKL_SRC`, and the plugin needs no rebuild.
**Revisit:** when the plugin is rebuilt: a file switch next to the other `ws-*` switches would replace the variable,
and E3 may deserve to be the default for GPU present on every card (needs a test on the XT).

## 2026-11-19: the fence wait's timed step is 200 us on the XTX machine, behind a daemon flag

**Decision:** the boot daemon sets `debug.phxwait_step_us` from the flag `~/gpu/phxboot-waitstep` (its content, empty
= 200, 20-1000 accepted) before its WindowServer restart. The XTX machine uses 200. The kext's default stays 50, so
the upstream XT keeps its behaviour.
**Why:** with IRQ waits off, every fence wait longer than 20 us sleeps in timed steps; at 50 us WindowServer woke
640-2500 times/s. 200 cut that 5-15x and was not worse than 50 in a blind A/B on the XTX machine
([findings](findings/2026-11-19-gpu-present-flicker-heat.md), live tests 3-4). It costs ~240 us more per waited
fence. A flag is the smallest change that survives reboots, with no kext rebuild. Its effect on heat is unverified
(no CPU temperature reading yet).
**Revisit:** once CPU temperature or package power is readable, or if the IRQ wait path is verified on the XTX
(then the step only matters as its fallback); growing steps in the kext (50, 100, 200) if a lower latency is wanted.


## 2026-11-19: the full EDID read is a separate sysctl first (b127), the served EDID stays 256 bytes

**Decision:** VermilionBoot b127 reads EDID blocks 2-3 only in a new root sysctl `debug.phxedid_full`. `readEdid`,
`debug.phxedid` and the 256-byte EDID PhxAMDFB serves (`phxEdidRead`) are unchanged.
**Why:** serving 384 bytes changes the PhxBringup/PhxAccel interface (`phx_head1_hotplug.edid[256]`, PhxAMDFB's buffer),
so both kexts would have to change in the same boot (EFI and AuxKC: two changes in one boot). The first goal is only
to see the LG's block 2 (the 240 Hz timing), and a sysctl does that with one kext and no change at boot.
**Revisit:** once the segment read works on hardware and the 240 Hz work starts (macOS then needs block 2 to list the mode).

## 2026-11-20: 1440p high refresh on the XTX: 120/144 Hz first on the firmware's HBR2 link, then 240 Hz

**Decision:** b128 adds 1440p 60/120/144 pipe-0 plans (firmware DTO convention, Linux's timing / DLG values from the
2026-11-20 capture, depth 12 -> 10 / 8 bpc), and the pipe-0 engine sends the two DAL hard minimums Linux sends for them
(DISPCLK and DPPCLK `SetHardMinByFreq`, 505 / 614 MHz; owner's OK 2026-11-20), never lowered, nothing else from Linux's DAL
list (0x14 stays blocked). 240 Hz (HBR3 retraining of DP B + DSC) is the next, separate step on top.
**Why:** 120/144 fit the HBR2 x4 link the firmware trains, so they need no link training, the riskiest part. Every piece
(plans, clock step, DET step, mode list) is also needed at 240 Hz.
**Revisit:** if the engine's underflow check fails at 144 with the firmware's watermarks (then Linux's watermark sets A-D).

## 2026-11-20: 240 Hz uses Linux's exact configuration (HBR3 + FEC + DSC + ODM 2:1)

A single pipe without DSC would fit the link at 8 bpc (inferred) but needs a DPPCLK minimum and HUBP values Linux never
used on this card, and gives no HDR at 240. Linux's configuration is fully captured (registers + DMUB commands), covers
10 bpc, and keeps the rule "nothing Linux doesn't send". The link work is staged: retrain at HBR2 first, then HBR3 at
144, then 240. Details: `.agents/findings/2026-11-20-240hz-plan.md`.

## 2026-11-20: every 1440p rate is a Linux replay through Linux's 144 (b135)

The 1 ppc pipe-0 plans (b128) and the Linux replay (b131-b134) left pipe 0 in two different setups that neither could
leave. b135 makes Linux's own 144 the hub: each rate is Linux's switch from 144 and back (capture v3), so every transition is
two recorded Linux mode sets and no start state is guessed. macOS's own state keeps the b128 plans until the first 240 (the
only entry, b133's verified plan). Other resolutions, VRR and HDR join the same set from the same capture.

## 2026-11-20: A squat boot that can't leave the squat runs a safe desktop, never another reboot

**Decision:** when a reboot meant to clear `phxfb-squat` comes back squatting (`~/gpu/phxboot-nvstuck`), the daemon
stops rebooting. With the kill switch, an unfinished attempt or a failure marker, it runs a safe desktop on the squat
(CPU copy at the boot mode; no pipe-0 mode list, GPU present, flip, M7, b15, VCN). If that attempt doesn't finish, the
next boot stays headless (no attempt, no reboot). `DisableVariableWrite` stays on (owner's choice).
**Why:** with `DisableVariableWrite` macOS can't delete the variable, so every recovery reboot came back to the same
squat: three reboots in a row on 2026-11-20 until Linux deleted it. A squat boot has no display except PhxAMDFB, so a
plain PhxAMDFB desktop is the only way to a picture without leaving macOS.
**Revisit:** if `DisableVariableWrite` goes off, the first recovery reboot clears the squat as upstream designed and the
safe desktop never runs.

## 2026-11-20: Adaptive Sync through Linux's OTG register path, offered by default

**Decision:** VRR on the XTX machine uses Linux's DCN 3.2 register path (OTG_V_TOTAL_MAX, MIN, CONTROL, and
OTG_TRIGA_MANUAL_TRIG per flip), never the DMUB FAMS command, and PhxAccel offers "Variable" by default when the monitor
and PhxBringup support it (opt-out `~/gpu/phxboot-novrr`).
**Why:** Linux on this card used the DMUB FAMS command, but only because its own firmware reports fw-assisted mclk
switching. The GOP's DMCUB build that runs on macOS reports none (`debug.phxdmub_caps`), so Linux's code would take the
register path on it, and PhxDmub.h forbids FAMS commands to that firmware. Box-verified 2026-11-20 (OTG0 52-188 Hz with the
content). Default on: the offer needs the LG's 240 Hz DisplayID timing, which the XT's monitors don't have.
**Revisit:** if Linux's firmware ever runs on macOS, or for the CPU-copy sizes (no per-flip trigger there yet).

## 2026-11-22: Adaptive Sync floor 60 Hz by default, configurable

The LG flickers in Variable near its EDID minimum of 48 Hz (owner; the same on Windows and Linux, so the monitor's VRR:
brightness swings between long and short frames). The common fix on any OS is a higher floor (owner's reading, findings
2026-11-20-adaptive-sync-research.md). PhxAccel's `debug.phxfb0_vrrmin` (default 60) sets System Settings' range, the
twin's verticalBlankingExtension and, through "phxVrrSet" io bits 8-15 (PhxBringup b146+), OTG0's V_TOTAL_MAX: the menu,
macOS's pacing and the hardware agree. With an older PhxBringup the EDID's minimum stays everywhere (the menu must match
what OTG0 does). The boot daemon's `~/gpu/phxboot-vrrmin` changes it (0 = the EDID's).

## 2026-11-22: the DRR double buffer is on only while Adaptive Sync's range is on

Linux runs every OTG with `OTG_DRR_TIMING_DBUF_UPDATE_MODE` = 2 from driver load; the GOP leaves 0, and the mid-frame
window changes that follow raised HUBP0's underflow sticky (findings 2026-11-22-vrr-underflow-and-auxkc.md). b150 sets
Linux's 2 when the range goes on and puts the boot value back when it goes off, after the off values have latched.
Not chosen: mode 2 for the whole boot, as Linux has it. The replay plans were captured on Linux under mode 2 but have only
ever run here under mode 0 (their V_TOTAL / H_TOTAL writes would latch at a frame start instead of at once, around an OTG
that the plans stop and start), and a plan that goes wrong blacks the owner's only screen. **Revisit:** if a plan is ever
shown to need the double buffer, or once a plan run under mode 2 has been tried on the box.

The replay engine's failed latch stays (one failed run = nothing more until reboot: no plan knows the state). What changed
is what counts as failed: an underflow sticky from before the run is cleared and not held against it, and a sticky raised
during the switch gets one clear and one more full post check.

## 2026-11-22: no VRR / HDR below 1440p; the other resolutions stay as fixed rates

The owner runs only 2560x1440, where every rate, Variable (60-240 Hz, ramp), 10-bit HDR and Variable + HDR work on the box.
VRR and HDR at the other sizes would need a CPU-copy trigger, per-size twins and a Linux capture per HDR mode: dropped.
The other resolutions are left on (fixed rates): they work, cost nothing at 1440p and serve other machines.

## 2026-11-22: the pre-merge clean-up is proven by identical binaries; the 10-bit pixel helper stays in two places

The clean-up of `ownback/gpu-refresh-hdr` was held to "no behaviour change" by a check, not by review: both kexts and
the Metal plugin build reproducibly, so each cleaned source had to produce the sha256 of the binary installed on the
XTX machine (VermilionBoot b150 3806555e..., VermilionAccel c6341e87..., NVMTLDriver 7f46d89b...). A tidy-up that moved
the machine code was taken back (a hoisted condition in `scanPresent`, a hoisted assignment in `PhxAMDFB::start`).
Consequence: sysctl description strings, log texts and non-static names were left as they are, also where a better
name exists (`phxVrrOids` registers four sysctls now). `PHX_KEXT_BUILD` stays 150 and nothing was reinstalled.

`bgraTo2101010` exists twice: `phxhead::bgraTo2101010` (PhxAccel, C++) and `nvmtl_bgra_to_2101010` (Metal plugin, C).
Both are PolyForm Noncommercial, so the licence would allow one shared header. They stay apart because the plugin is
built without any kext header and mirrors the kernel ABI by hand already; one four-line function does not justify the
first include across that line. Both comments name the other copy. **Revisit:** when the plugin gets a shared ABI header.

## 2026-11-22: USB map with USBToolBox, kept as a codeless kext source in `efi/kexts/`

**Decision:** map USB with USBToolBox 1.2.0 and a hand-written `UTBMap.kext` (`efi/kexts/UTBMap.kext`, matched by
the controllers' ACPI names), in the `base` stage. Keep every rear port, the internal audio and Bluetooth ports and
the unknown internal port 17; leave out the front-panel headers (no front panel) and the AURA LED controller.
**Why:** the firmware's `_UPC` marks the audio and Bluetooth ports not connectable, so macOS never created them;
USBToolBox detaches the ACPI ports and takes the ports from the map alone. A codeless kext in the repo keeps the map
reviewable and reproducible like the rest of the EFI (no tool-generated binary). Verified on the XTX machine
2026-11-22 ([findings](findings/2026-11-22-usb-map.md)).
**Revisit:** if a front panel is added (ports 2-4 / 7-9 / 13-14), if a USB 3 device shows a wrong pairing, or after
a BIOS update (port numbers can change).

## 2026-11-22: CPU sensors with SMCProcessorAMD; no AMDRyzenCPUPowerManagement

**Decision:** read the CPU's temperature and package power with macos86/SMCProcessorAMD 1.0.1 (a VirtualSMC
plugin, `base` stage). Leave CPU power management as it is (`DummyPowerManagement`, the CPU's firmware decides
clocks and idle).
**Why:** AMDRyzenCPUPowerManagement, the usual recommendation, replaces the kernel's CPU power-management dispatch
and rewrites the P1 P-state definition MSR at load with a formula for older Ryzen parts; its Tahoe fork also reads
and writes the GPU's registers on a timer, which would race Vermilion. SMCProcessorAMD only reads (binary
disassembled: no `wrmsr`, no port I/O, one SMN index write as Linux `k10temp` does). Verified on the XTX machine
([findings](findings/2026-11-22-cpu-sensors.md)).
**Revisit:** if OS-level P-state control is ever wanted (then a Zen 4-correct implementation, tested on its own
boot), or if per-core clocks are needed.
