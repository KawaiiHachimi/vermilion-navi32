# 2026-11-19: the "reboot" after the install was the installer's own

## What the owner saw

After the GUI phase of the install, a boot of the new disk printed verbose text. The Apple logo showed for a moment,
then a few more lines, then the machine restarted. There was no panic text. With `debug=0x100` a kernel panic halts
on the panic screen and does not reboot, so a reboot without panic text pointed to something else.

## What the logs show (read on Linux, no extra boot)

The OpenCore log files on the EFI partition (`/boot/opencore-*.txt`, `Target = 67`, boot.efi messages through
`AppleDebug`) name the booter path and kernel flags of every boot. The boots after the GUI phase (times in UTC):

| Log | boot.efi | Kernel flags from `com.apple.Boot.plist` | Phase |
|---|---|---|---|
| 01:04 | `com.apple.installer\boot.efi` | `-rootdmg-ramdisk auth-root-dmg=.../BaseSystem.dmg lca-boot-mode=autoinstall-msu` | MSU install ("About 27 minutes remaining") |
| 01:09 | `com.apple.installer\boot.efi` | `-restore -logOnCrash -rootdmg-ramdisk auth-root-dmg=.../x86_64SURamDisk.dmg` | apply the update to the sealed system volume |
| 01:11 | `System\Library\CoreServices\boot.efi` (Preboot) | `-bsdmgroot` | "Boot 1": template migration on the installed system |

All three reach `EXITBS:START`. `install.log` on the Data volume shows how the last one ends:

```
01:12:23 OSInstaller: ISAP: Done with Phase "Template Migration"
01:12:23 OSInstaller: IASGetCurrentInstallPhaseList: phases = ( "Boot 1", "Language Chooser" (100) )
01:12:23 OSInstaller: Triggering reboot
```

So the restart the owner saw was the last planned installer reboot. The next boot of Macintosh HD is the first
normal one, where Setup Assistant (Language Chooser) should start. That boot has not happened yet. Not yet set up:
`/var/db/.AppleSetupDone` is absent and `/Users` holds only `Shared`.

Side checks, from the same `install.log`:

- The firmware updater ran and changed nothing: `efiupdater` read `EFI ROM 2094.80.5` (OpenCore's MacPro7,1 value)
  and reported `no firmware update needed`. The DP-to-HDMI and SD firmware updaters were only staged.
- `restore.log` on the Update volume ends without errors. `last_update_result.plist` says 25G241 -> 25G241.

## How to read the macOS disk from Linux

`apfs-fuse` (sgan81/apfs-fuse, commit `66b86bd`, read-only) builds on current Arch with two workarounds. The steps are
in [runbooks.md](../runbooks.md). Volume indexes on p3: 0 Data, 1 Preboot, 2 Recovery, 3 System, 4 Update. Useful
files: Data `private/var/log/install.log`, Data `Library/Logs/DiagnosticReports/` (panics, empty so far), Update
`restore.log`.

## Lesson

Before treating an unexpected reboot as a failure, read the OpenCore log for that boot's kernel flags, and
`install.log`. Both are readable from Linux and cost no test boot.
