# 2026-11-18: EFI inputs and the macOS installer

## macOS versions Apple serves (software update catalog, 2026-11-18)

Read from Apple's public `swscan.apple.com` merged catalog (`index-26-15-...sucatalog.gz`), products with an
`InstallAssistant.pkg`:

| Product | Posted | Version | Build | Size |
|---|---|---|---|---|
| `142-28081` | 2026-09-28 | **26.7.1** (upstream's pin) | 25G241 | 18.4 GB |
| `142-50020` | 2026-10-26 | 26.7.2 | 25G332 | 18.4 GB |
| `142-54128` | 2026-11-16 | 26.7.3 | 25G342 | 18.4 GB |

Tahoe is the last x86 macOS: 27.x ("Golden Gate") is Apple Silicon only. `macrecovery -os latest` serves the newest
Tahoe, so only the full installer gives 26.7.1.

The full installer doesn't fit on the 8 GB stick the machine has. Options:

- a 20 GiB installer partition carved from the 100 GiB macOS partition, filled from a `createinstallmedia` disk image
  made in the macbox VM (no USB needed; afterwards delete it and grow the APFS container);
- a USB stick of 32 GB or more.

## Board facts used by the EFI

- **DSDT** (BIOS 4005): a real EC at `\_SB.PCI0.SBRG.EC0` (PNP0C09, ports 0x62/0x66, no `_STA`). No `EC` or `USBX`
  device exists. Other tables:
  - 15 SSDTs (AMD CPU/CPPC tables, an ASUS PLD table);
  - WPBT (ASUS's Windows platform binary, ignored by macOS);
  - IVRS (AMD IOMMU).
- **USB controllers:**

  | Controller | USB 2 ports | USB 3 ports |
  |---|---|---|
  | `0c:00.0` (chipset) | 12 | 5 (17 in total, over macOS's 15 per controller) |
  | `0e:00.3` | 2 | 2 |
  | `0e:00.4` | 2 | 2 |
  | `0f:00.0` | 1 | 0 |

  The keyboard (Hama) and mouse are on `0c:00.0` and `0e:00.4` (Linux bus 1 / bus 5).
- **ReBAR:** on (32 GB BAR0 under Linux). macOS gets the default 256 MiB through `ResizeAppleGpuBars = 0`.

## Risks found while researching

- **AM5 firmware with 9000-series AGESA** has been reported to hang macOS at boot. On a B650E-F above BIOS 2613 it
  needed a DSDT fix ([ryanilano's B650E-F EFI](https://github.com/ryanilano/hackintosh-asus-b650ef-amd),
  [macos86 thread](https://macos86.it/topic/6542-bug-fix-for-am5-new-firmware-motherboards-dsdt-fix/)). A B850 board
  hung at `AMDCPUSupport::start trying to init PCI service` after an AGESA 1.3 update
  ([amd-osx thread](https://forum.amd-osx.com/threads/help-tuf-b850m-plus-wifi7-9800x3d-stuck-at-amdcpusupport-start-trying-to-init-pci-service-after-bios-1087-%E2%86%92-1686-agesa-1-3-0-1b.6534/)).
  This board runs BIOS 4005 (2026-09-18), so expect it. Things to try, in order:
  1. AMD_Vanilla's `IOPCIIsHotplugPort` patch (disabled by default);
  2. ACPI `RebaseRegions`;
  3. a DSDT fix written against this board's DSDT.
- **I225-V on Tahoe:** documented working on 26.7.1 with AppleIntelI210Ethernet 2.3.1 + `device-id 0x1533` +
  `e1000=0` ([GeekAudio/intel-i225-tahoe-fix](https://github.com/GeekAudio/intel-i225-tahoe-fix)). Not added to the EFI
  until the built-in driver has been tried.
- **AppleMCEReporterDisabler:** no longer hosted by Dortania. The repo keeps acidanthera's v1.2 plist as source.
