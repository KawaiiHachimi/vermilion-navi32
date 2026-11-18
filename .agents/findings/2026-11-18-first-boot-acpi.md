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

## Second boot: the DSDT loaded, the hang stayed (same line)

With the Scope wrap the log showed `ACPI: 17 ACPI AML tables successfully acquired and loaded` (no ACPI errors), but
the boot still stopped right after `CoreAnalyticsHub start completed`. So the ACPI errors were not the (only) cause.

Research, sources read:

- [nullmoth/1401 issue 21](https://github.com/nullmoth/1401/issues/21): ASUS X870E Hero shows the same hang, the
  same DSDT pattern (`If (G002 ...)` blocks, top-level Scopes into them) and the same DSDT load failure. A commenter with an ASUS TUF
  B650M-PLUS WIFI got past the hang with 4 DSDT patches by etorix ("standard AM5 set", from an EFI shared on
  forum.amd-osx.com). The patches replace the `If (G002 != 0x03 && G000 == One ...)` headers with AML `NOP` (0xA3), so
  the gated chipset blocks become unconditional.
- forum.amd-osx.com "Errors Booting macOS 26": the stall means PCI device initialisation fails; USB drops at that point
  on AM5 (could not be read directly: HTTP 429).

Why: the gated blocks hold the whole chipset (`GPP7.UP00`: its USB with the mouse, SATA, LAN, Wi-Fi). Making the table
load still leaves those devices to late module-level code, and macOS hangs there.

## Fix v2: resolve the conditions from the Linux namespace

Instead of NOP-ing every gated header (the community patches also enable the inactive `G000 == 0x02` layout),
`fix_dsdt.py resolve` uses the machine's Linux ACPI namespace (`/sys/bus/acpi/devices/*/path`, saved by `acpi-dump`
as `namespace.txt`):

| Block (DSDT line) | Linux | Action |
|---|---|---|
| 12901 `G002 != 3 && G000 == 1` (chipset PCIe tree) | 26/26 devices | unwrap |
| 13497 `... && G004 != 0x43FA1022` (chipset xHCI + ports) | 18/18 | unwrap |
| 15053, 15079, 15461 (NVMe/slot leaves) | 1/1 each | unwrap |
| 14066 `G000 == 2` (other chipset layout) | 0/28 | remove |
| 15105, 15435, 15426 `G003 != 0` (second chipset, X670 only) | 0 | remove |
| 12421 (power button, has Else), 15040, Scope-only blocks on `\_GPE` / unconditional devices | n/a | left as is |

Check with `acpiexec` (which reads the condition fields as 0, like a late or failed evaluation): the original table
has no `GPP7.UP00` devices at all, while the resolved table loads cleanly with `UP00`, `DP58.WIFI`, `XH00` and all
`RHUB.POTx`. The Scope wrap is no longer needed (nothing top-level points into a conditional device any more).
Installed on `/boot` for the third boot.
