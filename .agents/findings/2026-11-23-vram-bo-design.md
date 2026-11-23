# 2026-11-23: VRAM-backed buffer objects, a design note (thread 02)

Nothing here is built yet. It is the plan the TODO asked for before any code ("weeks; a design note first").

## Why

Every buffer object (BO) is system memory today: `doBoCreate` (`src/vermilion-boot/phx_client.cpp`) backs each one
with an `IOBufferMemoryDescriptor`, whatever domain RADV asks for. The "visible VRAM" (256 MiB), "hi VRAM" (6 GiB)
and GTT (2 GiB) pools are budgets over system memory (`phx_globals.cpp`). Measured on the XTX machine
([perf finding](2026-11-23-perf-power.md) section 3): a buffer copy runs at **44.7 GB/s**, the PCIe 4.0 x16 limit,
in Metal and in RADV alone. Navi 31's GDDR6 peaks at ~960 GB/s, and Linux's number for the same `vkbench bandwidth`
is the target: **823 GB/s** (2026-11-23, [linux-reference](2026-11-23-linux-reference.md)). A memory-bound load (big
textures, post-processing, compute over large buffers, games) can lose up to ~20x. A compute-bound load loses
nothing: the Mandelbrot loop is within 7 % of RADV.

The fixed 6 GiB Metal working set (`recommendedMaxWorkingSetSize`) is tied to this. Raising it while BOs are system
memory would let apps wire 24 GiB of a 48 GB machine, so it moves only together with this work.

## What already exists

| Piece | Where | State on the XTX |
|---|---|---|
| Local-VRAM PTEs (VALID, EXEC, READ, WRITE, no SYSTEM / SNOOPED; address = VRAM offset + `fbPhys`, as amdgpu_vm writes them) | `vmMapVram`, `phx_scan.cpp` | in use: GPU present rebinds a client BO onto a VRAM slot at 256-352 MiB every frame since b126 |
| A BO whose pages are VRAM (`scanSlot`, `scanVram`) mapped through GEM_VA | `phx_client.cpp` (the `b->scanSlot ? vmMapVram(...)` branch) | in use (scan slots) |
| Frees and unmaps deferred until the GPU retired the client's last submission (b52) | `doBoFree`, `reap` | in use |
| Per-client VMID page tables (process isolation) | `phx_vm.cpp`, `PhxVmTables.h` | in use |
| The VRAM size from the memory controller | `phx_core.cpp` (`fbSize`) | 24560 MiB read on the XTX |

So the GPU side of "a BO in VRAM" is proven; what is missing is ownership of VRAM: an allocator, a placement rule, a
clear, and the life cycle.

## VRAM map (offsets from the VRAM base)

| Range | Use | Source |
|---|---|---|
| 0 - 24 MiB | console surface A, flip buffer B | `PhxScanLayout.h` |
| 24 - 48 MiB | small scan slots | same |
| 48 - 96 MiB | PSP ring area (`PHX_VRAM_RING_MIN`) | `PhxNativeABI.h` |
| 96 - 116 MiB | PSP TMR copy area (`PHX_VRAM_TMR_OFF`, up to 20 MiB) | same |
| 128 - 144 MiB | firmware staging (`fw_buf`); VCN ring at 136, the IH ring (when on) at 140 | same, `phx_sysctl.cpp` |
| 144 - 146 MiB | SMU driver / tools tables | same |
| 164 - 240 MiB | page-table pool (`PHX_VRAM_PT_OFF`, `_SIZE`) | same |
| 256 - 352 MiB | large scan slots (1440p GPU present) | `PhxScanLayout.h` |
| top of VRAM | firmware: PSP TMR, **19 MiB at MC 0x85_fc00_0000** (Linux dmesg 2026-11-23), DMUB regions, "firmware above 24560 MiB" | XTX.md, linux-reference; `amdgpu_vram_mm` prints no per-range reserved list, so the DMUB ranges stay to be read from dmesg / debugfs |

The pool for client BOs: **[512 MiB, top - Linux's reserved regions - a margin)**, about 23 GiB. Linux (2026-11-23,
[linux-02b](2026-11-23-linux-02b.md)): VRAM MC 0x80_0000_0000 - 0x85_FEFF_FFFF (24560 MiB), the PSP TMR at offsets
24512-24531 MiB, DMUB in ordinary buffers; on Linux UCLK rises to 1249 MHz within 0.5 s of a VRAM load (837 GB/s). 352-512 MiB stays
free as headroom for the fixed regions.

