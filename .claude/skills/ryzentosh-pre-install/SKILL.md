---
name: ryzentosh-pre-install
description: Prepare an AMD Ryzen + Radeon RX 7900 XT/XTX PC for macOS Tahoe 26.7.1 from Linux. It checks the machine, dumps ACPI, records the monitor, builds the OpenCore EFI, copies it to the EFI partition and prepares the installer. Use when the user wants to install macOS (a Hackintosh) on this kind of machine and is running Linux on it. DRAFT skeleton (thread 00); the steps are filled in by the series threads.
---

<!-- SPDX-License-Identifier: MIT -->

# ryzentosh pre-install (Linux side) — draft

**Status: a skeleton.** Thread 00 wrote it on 2026-11-23. Each section names the series thread
([ROADMAP.md](../../../ROADMAP.md)) that fills it in, and it moves to `ownback/ryzentosh` in thread 10. Only steps
that are already scripted and verified on the reference machine are written out. Everything else is a TODO: say so
to the user and stop there. Don't improvise.

## Hard guardrails (read before doing anything)

1. **Hardware safety.** Never send an SMU, PSP or DAL message, or write a GPU register, that Linux amdgpu doesn't
   send on this ASIC. Never flash, write or dump the VBIOS. When going Linux -> macOS the user always **powers off
   fully**, never a warm reboot (the GPU bring-up needs a cold GPU). Keep the bring-up watchdog and the boot daemon's
   fallbacks. ([CLAUDE.md](../../../CLAUDE.md) rule 1.)
2. **Never repartition, format or write the EFI partition (ESP) without the user's explicit OK for that exact
   step,** given right before it. Show the exact command and the target device first (`lsblk -f`), and back up the
   GPT first (`sgdisk --backup=<file> /dev/<disk>`). Never touch another OS's boot files on a shared ESP (Limine,
   systemd-boot, `EFI/BOOT/BOOTX64.EFI`, Windows' `EFI/Microsoft`).
3. **One EFI change per boot.** `efi/build.py install` keeps the previous EFI as `EFI/OC.prev`. Write the rollback
   steps down for the user before they reboot.
4. **Never print serials.** `efi/build.py smbios` runs macserial and writes the serial, MLB, UUID and ROM into
   `~/.config/vermilion/smbios-<machine>.json` (mode 0600). Don't `cat` it, don't print `config.plist`'s
   `PlatformInfo`, and don't paste `build/efi/` anywhere. A built EFI contains the serials: never commit, upload or
   share it. The same goes for EDIDs with serials, the ACPI dump (a full one contains the VBIOS) and MAC addresses.
5. **Tell the user, once, at the start:** everything this agent reads (command output, files, logs) goes to the
   model provider. The skill keeps serials out of that, but the user decides what else to share.
6. Scripts do the risky work; this skill only runs them. It never writes an ESP layout, a partition table, a
   register access or a `config.plist` by hand.

## 1. Check the machine — to be filled by thread 10

Today, by hand: GPU PCI id `1002:744c` and revision (`lspci -nn -d 1002:744c`, `cat
/sys/bus/pci/devices/*/revision`): `0xcc` is the XT, `0xc8` the XTX. Board and BIOS version
(`/sys/class/dmi/id/board_name`, `bios_version`). The CPU. Linux booted directly from the firmware, **not through
OpenCore** (`/sys/class/dmi/id/bios_vendor` must not be `Acidanthera`). TODO: a script that checks all of it and the
BIOS settings (dGPU as boot display, Above 4G + ReBAR on, CSM off) against a supported list. CPU power settings
(Global C-state Control, CPPC, Power Supply Idle Control, PBO, ECO mode) stay at **Auto**: Linux on the reference
machine reaches CC6 and 5.16 GHz with them so (thread 02). Optional, while still on Linux: a reference capture for later
comparison ([HANDOFF-LINUX-REFERENCE.md](../../../.agents/HANDOFF-LINUX-REFERENCE.md), read-only).

## 2. Machine profile — to be filled by thread 10

