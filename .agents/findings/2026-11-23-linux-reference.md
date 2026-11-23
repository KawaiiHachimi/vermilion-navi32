# 2026-11-23: Linux reference numbers on the XTX machine (thread 02)

One Omarchy boot, following [HANDOFF-LINUX-REFERENCE.md](../HANDOFF-LINUX-REFERENCE.md) (on `ownback/perf-power`),
sections 0-5. Everything was read-only: no register writes, no EFI or `efibootmgr` change, nothing under `/boot/EFI`,
crypttab untouched. Root only through T3's `request_secret` (file delivery). Two packages were installed because
the handoff needs them: `turbostat` 7.2.11 and `fio` 3.42 (Arch `extra`). Raw output: `~/vermilion-captures/linux-ref-20261123-0641/`
on Linux, and a copy of the serial-free files on the ESP in `vermilion/results-02/`.

## Context

| Item | Value |
|---|---|
| Kernel / BIOS | `7.2.15-1-omarchy` / `4005`, booted directly (Limine, not through OpenCore) |
| Desktop | Hyprland 0.56.2, one monitor on DP-3, 2560x1440. VRR off in every block |
| Running | T3 Code with this Claude session (one terminal pane streaming output). Nothing else opened |
| Governor | `amd-pstate-epp`, status `active`, governor `performance`, EPP `performance`, boost 1 |
| CCDs (`ccd.txt`, `cache/index3/size`) | **CPUs 0-5,12-17: V-Cache CCD (96 MB L3)**; **CPUs 6-11,18-23: frequency CCD (32 MB L3)**. Both report `cpuinfo_max_freq` 5663303 kHz; prefcore ranking 191 (V-Cache) vs 211 (frequency) |

| Block | Time (CET) | Minutes since boot | Display mode |
|---|---|---|---|
| 0 setup | 06:41 | 1 | 240 Hz SDR (XRGB8888, `cm srgb`) |
| 1 GPU idle, 240 SDR (two runs, a/b) | 06:42-06:48 | 4-8 | 239.97 Hz SDR |
| 1 GPU idle, 240 HDR | 06:48-06:51 | 9-11 | 239.97 Hz HDR (XRGB2101010, 10 bpc, `cm hdr`) |
| 1 GPU idle, 60 SDR | 06:51-06:54 | 12-14 | 59.95 Hz SDR |
| 1b vkbench | 06:57-06:59 | 17-19 | 239.97 Hz SDR |
| 1c gpuinfo / VRAM | 06:59 | 19 | 240 SDR |
| 2 CPU | 07:00-07:01 | 20-21 | 240 SDR |
| 3 storage | 07:01-07:04 | 21-24 | 240 SDR |
| 4 sensors | 07:01 | 21 | 240 SDR |

Each mode was set with `hyprctl eval 'hl.monitor({...})'` (Hyprland 0.56 refuses `hyprctl keyword`; the first
attempt with `keyword` silently stayed at 240 SDR, so those two runs are kept as the 240 SDR a/b data), and the
measurement started 120 s after the switch.

## 1. GPU idle: power, clocks, SMU features

`power1_average` is the amdgpu hwmon **PPT** (`sensors` labels it `PPT`, cap 327 W): the ASIC's package power,
not total board power. It is the analogue of macOS's *socket* power (49-56 W), not of the 62-70 W board figure.

| Mode | PPT, 30 x 1 s (W) | UCLK (`pp_dpm_mclk`), 30 x 1 s | FCLK | SOCCLK | SCLK | DCEFCLK | VDDGFX | Temps edge / junction / mem |
|---|---|---|---|---|---|---|---|---|
| 60 Hz SDR | **mean 9.9, 7-13** | **96 MHz (level 0) 30/30** | 1000 MHz (level 1) 30/30 | 600 MHz | 1-3 MHz (deep sleep `S`) | 200 MHz | 94 mV | 38 / 45 / 62 C |
| 240 Hz SDR (a) | mean 23.3, two levels: ~10 and ~37 | 96 MHz 25/30, 456 MHz 5/30 | 1000 MHz 30/30 | 600 MHz | 3-9 MHz (`S`) | 200 MHz | 132 mV | 35 / 43 / 62 C |
| 240 Hz SDR (b) | mean 22.6, two levels: ~11 and ~37 | 96 MHz 23/30, 456 MHz 7/30 | 1000 MHz 30/30 | 600 MHz | 1-3 MHz (`S`) | 200 MHz | — | 37 / 46 / 64 C |
| 240 Hz HDR | **mean 35.2, 11-51** | 456 MHz 22/30, 96 MHz 6/30, 772 MHz 2/30 | 1000 MHz 28/30, 1200 MHz 2/30 | 600 MHz | 3 MHz (`S`) | 200 MHz | — | 41 / 49 / 66 C |

