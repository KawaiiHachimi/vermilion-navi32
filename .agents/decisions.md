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
