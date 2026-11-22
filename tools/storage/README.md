# Storage tools (macOS)

Measure the NVMe the way the [performance record](../../docs/PROJECT_KNOWLEDGE.md#performance-record) asks for:
the same jobs, the conditions written down, and SMART before / after. Results for the XTX machine are in
[`.agents/findings/2026-11-22-storage-baseline.md`](../../.agents/findings/2026-11-22-storage-baseline.md).

| File | What |
|---|---|
| `nvmebench.sh` | the runner: conditions, SMART before / after, SSD temperature every 2 s, then the three tests below |
| `raw-read.fio` | read-only test of a raw partition (`fio --readonly`) |
| `apfs.fio` | file test on the boot volume: 8 prefilled files, sequential and 4 KiB random, reads before writes |
| `fsynclat.c` | latency of `fsync()`, `F_BARRIERFSYNC` and `F_FULLFSYNC` |
| `nvmeior.c` | samples the IOReport `NVMe` group: power-state residency and Apple's bandwidth-throttle tiers |
| `fiosum.py` | one line per fio job from fio's JSON output |

## Install fio and smartctl

There is no Homebrew on the machine; both build from source with the Command Line Tools into `~/.local`:

```sh
# smartmontools 7.5
curl -LO https://github.com/smartmontools/smartmontools/releases/download/RELEASE_7_5/smartmontools-7.5.tar.gz
tar xzf smartmontools-7.5.tar.gz && cd smartmontools-7.5 && ./configure --prefix=$HOME/.local && make -j12 && make install
# fio 3.38: --disable-shm is required, macOS's SysV shared memory is too small ("failed to setup shm segment")
curl -LO https://github.com/axboe/fio/archive/refs/tags/fio-3.38.tar.gz
tar xzf fio-3.38.tar.gz && cd fio-fio-3.38 && ./configure --prefix=$HOME/.local --disable-shm && make -j12 && make install
```

`make install` of fio fails on the man pages under some prefixes; `~/.local/bin/fio` is installed before that.

## Run

```sh
RAWDEV=/dev/rdisk0s2 tools/storage/nvmebench.sh ~/nvmebench-$(date +%Y%m%d)
```

`RAWDEV` must hold real data (on the XTX machine: the Linux LUKS partition; read-only, so it is safe). A run takes
about 6 minutes and **writes 60-80 GB to the SSD**. `SUSTAINED_GIB=N` adds an N GiB sequential write, to watch the
SLC cache run out (about 190 GB on the 1 TB MP700 PRO); it costs N GiB of wear.

## Pitfalls (each one seen on 2026-11-22)

- **Never read unwritten space.** A fresh fio file, or a raw APFS range never written, reads as zeros without
  touching the flash: 19-23 GB/s, above a Gen 5 x4 link.
- **Cap writes by volume, not time.** A 180 s time-based write rewrote the test files at up to 9.6 GB/s and put about
  0.9 TB on the SSD.
- **`direct=1` is `F_NOCACHE`, not `O_DIRECT`.** APFS acknowledges a 4 KiB write in about 8 us, before it is durable.
  Durability costs `F_FULLFSYNC` (`fsynclat`).
- **Raw `/dev/rdiskN` is not the drive's ceiling.** It tops out near 7 GB/s and gets slower with requests above
  512 KiB, while APFS reads the same drive at 9 GB/s.
- **Random overwrites fragment APFS files** (copy on write): sequential reads of the same files dropped from 9.1 to
  2.0 GB/s after the 4 KiB write jobs. Keep sequential reads before random writes.
- Write speed depends on the SSD's SLC cache state: the same 1 MiB write job measured 9.5 and 1.7 GB/s minutes
  apart. Report the state (idle time before the run) with the number.
- fio on macOS: `--thread` and `ioengine=psync` with many jobs. POSIX AIO is 4 kernel threads and 16 requests per
  process (`kern.aiothreads`, `kern.aioprocmax`). `readonly` is a command-line option only.