GPU fan 0 RPM in every mode (zero-RPM). `power_dpm_force_performance_level` = `auto`, power profile `BOOTUP_DEFAULT`.
`pp_od_clk_voltage` does not exist (overdrive not enabled). `amdgpu_pm_info` agrees ("average SoC" 10-11 W at 60 SDR,
MCLK 96 MHz, `VCN: Powered down`).

The DPM tables (identical in all modes):

| Clock | Levels |
|---|---|
| SCLK | S (deep sleep), 500, 2482 MHz |
| UCLK | 96, 456, 772, 1249 MHz |
| FCLK | 601, 1000, 1200, 1600, 2000, 2201, 2251, 2301 MHz |
| SOCCLK | 500, 600, 1500 MHz |
| DCEFCLK | 145, 200, 1563 MHz |

What it answers for macOS:
- **UCLK is not pinned on Linux.** At idle it sits at the *lowest* level, 96 MHz, all the time at 60 Hz and most of
  the time at 240 Hz SDR. It rises to 456 MHz when the screen updates (likely the terminal pane streaming output;
  not checked against a static screen), and at 240 Hz HDR it is at 456 MHz most of the time. macOS pins it at 456 MHz
  hard-min, so the pin costs the 96 MHz state. In the 240 SDR samples that state is worth about 25 W of PPT
  (10 W at 96 MHz vs ~37 W at 456 MHz).
- **FCLK idles at 1000 MHz (level 1 of 8)**, not 2301 MHz as on macOS. SOCCLK idles at 600 MHz (level 1).
- **Linux idles at 10 W PPT at 60 Hz SDR and 35 W at 240 Hz HDR**, against macOS's 49-56 W socket at 240 Hz HDR.
  The mode matters on Linux: the 240 Hz HDR desktop costs ~25 W over 60 Hz SDR.
- **SMU feature mask: `0x0003ebb8_71ffffff`, exactly macOS's (`0x71ffffff` / `0x0003ebb8`).** The difference is not
  which features are on but the DPM limits macOS sets (UCLK hard-min) and what keeps FCLK up.
  Disabled bits: 25 SMARTSHIFT, 26 GTHR, 27 ACDC, 31 GFX_DCS, 32 GFX_READ_MARGIN, 33 LED_DISPLAY,
  34 GFXCLK_SPREAD_SPECTRUM, 38 BOOT_TIME_CAL, 42 GFX_EDC, 44 CLOCK_POWER_DOWN_BYPASS. Everything else is enabled,
  including 19 GFXOFF, 16 DS_UCLK, 13 DS_FCLK, 41 DF_CSTATE.
- **GFXOFF residency: not available.** `amdgpu_gfxoff_residency`, `amdgpu_gfxoff_count` and `amdgpu_gfxoff_status`
  all return `EINVAL` on reads. Starting the residency log takes a debugfs write, which this read-only session
  doesn't do. Indirect evidence that GFX powers off: SCLK reads 1-9 MHz (`S`), VDDGFX 94-132 mV, `gpu_busy_percent` 0.
- Clock gating flags mask `0x3bc080301` (FGCG, MGCG, MC LS/MGCG, BIF MGCG, HDP SD, IH CG, repeater FGCG, perfmon CG,
  ATHUB MGCG/LS on).

## 1b. vkbench (the same Mandelbrot shader through Vulkan)

