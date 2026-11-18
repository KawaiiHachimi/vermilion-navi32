# 2026-11-18: first boot of the installer, and the DSDT fix

## What happened

The `install` stage EFI booted the 26.7.1 installer from p4 (OpenCore log: boot.efi loaded the kernel collection,
`EXITBS:START` at 21:34). The verbose boot then showed (photo by the owner):

```
ACPI: SSDT ... VRMLN ECUSBX ...                               <- our SSDT was injected
ACPI Error: [\_SB_.PCI0.GPP7.UP00.DP58] Namespace lookup failure, AE_NOT_FOUND (20160930/dswload-292)
ACPI Exception: AE_NOT_FOUND, [DSDT] table load failed
... (SSDT:CPMDFIG2 / CDFAAIG2 / CPMDFDG2 / CPMCMN / AMDWOV / ECUSBX) fail on GP17.VGA_, GPP0, GPIO, GP17.ACP_, SBRG.EC0_
ACPI Error: 7 table load failures, 10 successful
```

The boot stopped a little later (after `CoreAnalyticsHub start completed`), with no ACPI namespace to speak of.

## Cause

The BIOS 4005 DSDT defines `\_SB.PCI0.GPP7.UP00` and its `DP58` (the PCIe path to the MT7922 Wi-Fi) **inside a
module-level** `If (((G002 != 0x03) && (G000 == One)))` (line 12901; G000/G002 are fields of the RAM region `CPVS`
at `0x733D9000`). At line 18199 the DSDT opens `Scope (\_SB.PCI0.GPP7.UP00.DP58) { Device (WIFI) ... }` at the
**top level**.

- Current ACPICA (Linux) runs module-level code while it loads the table, so `DP58` exists by line 18199.
- macOS's ACPICA (20160930) defers module-level code until after the load. The Scope can't resolve, so the whole
  DSDT is rejected, and every SSDT that refers to it fails too.

Reproduced on Linux: `acpiexec -l -di DSDT.aml SSDT*.aml` (region simulation reads 0, so the `If` is false) prints
the same `Could not resolve symbol [\_SB.PCI0.GPP7.UP00.DP58]`.

## Fix

[`efi/acpi/fix_dsdt.py`](../../efi/acpi/fix_dsdt.py) wraps each top-level Scope whose target is defined only inside a
module-level `If` in that same condition. On this DSDT exactly one change was made (line 18199). The fixed table
compiles (0 errors) and loads cleanly with all firmware SSDTs and ours in `acpiexec`. `efi/build.py`:

- `acpi-dump` saves the machine's own DSDT to `~/.config/vermilion/acpi/<machine>/` (ASUS's table, not in git) with
  the BIOS version;
- the build fixes and compiles it into `ACPI/DSDT.aml` with `RebaseRegions` on (CPVS and WPBR are RAM regions, both
  with unique names).

A BIOS update needs a new dump. The build refuses a dump from another BIOS version.

## Side findings

- **Booting Linux through OpenCore** (picking it in OpenCore's picker) gives Linux OpenCore's SMBIOS (`Acidanthera
  MacPro7,1`, BIOS `2094.80.5.0.0`) and the injected ACPI tables. With `RequestBootVarRouting`, `efibootmgr` then sees
  OpenCore's private `Boot####` namespace (empty), not the firmware's. It looked as if all boot entries were gone;
  they weren't. Entries created in that state land in OpenCore's namespace (cleaned up again). `acpi-dump` now refuses
  to run under OpenCore unless told the running OpenCore replaces no DSDT.
- `ResetNvramEntry.efi` now gets `--preserve-boot`, so the picker's Reset NVRAM can't wipe the firmware entries.