## Design

1. **Allocator.** A range allocator over the pool: 2 MiB blocks for BOs of 2 MiB and up (Linux's PTE fragment size,
   `pte_fragment_size` 2 MiB in the profile), a 64 KiB sub-allocator inside 2 MiB blocks for small ones. Free ranges
   coalesce. Metadata in kext memory, nothing in VRAM. Pure code with no kernel headers, so
   `tests/kext/` can test it on a host (like `PhxScanLayout.h` and `PhxVmTables.h`).
2. **Placement.** A BO asked for in the VRAM domain with `NO_CPU_ACCESS` and without `CPU_ACCESS_REQUIRED` (Metal's
   private resources, render targets, depth) goes to the pool. Everything the CPU maps stays system memory as
   today: BAR0 is 256 MiB and the fixed regions use it (`ResizeAppleGpuBars = 0`). A full pool falls back to system
   memory (amdgpu falls back to GTT the same way); never a refusal that RADV didn't get before.
3. **Clear before use.** A new VRAM BO may hold another process's pixels. The CPU can't reach VRAM above BAR0, so the
   GPU clears it: a `DMA_DATA` constant fill (PM4, the packet RADV itself uses for fills) on the kext's own queue,
   in a kernel VM context that maps the range, waited before `doBoCreate` returns. Open: whether one kernel context
   with a sliding window is cheaper than mapping into the client's context; measure the clear cost (at ~800 GB/s,
   64 MiB is ~80 us). The BO keeps reporting `PHXN_PLACED_ZEROED` only when the clear completed.
4. **Mapping.** GEM_VA of a VRAM BO takes the existing `vmMapVram` branch with the BO's pool offset. BOMAP (a CPU
   mapping) of a VRAM BO is refused: RADV doesn't map `NO_CPU_ACCESS` BOs.
5. **Free.** Through the existing deferred path; the range returns to the allocator only after the GPU retired the
   client's last submission, and after the unmap's TLB flush.
6. **Budgets.** `vram_hi_total` reports the pool's size and `vram_hi_free` what is left, so RADV's memory budget and
   the plugin's `recommendedMaxWorkingSetSize` follow the real VRAM (the TODO item "working set from VRAM").
7. **Diagnostics.** `debug.phxvm` gains the pool's used / free / largest free range and the clear time; a refusal or a
   fallback is counted like today's `VRAM placed as GTT`.

## Risks and what guards them

| Risk | Guard |
|---|---|
| A range overlapping firmware (PSP TMR, DMUB): a hard GPU hang | the pool's top comes from Linux's measured reservations plus a margin, checked against the MMHUB FB location at start; refuse to enable when unknown |
| Reading another process's data | the GPU clear of every new range; clear-on-free as a second measure if the clear turns out cheap |
| The GPU still using a freed range | the b52 deferral (already guards system pages) |
| Sleep (thread 05): VRAM is lost in S3 | until 05 has an eviction path, VRAM BOs exist only while sleep is off (it is today); 05 owns the eviction |
| WindowServer on a new path at once | opt-in first (below) |

No new SMU, PSP, DAL message or register write: PTEs, PM4 packets on the existing queues, and the TLB flush the kext
already does.

## Steps

1. **M0 (Linux, read-only), done 2026-11-23:** `vkbench bandwidth` 823 GB/s; the PSP TMR 19 MiB at MC 0x85_fc00_0000.
2. **M1:** allocator + placement + GPU clear behind `debug.phxbo_vram` (0 at boot). 1 = only BOs of processes named in
   `debug.phxbo_vram_proc` (e.g. `metalbench`, `vkbench`), so WindowServer never sees it during the first tests. One
   kext build, one boot; the switch is live after that.
3. **M2:** measure with the switch: `bandwidth` (expect hundreds of GB/s), `render`, the clear cost, and a check that
   a fresh BO reads back zero (a GPU copy into a system-memory BO).