RADV from Mesa 26.2.5 (`RADV NAVI31`), system loader, 2560x1440 at 240 Hz SDR, nothing else running, 5 s between tests.

| Test | Linux GPU per frame, median (avg / p99) | Linux wall per frame, median (p99) | macOS (2026-11-23, b155, RADV directly) |
|---|---|---|---|
| `compute 20 64` | **0.134 ms** (0.134 / 0.135); 6756 frames/s | 0.148 ms (0.199) | 0.41 ms GPU |
| `compute 20 2000` | **4.054 ms** (4.058 / 4.141); 245 frames/s | 4.076 ms (4.193) | 3.95 ms GPU |
| `submit 10` (16x16, 1 it) | 0.002 ms | **37 us** (63 us); 25,567/s | median 34 us |
| `submit 10 200 512 512` | 0.070 ms | **114 us** (214 us); 8,529/s | median 265 us |
| `submit 10 2000 1024 1024` | 1.138 ms | 1.190 ms (1.281) | — |
| `bandwidth 10 256` | 0.652 ms | 0.671 ms | **823 GB/s median (best 842)** vs macOS 44.7 GB/s |
| `bandwidth 10 16` | 0.014 ms | 0.021 ms | 2,410 GB/s median (best 2,759): 2 x 16 MiB fits the 96 MB MALL |

Buffer memory on Linux: memory type 0, flags `0x1` (DEVICE_LOCAL), heap 1 (24,560 MiB, DEVICE_LOCAL).

During `compute 20 2000` (`vkbench-load.txt`, 1 s samples): PPT 296-309 W, SCLK 2518-2521 MHz (above the 2482 MHz
table top), `gpu_busy_percent` 100, **UCLK at 96 MHz for most of the run** (456 MHz in the first 2 and the last 3
samples), FCLK 1000 MHz throughout. The Mandelbrot loop doesn't touch memory, so the SMU leaves UCLK at its lowest level.

Reading:
- The long shader (`compute 2000`) takes about the same time on both (4.05 vs 3.95 ms). Linux clocks SCLK at ~2.52 GHz
  for it.
- The short one (`compute 64`) is 3x faster on Linux (0.134 vs 0.41 ms). The difference is per-dispatch overhead or
  the clock ramp, not shader throughput.
- Bandwidth is the large gap: 823 GB/s on Linux, which is VRAM (960 GB/s theoretical, `RADV_DEBUG=info`). macOS's
  44.7 GB/s says macOS's buffers are not in VRAM, which fits the VRAM-backed-buffers design work.
- The bare submit costs about the same on both (37 vs 34 us). With a 0.07 ms GPU job, Linux waits 114 us and macOS 265 us.

## 1c. The XTX's device profile

`gpuinfo.txt`, whole (no serials):

```
/sys/devices/pci0000:00/0000:00:01.1/0000:01:00.0/0000:02:00.0/0000:03:00.0
asic_id=0x744c chip_rev=0 ext_rev=0x1 pci_rev=0xc8 family=145
gb_addr_cfg=0x545 num_se=6 sa_per_se=2 cu_active=96 rb_pipes=24 rb_mask=0xffffff num_tcc=24 tcc_disabled=0 vram_bit_width=384 vram_type=9
max_sclk_khz=2482000 max_mclk_khz=1249000 gl2c_kb=6144 mall_bytes=100663296 pcie_gen=4 pcie_lanes=16
cu_bitmap: 0xff 0xff 0xff 0xff 0xff 0xff 0xff 0xff 0xff 0xff 0 0 0xff 0xff 0 0
vram_bytes=25700798464 vram_cpu_visible=25700798464 gtt_bytes=25051697152
fw me    ver=2540 (0x9ec) feature=29
fw pfp   ver=2710 (0xa96) feature=29
fw mec   ver=2790 (0xae6) feature=29
fw rlc   ver=128 (0x80) feature=1
fw mes   ver=146 (0x92) feature=1
fw smc   ver=5146368 (0x4e8700) feature=0
fw sos   ver=3211318 (0x310036) feature=3211318
fw dmcub ver=117453568 (0x7003300) feature=0
fw vcn   ver=152141858 (0x9118022) feature=0
fw sdma  ver=27 (0x1b) feature=60
```

