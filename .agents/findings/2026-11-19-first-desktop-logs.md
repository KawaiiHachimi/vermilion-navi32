# 2026-11-19: the first desktop, read from macOS's own logs

The first normal boot (Setup Assistant, then the desktop, about 10 minutes) was "very extremely slow": no Dock,
and a menu bar that did not respond. The owner then powered off. The unified log of that boot was read on Linux:

- `apfs-fuse` read-only mount of the Data volume (`-v 0`), see [runbooks.md](../runbooks.md);
- `private/var/db/diagnostics` and `private/var/db/uuidtext` copied into one folder (a logarchive layout);
- decoded with mandiant/macos-UnifiedLogs `unifiedlog_iterator -m log-archive` (commit `3d5b27a`, builds with plain
  `cargo build --release` in `examples/unifiedlog_iterator`): 810k entries for this boot.

## Why it is slow

- **WindowServer composites on the CPU:** `Software compositor activated.` (SkyLight) and a frame interval of
  `refresh_interval=33333333` (30 Hz). Core Animation logs `Render timed out fence ...` and
  `Render synchronize timed out` while Setup Assistant runs. `runningboardd` logs
  `setGPURole:pid: failed due to nil IOGPU device ref` 261 times: there is no GPU device at all. Every frame of
  the 2560x1440 desktop is drawn by the CPU. Only a Metal driver (Phase 2, Vermilion) changes this.
- **The first login's background work runs at the same time:** the most active loggers are mobileassetd,
  runningboardd, efilogin-helper, storagekitd, iconservicesagent, lsd, chronod (widgets), mds (Spotlight),
  modelcatalogd and sirittsd. They compete with the compositor for the CPU.
- **No sign of a timing (TSC) or CPU fault:** no TSC, watchdog or panic messages. Side effect of SSDT-CPUR:
  `X86PlatformPlugin` attaches and logs `Unsupported CPU`, and `_PSS`/`_CST` fail for `PR00..PR17`. On AMD the
  firmware controls the clocks anyway (CPPC), so this is expected to be harmless (unverified: no clock measurement in
  macOS yet).

## Other things the log shows

- **Ethernet (I225-V):** Apple's `AppleEthernetE1000` DriverKit driver matches, then exits with
  `provider entitlements check failed`. That is the known I225 failure on Tahoe; the documented fallback
  (AppleIntelI210Ethernet 2.3.1 + `e1000=0`, TODO Phase 1) is the next thing to try.
- The display is set offline and online several times (`CGXVFBSelectOnlineState`, 88 reconfigure notifications)
  while Setup Assistant and loginwindow change places. The GOP framebuffer stays the only display.

## Decision

The owner can't use this desktop (no Dock, menu bar unresponsive), so Phase 1's remaining items wait. Phase 2
(Vermilion on the XTX) comes next, installed without relying on the desktop: build on macbox, put the files on the
EFI partition from Linux, and install them with one script from Terminal or Recovery's Terminal.