4. **M3:** all processes; a daemon flag; days of use; the working set from the pool. Then the default.
5. **M4 (thread 05):** eviction to system memory at sleep, back at wake.

Estimated: M1 a few days of code and host tests, M2-M3 a week of use. Not started in thread 02 beyond this note and
M0: M1 is its own kext build and boot (TODO 02).

## M1 / M2 on the box (2026-11-23 09:00-09:02, VermilionBoot b156, EFI change 16)

XTX machine, 1440p240 HDR, idle desktop (owner away from the keyboard), free idle clocks (`phxboot-idledpm`) and
interrupt waits on. `vramtest.sh` (scratchpad): baseline with the switch off, the self-test, then
`debug.phxbo_vram_proc=vkbench,metalbench` and `debug.phxbo_vram=1`, the switch off again at the end.

| | switch off (system memory) | VRAM BOs | Linux |
|---|---|---|---|
| `vkbench bandwidth 5`, median / best | 42.9 / 43.7 GB/s | **840.9 / 859.4 GB/s** | 837.3 / 851.1 |
| `metalbench bandwidth 5` | 42.9 / 43.7 GB/s | **836.1 / 858.9 GB/s** | n/a |
| UCLK during the run (SMU metrics, 0.5 s) | 96 MHz throughout | **1249 MHz** from the second sample | 1249 MHz |
| GPU socket power during the run | 136 W | 261 W | n/a |
| `vkbench zerocheck` (64 MiB buffers: fill A with a pattern, free it, read a new B) | 4 rounds, 0 of 134 M dwords non-zero | **8 rounds, 0 of 268 M dwords non-zero** | n/a |

- Self-test: 1 (CPU-checked clear inside BAR0; GPU pattern fill and clear at the pool's first and last MiB read back
  through MM_INDEX).
- Pool: [512, 20464) MiB, 19952 MiB free; the GFXHUB system aperture ends at 20464 MiB (XT value, XTX.md row 3), so
  that is the pool's end today. Peak use 565 MiB; 28 allocs, 28 frees, 0 fails, 0 lost, 0 fallbacks, 0 leaked.
- Clears: 28, 2118 MiB, **avg 9.3 ms, max 30.8 ms** (a 256 MiB buffer): ~8 GB/s, the CP DMA's 32 KiB packets.
  Fine for an opt-in; for all processes a faster clear (bigger CP DMA ops are not allowed on GFX11; a compute
  shader or SDMA as Linux) is worth measuring first *(unverified)*.
- No display underflow (OPTC0 / HUBP0 stickies 0 after the run), every submission retired, no map failure.
- RADV's memory type 0 (DEVICE_LOCAL only, the 6 GiB "hi" heap) is the one placed in the pool.

## Benchmark set (2026-11-23 09:45-09:47, after a restart)

Same machine and build, 13 min after boot; `~/src/bench/benchset.sh`, 10 s per test, off then on. Compute and render
gain as well as bandwidth: `vkbench compute` 0.404 -> 0.140 ms (Linux 0.134), `metalbench render` 0.262 -> 0.137 ms,
`metalbench compute` 0.192 -> 0.144 ms; bandwidth 43.7 -> 841.9 (vk) and 44.0 -> 836.9 GB/s (Metal); submit
unchanged (32 us). `zerocheck` PASS in both passes. 48 allocs / 48 frees, 0 leaks, clears avg 6.1 ms, max 29.8 ms.
The table is in PROJECT_KNOWLEDGE ("Benchmark set"). clpeak is out: it hung the GPU in its MPS pass with the switch
**off** ([clpeak hang](2026-11-23-clpeak-hang.md)), so that hang is not this code (one sample).

First real app (10:04-10:06): **Horizon** (the owner's Rust Minecraft client, Metal through wgpu), fullscreen
1440p, render distance 12, uncapped: 93 / 83 fps with the switch off (before / after), **158 fps** with
`debug.phxbo_vram_proc=horizon-client` (1.7-1.9x; 1 % low 61 / 53 -> 118 fps). 395 BOs, peak 675 MiB, no leak, no
fallback; the frame renders correctly. The view drifted (survival, water), sections drawn comparable. Details in
PROJECT_KNOWLEDGE ("Benchmark set").

Not yet measured: a Linux twin for Horizon; days of use (M3).
