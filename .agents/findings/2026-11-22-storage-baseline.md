# 2026-11-22: NVMe and APFS on the XTX machine, inventory and first storage baseline

> **Correction (2026-11-23, thread 00, from thread 03's final report):** don't quote the 9.54 GB/s sequential write
> as typical: the same prefill job measured 2.79 GB/s in the exploratory run and 9.54 GB/s in the final one; write
> speed follows the SLC cache state. "SLC lasted ~190 GB" is one sample after ~0.3 TB of earlier writes. For the
> Linux raw read use the LUKS partition (`p2`, all real data), and the variable goes before the command:
> `sudo FIODEV=/dev/nvme0n1p2 fio --readonly --thread tools/storage/raw-read.fio`. "Writes 60-80 GB per run" is from
> one run, untested since the layout fix. Only the TRIM *capability* is verified, not runtime TRIM.

Thread 03 (Storage & Filesystems). Everything here was measured on the XTX machine under macOS 26.7.1 (25G241),
EFI `vermilion` stage as in [PROJECT_KNOWLEDGE.md](../../docs/PROJECT_KNOWLEDGE.md) "Known-good configuration"
(VermilionBoot b150, NVMeFix 1.1.3), boot-args `-v keepsyms=1 debug=0x100 pci=0x8000000 amfi=0x80
ipc_control_port_options=0 phxdebug=1`, desktop at 1440p, nothing else running. Nothing on the boot path was changed.
Tools: [`tools/storage/`](../../tools/storage/) (fio 3.38 and smartmontools 7.5, built from source into `~/.local`).

## Inventory

| Item | State | Evidence |
|---|---|---|
| Drive | Corsair MP700 PRO 1 TB, Phison E26 (PCI `1987:5026`), firmware EQFM22.3, NVMe 2.0, 1 namespace, 512 B sectors | `smartctl -x`, `system_profiler SPNVMeDataType` |
| Driver | Apple `IONVMeController` (IONVMeFamily), `quirks = 0`, command pool 256, max transfer 512 KiB (= the drive's MDTS of 128 pages) | `ioreg -r -c IONVMeController` |
| PCIe link | **Gen 5 x4 (32 GT/s), at both ends.** NVMe `LinkStatus 0x7045`, root port GPP1 (`PciRoot(0x0)/Pci(0x1,0x2)`, a CPU lane) `LinkStatus 0x7045`; capability Gen 5 x4, ASPM L1 only | `ioreg` `IOPCIExpressLinkStatus`, decoded per the PCIe spec |
| "Link Speed" in System Information | empty: it has no label for 32 GT/s. Cosmetic | as above |
| NVMeFix | working: `"apst" = Yes` on the controller (without NVMeFix, Apple's driver leaves APST off on third-party drives, per NVMeFix's documentation) | `ioreg` |
| Power states | PS0 11.5 W, PS1 8 W, PS2 6 W, PS3/PS4 0.144 W (non-operational, 3 ms entry / exit). IOReport "NVMe Power States": ACTIVE 100 % under load and idle (no OFF / PAUSE) | `smartctl -x`, `tools/storage/nvmeior` |
| Host throttling | none. `ThermalThrottlingSupported = No`; IOReport "BW Limits" scale factors stay 0, and the tier "throttle time" counters just equal the elapsed time, idle and under load (they are for Apple's own SSDs) | `ioreg`, `nvmeior` idle and during a 6.9 GB/s read |
| TRIM | supported (`TRIM Support: Yes`), block layer `IOStorageFeatures Unmap = Yes`. Per-command activity is not observable from macOS *(unverified that APFS issues it at runtime)* | `system_profiler`, `ioreg` |
| Barriers | `IOStorageFeatures Barrier = No`: `F_BARRIERFSYNC` falls back to a full cache flush (measured below) | `ioreg`, `fsynclat` |
| SMART | PASSED. Critical warning 0, spare 100 %, **2 % used**, 0 media / integrity errors, error log empty, no self-test run. Warning (87 C) and critical (89 C) temperature time: 0. Power-on 4,854 h, 1,296 power cycles, **212 unsafe shutdowns** (baseline: see "Open") | `smartctl -x` |
| Partitions | `disk0s1` EFI 2.1 GB (shared with Limine), `disk0s2` Linux LUKS 890.7 GB, `disk0s3` APFS container 83.8 GB, `disk0s4` "Install macOS Tahoe" HFS+ 23.6 GB (kept, owner 2026-11-22) | `diskutil list` |
| APFS | one container, 53 % used (39-42 GB free). System volume **sealed**, booted from its sealed snapshot (`com.apple.os.update-…`); Data, Preboot, Recovery, VM separate. FileVault off. Case-insensitive | `diskutil apfs list` |
| Time Machine | no destination, no local snapshots | `tmutil destinationinfo`, `tmutil listlocalsnapshots /` |
| Spotlight | indexing enabled on `/` | `mdutil -s /` |
| disk0 "external" | see below | |

### Why macOS calls the NVMe "external"

`diskutil` and System Information report `Device Location: External`; the controller has `Physical Interconnect
Location = External`. The root port GPP1 sets "slot implemented" in its PCIe capabilities (`0x142`, bit 8; not hot-plug
capable), and neither the NVMe nor its port carries a `built-in` property (nothing describes it in ACPI). Effects seen:
the label only; the drive boots and is indexed. The usual fix is the DeviceProperty `built-in = <01>` on
`PciRoot(0x0)/Pci(0x1,0x2)/Pci(0x0,0x0)` *(path from ioreg, not checked with gfxutil; fix unverified)*. `efi/build.py`
writes `DeviceProperties` empty today (line 232), so it needs a `device_properties` field in the machine profile, then
one EFI change and one boot. Not done: waiting for the owner.

## Baseline (21:30, 1 h 03 min after boot, `tools/storage/nvmebench.sh`)

`RAWDEV=/dev/rdisk0s2` (the Linux LUKS partition, read-only), APFS files in the home folder, temperature sampled every
2 s: 51-61 C during the run. The SSD had already absorbed about 1.2 TB of test writes in the hour before (see "Cost"),
so its SLC cache was not in a rested state.

| Job | Result | Latency p50 / p99 / p99.9 | CPU usr / sys |
|---|---|---|---|
| APFS seq read 1 MiB, 8 threads | **9.13 GB/s** | 881 / 2310 / 5931 us | 1 / 36 % |
| APFS seq write 1 MiB, 8 threads (prefill, fresh files) | 9.54 GB/s | 758 / 4358 / 6980 us | 3 / 55 % |
| APFS seq write 1 MiB, 8 threads (overwrite, right after) | 1.73 GB/s | 5538 / 8585 / 10551 us | 1 / 11 % |
| APFS rand read 4 KiB QD1 | 19.9 k IOPS (81 MB/s) | 48 / 77 / 89 us | 2 / 13 % |
| APFS rand write 4 KiB QD1 (`F_NOCACHE`, not durable) | 125 k IOPS | 8 / 10 / 26 us | 9 / 91 % |
| APFS rand read 4 KiB, 8 threads | 133 k IOPS (543 MB/s) | 57 / 90 / 115 us | 16 / 126 % |
| APFS rand write 4 KiB, 8 threads (not durable) | 326 k IOPS | 21 / 86 / 155 us | 30 / 630 % |
| raw seq read 1 MiB, 1 thread | 6.47 GB/s | 146 / 375 / 518 us | 1 / 16 % |
| raw seq read 512 KiB, 16 threads | 6.98 GB/s | 1188 / 2343 / 2834 us | 2 / 19 % |
| raw seq read 1 MiB, 16 threads | 7.00 GB/s | 2474 / 4293 / 5145 us | 1 / 20 % |
| raw rand read 4 KiB QD1 | 20.9 k IOPS | 59 / 74 / 90 us | 2 / 9 % |
| raw rand read 4 KiB, 32 threads | 454 k IOPS (1.86 GB/s) | 76 / 161 / 218 us | 64 / 342 % |
| raw rand read 4 KiB, 64 threads | 558 k IOPS (2.29 GB/s) | 115 / 230 / 469 us | 85 / 502 % |
| `fsync()` 4 KiB (not durable) | 71 k/s | 14 / 25 us, max 106 us | |
| `F_BARRIERFSYNC` 4 KiB | 651/s | 1535 / 2123 us, max 18.5 ms | |
| `F_FULLFSYNC` 4 KiB | 668/s | 1498 / 1923 us, max 8.9 ms | |

The run's `seq-read-512k-t16` line (19.2 GB/s) is invalid: 8 of its 16 files had never been written (fixed in
`apfs.fio`). An earlier exploratory run (21:10-21:20, same tools by hand) agrees within a few percent on every read and
on the QD1 numbers.

### Sustained write (21:16, 180 s, 8 threads x 1 MiB over 16 GiB of files)

886 GB written, 4.92 GB/s average. Per 5 s: 9.6 GB/s for 20 s, then it alternates between ~1.46 GB/s and bursts back
to 4-9.6 GB/s. Temperature 54-65 C the whole time; SMART warning / critical temperature time stayed 0. **No thermal
throttling**: the pattern is the SLC cache running out (~190 GB at full speed) and the drive folding into TLC at
~1.46 GB/s, recovering whenever it catches up.

## What the numbers say

1. **The link and the drive are fine.** Gen 5 x4 negotiated, APFS reads 9.1 GB/s and fresh writes 9.5 GB/s, which is
   most of a Gen 5 x4 link (~15.7 GB/s raw, minus protocol overhead). The drive's rated speeds are higher (Corsair
   quotes up to ~11.7 GB/s read for the 1 TB model *(from memory, unverified)*); the Linux run below will say how much
   of the gap is macOS.
2. **The raw device path is slower than APFS**: `/dev/rdisk0s2` stops at ~7.0 GB/s whatever the thread count, and gets
   *slower* with bigger requests (bs sweep, 8 s each: 256 KiB x16 5.9, 512 KiB x16 6.9, 2 MiB x8 5.4, 4 MiB x4 3.7,
   8 MiB x8 5.4 GB/s). The kernel's raw-device path splits large requests into 512 KiB commands and evidently doesn't
   keep enough of them in flight. Apps don't use it; don't read it as the drive's limit.
3. **Random I/O is CPU-bound in the kernel**: at 64 threads, 558 k IOPS cost five cores of system time, about 9 us of
   CPU per 4 KiB read. The drive rates far more IOPS (~1.4 M *(unverified)*). Apple's NVMe driver uses one I/O queue
   *(unverified; it would explain the lock-bound scaling)*. Not fixable from configuration.
4. **QD1 latency is good**: 48-59 us per 4 KiB read (APFS / raw); APFS adds nothing measurable at QD1.
5. **Durability is expensive, as on any consumer SSD without power-loss protection**: ~1.5 ms per real flush, 650-670
   per second, and `F_BARRIERFSYNC` costs the same because the drive has no barrier support. Apple SSDs make barriers
   cheap; here every SQLite / Core Data commit that uses one pays a full flush. Inherent, no setting changes it.
6. **APFS copy on write fragments randomly overwritten files**: after the random-write jobs, a 512 KiB x8 sequential
   read of the same 8 files gave **2.04 GB/s** (p50 2.0 ms), against 9.1 GB/s before. Affects databases, VM images,
   and anything that does in-place random writes *(cause inferred, extents not dumped)*.
7. **Write speed depends on the SLC cache state**: 9.5 vs 1.7 GB/s minutes apart. A possible aggravating factor, not
   checkable from macOS: if the Linux LUKS mapping doesn't pass discards (dm-crypt's default), the SSD never learns
   which of the 890 GB are free, and its dynamic SLC cache shrinks for both OSes *(unverified)*.