`vulkaninfo` is not installed, so `radv-info.txt` is the `RADV_DEBUG=info` dump that `vkbench submit 1` prints
(239 lines). The key values: NAVI31, `gfx_level` 14 (GFX11), `family` 87 / `family_id` 145, 6 SE, 24 RB, 96 CU,
max GPU clock 2482 MHz, L2 6144 KB, L3 (MALL) 96 MB, 24 memory channels, 384-bit, `memory_freq` 20 GHz,
960 GB/s, PCIe 4 x16, `vram_size` 24576 MB, `vram_vis_size` 24560 MB (resizable BAR: all of VRAM CPU-visible),
`gart_size` 23907 MB, `pte_fragment_size` 2 MiB, `has_set_context_pairs_packed` 1, `has_dcc_constant_encode` 1,
`rbplus_allowed` 1, `use_display_dcc_with_retile_blit` 1. Queues: GFX 1, COMPUTE 4, SDMA 2, VCN 1, JPEG 1.
Compare it with `patches/mesa/0016` (the XT's canned description): the XTX has 96 CU / 6 SE / 24 RB / 384-bit / 24 GB.

Where Linux keeps its own things in VRAM:

| Item | Value |
|---|---|
| VRAM range (GPU address) | `0x80_0000_0000 - 0x85_FEFF_FFFF`, 24,560 MiB, BAR 32,768 MiB |
| PSP TMR | **0x1300000 (19 MiB) reserved at `0x85_FC00_0000`**, i.e. near the top of VRAM |
| `amdgpu_vram_mm` at 19 min uptime | 24,560 MiB total, 23,626 MiB free (~934 MiB used by the desktop and the kernel), buddy allocator, 4 KiB chunks; no per-range reserved list printed |
| Board | IP discovery `1002:744C`, subsystem `1EAE:7901`, rev `C8` |

## 2. CPU: clock, idle power, C-states

| Measurement | Result |
|---|---|
| 1 thread, frequency CCD (CPU 6), 5 s | **median 5.16 GHz** (5.15-5.18) |
| 1 thread, V-Cache CCD (CPU 0), 5 s | median 5.08 GHz (5.02-5.08) |
| 24 threads, 15 s | median 4.91 GHz (3.82-5.10) |
| `cpuinfo_max_freq` | 5.663 GHz on both CCDs |
| Idle package power (turbostat `PkgWatt`, 30 s, idle desktop at 240 SDR, 21 min uptime) | **37.8 W** |
| Idle C-state residency (turbostat, per-CPU mean) | Busy 0.50 %, C1 0.13 %, C2 1.21 %, **C3 98.10 %** |
| `cpuidle` states (CPU 6) | POLL; C1 `ACPI FFH MWAIT 0x0`; C2 `ACPI IOPORT 0x414`; C3 `ACPI IOPORT 0x415` (at capture: C3 1,156 s of residency, C2 32 s, C1 9.8 s) |
| Temperatures (idle, `k10temp`) | Tctl 52-63 C, Tccd1 47-51 C, Tccd2 43-47 C |

turbostat 7.2.11 printed no `CPU%c1`, `CPU%c6` or `PkgTmp` columns on this Zen 4 (it has no core C-state residency
counters or package temperature for AMD). The ACPI C-states stand in: on Zen the deepest `_CST` I/O-port state
(C3 here, port 0x415) is CC6.

The decision in the handoff:
- **Linux tops out at 5.16 GHz on the frequency CCD too**, the same as macOS (5.15), with governor and EPP at
  `performance`. The cap is not a macOS problem: BIOS settings or temperature (the owner's BIOS photos answer it).
- **Linux idles at 37.8 W package, 2-12 W below macOS's 40-50 W, not 15+ W.** Linux's cores sit in C3 (CC6) 98 % of
  the time. A `_CST` idle kext on macOS would gain at most that 2-12 W, so it is a smaller win than assumed.

## 3. Storage (read-only on the LUKS partition `nvme0n1p2`)

`fio --readonly`, `tools/storage/raw-read.fio`, variable before `fio`. Drive: Corsair MP700 PRO 1 TB (PCIe Gen 5 x4).

| Job | Linux | macOS (2026-11-22) |
|---|---|---|
| `raw-seq-read-1m-t1` (psync) | 4,663 MB/s | — |
| `raw-seq-read-512k-t16` | 6,749 MB/s | — |
| `raw-seq-read-1m-t16` | **6,797 MB/s** | raw 7.0 GB/s, APFS 9.1 GB/s |
| io_uring 1 MiB QD32, 1 job ("ceiling") | 5,041 MB/s (p50 latency 6.0 ms, p99 18.2 ms) | — |
| `raw-rand-read-4k-qd1` | **21.3 k IOPS**, avg 46.7 us (30 % under 20 us, 67 % 50-100 us) | ~20 k IOPS |
| `raw-rand-read-4k-t32` | 524 k IOPS (2,147 MB/s) | — |
| `raw-rand-read-4k-t64` | 753 k IOPS (3,083 MB/s) | — |

All reads go through the raw partition, below dm-crypt, so there's no decryption cost. The io_uring QD32 single job
being slower than 16 psync threads says one submitting thread is the limit there, not the drive.
Linux reads the raw device at the same speed as macOS (6.8 vs 7.0 GB/s).

LUKS discards:

| Item | State |
|---|---|
| `lsblk --discard` | `nvme0n1`, `p1`, `p2`, `p3`: DISC-GRAN 512 B, DISC-MAX 2 TB. **`root` (the dm-crypt device): DISC-GRAN 0 B, DISC-MAX 0 B = no discard** |
| `cryptsetup status root` | LUKS2, `aes-xts-plain64`, 512-bit key, 512 B sectors, no `flags: discards` line |
| `fstrim.timer` | loaded, **disabled, inactive** |

So nothing on the Linux side ever TRIMs. Turning it on (`discard` in crypttab / `--allow-discards`, and enabling
`fstrim.timer`) is the owner's decision. It leaks which blocks are free. Not changed in this session.

## 4. Super I/O chip and sensors

| Item | Result |
|---|---|
| Board | ASUS ROG STRIX B650E-I GAMING WIFI |
| hwmon devices | `nvme`, `amdgpu`, `k10temp`, `asus` (WMI), `asusec`, 2x `spd5118` (DIMM temps), `mt7921_phy0` |
| Super I/O driver | **none loaded**: no `nct6775` / `it87` in `lsmod`, nothing in `dmesg` |
| `asus-ec-sensors` | loaded: "board has 6 EC sensors that span 4 registers": CPU 41 C, Motherboard 40 C, T_Sensor 0 C (no probe), VRM 46 C. **No fan readings** |
| `nct6775.ko` | its ASUS WMI board list includes "ROG STRIX B650E-I GAMING WIFI", and its chip names include `NCT6796D-S/NCT6799D-R` |
| Motherboard fan RPM | not visible on Linux in this configuration (the GPU's fan reads 0 RPM, zero-RPM mode) |

The chip is a Nuvoton NCT67xx behind ASUS's WMI Super I/O methods: the board's place in `nct6775`'s ASUS-WMI list
says so. The exact model stays **unverified**. `nct6775` isn't autoloaded, and loading it probes the chip (Super I/O
config-mode writes), which this read-only session doesn't do. The likely answer is still NCT6799D-R, which SMCSuperIO in VirtualSMC
1.3.8 supports (*unverified*). One `sudo modprobe nct6775`
in a later Linux session, with the owner's OK, prints the chip ID in `dmesg`.

## Open

- GFXOFF residency on Linux (needs the debugfs logging write).
- The exact Super I/O chip (needs `modprobe nct6775`).
- The 240 Hz SDR/HDR PPT swings with screen activity. A capture with the T3 window hidden or the session idle
  would give a cleaner static-desktop number. The 60 Hz SDR figure (10 W, UCLK 96 MHz throughout) is the clean one.
