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
