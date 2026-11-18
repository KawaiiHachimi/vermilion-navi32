# OpenCore EFI

A reproducible OpenCore EFI for running macOS Tahoe on the machines in [`machines/`](machines/). Today that's only
the XTX machine from [docs/REFERENCE-MACHINES.md](../docs/REFERENCE-MACHINES.md). Nothing here is a hand-edited
`config.plist`:

| File | What |
|---|---|
| [`components.json`](components.json) | Every downloaded input (OpenCore, kexts, drivers, AMD patches), pinned by URL and SHA-256 |
| [`machines/<id>.json`](machines/) | One machine: CPU cores, SMBIOS model, ACPI tables, the settings that differ from OpenCore's `Sample.plist`, and the **stages** |
| [`acpi/`](acpi/) | SSDT sources (`.dsl`), compiled with `iasl` at build time. Each one says which DSDT it was written against |
| [`kexts/`](kexts/) | Codeless kexts kept as source (plists) |
| [`build.py`](build.py) | Fetches, verifies, assembles `EFI/`, generates `config.plist` from `Sample.plist` + the machine file, runs `ocvalidate` |

SMBIOS serials (serial, MLB, UUID, ROM) are generated once per machine with OpenCore's `macserial`. They live in
`~/.config/vermilion/smbios-<machine>.json` (mode 0600) and are never written into the repository. The generated
`config.plist` contains them, so `build/` is git-ignored. **Never publish a built EFI.**

## Build

Needs `python3` (stdlib only) and `iasl` (Arch: `pacman -S acpica`, macOS: `brew install acpica`). Works on Linux and
macOS.

```sh
efi/build.py smbios                      # once per machine: ~/.config/vermilion/smbios-b650e-i-xtx.json
efi/build.py stages                      # what each stage contains
efi/build.py build --stage install       # -> build/efi/b650e-i-xtx-install/EFI/{BOOT,OC}
efi/build.py build --stage install --placeholder-smbios   # test build with dummy serials (never boot it)
```

## Stages

One machine, several configurations. Each stage builds on the previous one, so a problem can be traced to the one
change that caused it:

| Stage | For | Adds |
|---|---|---|
| `install` | First boot and the macOS installer | Lilu, VirtualSMC, NVMeFix, RestrictEvents, AppleMCEReporterDisabler; `-v keepsyms=1 debug=0x100`; SIP on |
| `base` | The installed system | + AirportMtkWlan (Wi-Fi). Bluetooth, the Ethernet fallback and the USB map follow as they are tested |
| `vermilion` | GPU acceleration ([docs/INSTALL.md](../docs/INSTALL.md)) | + AMFIPass, `VermilionBoot.kext` (from `build/`); `amfi=0x80 ipc_control_port_options=0 phxdebug=1`; SIP `0x0A03` |

## Install onto the EFI partition

The EFI partition on the XTX machine is shared with Linux's Limine (`/boot`). `install` only writes `EFI/OC` and keeps
the previous one as `EFI/OC.prev`. It never touches `EFI/BOOT/BOOTX64.EFI` (Limine's fallback) unless
`--with-fallback` is given, which is meant for USB sticks.

```sh
sudo efi/build.py install --esp /boot --stage install
sudo efibootmgr --create --disk /dev/nvme0n1 --part 1 --label OpenCore --loader '\EFI\OC\OpenCore.efi'
```

`efibootmgr --create` puts the new entry first in the boot order. To keep Limine as the default, restore the old
order with `efibootmgr --bootorder ...`, then choose OpenCore once from the firmware boot menu (F8 on ASUS).

## The macOS installer without a USB stick

Recovery images always install the newest macOS, but Vermilion is pinned to 26.7.1. The full installer (18.4 GB)
doesn't fit on a small stick, so it goes onto a partition of the internal disk instead:

```sh
efi/installer/macos_catalog.py --version 26.7.1 --url        # Apple's download URL for that version
# on a Mac (the macbox VM): download it, then
efi/installer/make-installer-image.sh InstallAssistant.pkg Installer-26.7.1.dmg 21g
# on Linux: a partition with the GPT name "macOS Installer" (Apple HFS+ type, 22 GiB), then
efi/installer/write-installer-partition.sh macbox Installer-26.7.1.dmg /dev/nvme0n1p4 --yes
```

`make-installer-image.sh` checks Apple's signature, runs `createinstallmedia` onto a bare HFS+ image (no partition
map), and the writer streams it over SSH and compares SHA-256 on both sides. OpenCore lists the partition as
**Install macOS Tahoe**. After the install, delete the partition and grow the APFS container into the space
(`diskutil apfs resizeContainer disk0s3 0` from macOS).

## Why these settings (XTX machine)

- **AMD CPU:** AMD_Vanilla kernel patches with the core count set to the physical cores (`0x0C`), including the
  Zen 4 IOPCIFamily patch; `ProvideCurrentCpuInfo`; `DummyPowerManagement`.
- **Booter quirks:** as the ASUS B650E-F config (same chipset family) uses them, including `SetupVirtualMap` and
  `DevirtualiseMmio`.
- **`ResizeAppleGpuBars = 0`:** Resizable BAR stays on for Linux. macOS sees the default BAR sizes (BAR0 256 MiB),
  which is what Vermilion was validated with.
- **ACPI:** only `SSDT-EC-USBX-B650EI` (hide the desktop EC from macOS, add the fake `EC` and USB power properties).
  Newer AM5 firmware (9000-series AGESA) has been reported to hang macOS without a DSDT fix (B650E-F, BIOS > 2613).
  The board here runs BIOS 4005, so watch for that on the first boot.
- **SMBIOS `MacPro7,1`:** upstream's choice for Vermilion. RestrictEvents silences its memory-slot warnings, and
  AppleMCEReporterDisabler stops its MCE reporter from panicking on AMD.
- **`SecureBootModel = Disabled`:** Vermilion's injected kexts are unsigned.
- **Debug:** verbose boot and an OpenCore log file on the EFI partition (`Target = 67`) during bring-up.

## Updating a pin

Change `url` and `sha256` together in `components.json` (`sha256sum` of the new file), rebuild, and note the reason in
the commit. `build.py` refuses any file whose hash differs from its pin.