## Cost of this investigation

SMART "Data Units Written" went from 20.9 TB to 22.1 TB: **about 1.2 TB written**, about 0.9 TB of it from the
time-based sustained test, which should have been capped by volume. Wear indicator unchanged at 2 %. The tools now
cap writes by volume (60-80 GB per `nvmebench.sh` run); see the pitfalls in
[`tools/storage/README.md`](../../tools/storage/README.md).

## Open

- **Linux reference run** (next Linux boot, needs the owner): the same `raw-read.fio` with
  `fio --readonly --thread`, `FIODEV=/dev/nvme0n1p3` (the macOS APFS partition, read-only, real data in its used half)
  or the LUKS partition, `ioengine=psync` for a like-for-like run plus `io_uring` / `iodepth=32` for the drive's
  ceiling. Same day, check discard: `lsblk --discard`, `cryptsetup status <mapping>` (flags `discards`),
  `systemctl status fstrim.timer`.
- **Unsafe shutdowns = 212** at 21:07. Read it again after the next macOS restart and the next macOS shutdown
  (`smartctl -A disk0 | grep Unsafe`): if it goes up, macOS isn't shutting the NVMe down cleanly (relevant to the
  restart panic in the problem index).
- **"external"**: DeviceProperty `built-in` (see above), one EFI change, owner's OK first.
- Time Machine: verify once the owner attaches a backup disk (first backup speed, local snapshots, `tmutil`).
- TRIM at runtime: not observable from macOS; Linux can't see it either. Low priority.
