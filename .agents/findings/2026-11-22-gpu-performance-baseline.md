# 2026-11-22: GPU performance, heat and API baseline (measured on the XTX machine)

Measured from macOS on the XTX machine, 19:05-19:15, 30 min after boot. Installed: VermilionBoot b150, the matching
PhxAccel, the Metal plugin of b149. Desktop at 2560x1440, 240 Hz, HDR on (10-bit scan-out), Variable off, GPU present
on with E3, wait step 200 us. Daemon flags: `sdma`, `cg`, `uclk` (456), `p0modes`, `wscdcopy`, `waitstep`, `vrrmin`.
Nothing was changed: sysctl reads, `phxctl smuinfo` / `regs`, `smumetrics.py` (SMU message 0x12, the metrics table
Linux reads), `powermetrics`, and two new user programs (below). Not compared with Linux yet: every "high" or "low"
here is unverified until the Linux capture exists.

## Tools added for this

- `tools/metaltest/metalbench.m` (`scripts/build/metalbench.sh`): an off-screen Metal load. `render` and `compute`
  run a fixed-cost loop per pixel, `submit` measures one tiny waited command buffer.
- `tools/metaltest/apiprobe.m`: what Metal, OpenGL, OpenCL, Core Image and MetalFX report to an app.

## GPU at the idle desktop

`smumetrics.py`, 2-3 s apart, the owner's normal desktop (a few apps, nothing animating):