Today only one profile exists: `efi/machines/b650e-i-xtx.json` (ASUS ROG STRIX B650E-I, Ryzen 9 7900X3D, BIOS 4005).
Another board needs a new profile. Its DSDT patches, SSDT-CPUR and USB map are written by hand today (generators:
thread 10). If the user's machine is not that profile, stop and say so.

## 3. ACPI dump (verified on the reference machine)

```sh
sudo efi/build.py acpi-dump --machine <id>     # DSDT + namespace + BIOS version -> ~/.config/vermilion/acpi/<id>/
chmod -R go-rwx ~/.config/vermilion            # acpi-dump doesn't restrict the folder's mode itself
```

`acpi-dump` saves the DSDT, the live ACPI namespace and the BIOS version. A full ACPI dump made by hand also holds
`VFCT`, which is the VBIOS. Either way it stays in `~/.config/vermilion`, outside git, and is never printed. Dump
again after every BIOS update; `efi/build.py build` refuses a dump or firmware of another BIOS version than the
profile's DSDT patches were derived for.

## 4. Monitor capture — to be filled by thread 06

Today high refresh, HDR and VRR exist only for the LG 27GS95QE-B on the reference XTX (replay plans compiled into
VermilionBoot). The capture tool exists: `sudo python3 tools/capture/linux_display_capture.py <dir> --dry-run`
lists the steps (it switches the monitor's modes; the screen goes black for seconds). Turning a capture into plans
loaded at boot is thread 06's work. Until then, another monitor gets the firmware's mode only (unverified on other
monitors). Captures stay outside git; `edid-noserial.bin` must have zeros at bytes 12-15.

## 5. SMBIOS (verified)

```sh
efi/build.py smbios --machine <id>             # once per machine; never overwrites an existing file
```

Tell the user to back up `~/.config/vermilion/` privately. A new identity means iMessage registers again and uses
one more Apple Music authorization. Don't print the file.

## 6. Build the EFI (verified on the reference machine)

```sh
efi/build.py stages --machine <id>             # what each stage contains; the default stage is marked
efi/build.py build --machine <id> --stage install      # the installer's EFI (first boot): fewest kexts, SIP on
```

`ocvalidate` must report no issues. VermilionBoot / VermilionTrust come later, in macOS (post-install skill). TODO
(thread 06): the cdhash list as data, so the default stage can be built on Linux.

## 7. Copy to the EFI partition (verified; needs the user's OK — guardrail 2)

```sh
lsblk -f                                       # show the user which partition is the ESP, and what else is on it
sudo efi/build.py install --esp <mount point of the ESP> --machine <id> --stage install   # keeps EFI/OC.prev
```

It writes only `EFI/OC`. `EFI/BOOT` stays untouched unless `--with-fallback` is given, which is for USB sticks only.
Adding a firmware boot entry (`efibootmgr --create ...`, [efi/README.md](../../../efi/README.md)) changes the boot
order: ask first, and show how to restore the old order.

## 8. The macOS installer — to be filled by thread 10

Today it needs a Mac (`efi/installer/make-installer-image.sh` runs `createinstallmedia`), then
`efi/installer/write-installer-partition.sh` writes a 22 GiB HFS+ partition named "macOS Installer". That partition
has to be created first, which is a repartition (guardrail 2). Not yet possible without a Mac. TODO: an installer
image built on Linux, or a documented USB route, plus the partition guide.

## 9. Hand-off to the user

Tell them, in this order:
1. Power off (not restart). Power on, pick OpenCore in the firmware boot menu (F8 on ASUS), then the installer.
2. In the installer's Disk Utility, erase **the macOS partition** (View > Show All Devices), never the whole disk,
   as APFS.
3. After the install, boot Macintosh HD and run `/ryzentosh-post-install` in macOS.
4. Rollback: boot Linux from the firmware menu and restore `EFI/OC.prev` (`mv EFI/OC EFI/OC.bad && mv EFI/OC.prev
   EFI/OC` on the ESP).