| | value |
|---|---|
| GFX activity | 1-26 %, mostly 9-10 % |
| gfxclk, average | 52-470 MHz, mostly 180-210 |
| socclk / uclk / fclk | 600 / 456 (the daemon's pin) / 2301 MHz, constant |
| socket / board power | 49-56 W / 62-70 W |
| edge / hotspot / memory | 35-43 / 43-52 / 64-70 C |
| fan | 0 % (stopped), short 9-15 % steps |
| presents / vblanks | 14 /s / 240 /s |

- The metrics table's `CurrClock[GFXCLK]` reads 1200 MHz in every sample, idle and loaded. The average field moves.
  Either the field is not what `smumetrics.py` takes it for on this firmware or it is a target, not a reading
  (unverified).
- Clock gating is on as the daemon set it: `RLC_CGTT_MGCG_OVERRIDE` 0x000600c1, `RLC_CGCG_CGLS_CTRL` 0x0000363f,
  `RLC_RLCS_SDMA_INT_STAT` 0x3 (both SDMA engines idle).
- SMU: version 0x004e8200 (78.130.0), driver interface 0x40, running features low 0x71ffffff, high 0x0003ebb8.
  Which features Linux runs on this card (GFXOFF, the deep-sleep bits, UCLK / FCLK DPM) is the comparison to make.
- 62-70 W board at idle is the same as on 2026-11-19 at 60 Hz SDR (66-67 W), so the 240 Hz HDR mode costs nothing
  extra here. Whether Linux idles lower on this card and monitor: unknown. Candidates if it does: the UCLK pin
  (456 MHz hard minimum, never released), FCLK never leaving 2301 MHz, no GFXOFF entry.
- Memory at 64-70 C with the fan stopped is the warmest part at idle.

## GPU under a Metal load

`metalbench render 75 2000` (2560x1440 off screen, 2000 loop iterations per pixel, 3 command buffers in flight),
SMU sampled every 3 s:

| | idle before | load (75 s) | after |
|---|---|---|---|
| GFX activity | 9-20 % | 100 % | 9-10 % in the first sample after the end |
| gfxclk, average | 180-420 MHz | 2658-2680 MHz | 180-210 MHz |
| socclk | 600 MHz | 750 MHz | 600 MHz |
| socket / board power | 52-56 / 65-70 W | 326-327 / 388-389 W | 50-53 / 63-66 W |
| hotspot | 52-55 C | 82 C after 3 s, peak 88 C, settling at 85 C | 55 C after 3 s, 44 C after 40 s |
| edge / memory | 43-45 / 70 C | 54 / 74 C | 35 / 64 C |
| fan | 10-15 % | ramps to 49 % in 30 s, stays | ramps down, stopped 40 s after the end |

- **The GPU clocks up and down by itself.** The firmware's own power and fan control works: full clock under
  load, back to the idle power within one 3 s sample, the fan follows its curve and stops.
- 192.0 frames/s, GPU time 4.82 ms per frame (median 4.84, 99th percentile 4.93), wall time per frame median 4.92 ms,
  99th percentile 7.36, longest 9.68. No GPU errors.
- Short runs: `render` at 64 iterations 3446 frames/s (GPU 0.26 ms), `compute` at 64 iterations 2726 frames/s
  (GPU 0.20 ms, wall average 0.37 ms).
- `submit` (one 16x16 draw per command buffer, each waited): 9887 /s, 101 us per round trip on average (median
  89 us, 99th percentile 566 us), of which the GPU works 6 us. That is the stack's cost per submit and fence wait:
  plugin, RADV, the kext and the polled wait (20 us spin, then 200 us timed steps; interrupt waits are off:
  `debug.phxih 0`, `debug.phxwait_irq 0`).
- No Linux number for the same load yet. The same shader through Vulkan on Linux would say how much the stack costs.

## CPU side

- `powermetrics --samplers tasks` (2 x 10 s, idle desktop): WindowServer 123-127 ms/s CPU, 278-280 wakeups/s, 261-263
  of them "<2 ms deadline". All tasks 451-458 ms/s, 1756-1776 wakeups/s. On 2026-11-19 at 50 us WindowServer woke
  640-2500 times/s.
- `powermetrics --samplers cpu_power,thermal,smc,gpu_power`: no package power, no temperature, no fan, nothing for
  the GPU ("SMC version: Unknown"). C-state residency prints 0 % for every core: those are Intel counters, so this
  says nothing about whether the Ryzen idles. There is still no CPU temperature or power reading on this install.

## What each graphics API does

| API | Result |
|---|---|
| Metal | Works. One device, "AMD Radeon RX 7900 XT (RADV NAVI31) (NVMTL over ...)": the XT's name on the XTX. Families Common 1-3, Mac 2, Metal 3; not Metal 4. Ray tracing, function pointers, dynamic libraries, BC textures, barycentrics reported. Argument buffers tier 1, read-write textures tier 2. MSAA 2 / 4 / 8. No raster order groups, no programmable sample positions, no pull-model interpolation. Counter sets: timestamp only |
| Metal memory | `recommendedMaxWorkingSetSize` 6144 MiB, `maxBufferLength` 4095 MiB on a 24 GiB card. RADV prints "VRAM 256 MiB visible + 6144 MiB hi, GTT cap 2048 MiB". These are the kext's fixed budgets (`phx_globals.cpp`); its comment says every buffer object is backed by system memory, not VRAM |
| MetalFX | spatial and temporal scalers accept the device (not run) |
| Core Image | default context on the Metal device |
| OpenGL | **Apple Software Renderer** only (4.1 core and legacy). A request for an accelerated pixel format fails. Every OpenGL app draws on the CPU |
| OpenCL | the CPU device only, no GPU |
| Vulkan | no loader and no MoltenVK installed system-wide. Apps that bundle MoltenVK go Vulkan -> Metal -> the plugin -> RADV (not tested). `libvulkan_radeon.dylib` exports the ICD entry points, but nothing registers it with a loader |
| Video decode | VCN is off (`debug.phxvcn 0`; `phxboot-vcn` not set) |
| Shader disk cache | every user process prints `Failed to create /var/folders/zz/.../C/nvmtl-mesa for shader cache (Permission denied)---disabling.` twice: Mesa's disk cache points at root's cache folder, so apps other than root's have none and recompile every pipeline at each launch (the plugin's own `aircache` / `spvcache` are separate; whether they cover it is unverified) |

Not tested yet: any real app or game, WebGL / WebGPU in Safari and in Chromium (ANGLE on Metal), on-screen frame pacing
of an app's own window, MoltenVK.

## Also seen

- The kext's call counters since boot: 481195 submits, 452003 fence waits (`debug.phxwait_lat`: 131964 finished in the
  20 us spin), 25976 presents, no map failures, no TLB or unmap timeouts.
- `log show` returned none of the plugin's `ws gpu present split` lines in this session (not looked into); the
  present rate above comes from the kext's counters.

## Open questions this leaves

1. Idle: is 63 W / memory 64-70 C what Linux has at 1440p 240 Hz on this card? (Linux capture)
2. Load: how far is 192 frames/s from Linux for the same shader? How much do system-memory buffers cost a
   texture-heavy load? (`metalbench` has no memory-bound mode yet)
3. Frame pacing on screen: an app window at 240 Hz and with Variable on, through the copy per present.
