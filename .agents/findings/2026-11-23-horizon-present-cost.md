# Horizon at 1440p: a fixed cost per presented frame, not rendering (2026-11-23, XTX machine, b156)

The owner gets more than 3x Horizon's frame rate on Linux with the same setup (owner's report; not measured here).
macOS with VRAM BOs on: ~160 fps fullscreen 1440p. Where the time goes, from `~/src/bench/hzdiag.sh` runs at
10:11-10:16 (Horizon nightly 2026-11-13, Metal through wgpu, VRAM BOs on for `horizon-client`, Immediate present,
`maxFps` 260 = unlimited; GPU activity and power from the SMU metrics table at 1 s, CPU from `top`).

| Run | fps (avg) | p50 frame | GPU activity | board power | Horizon CPU per frame |
|---|---|---|---|---|---|
| fullscreen 2560x1440, render distance 12 | 152-166 | 6.0-6.3 ms | 93-95 % at ~2950 MHz | ~195 W | ~1.0 ms |
| same, `resolutionScale` 50 (1/4 of the pixels rendered, still a 2560x1440 drawable) | 190 | 5.2 ms | 92 % | ~189 W | |
| same, render distance 4 (69 sections drawn instead of 394) | 172 | 5.8 ms | 95 % | ~197 W | |
| **1280x720 window** (a 1280x720 drawable) | **328** | **2.6 ms** | 82 % | ~176 W | |
| `NVMTL_SHARED_VRAM=1` (Shared buffers in CPU-visible VRAM) | 166 | 6.0 ms | 95 % | ~195 W | no change: that heap is the 256 MiB BAR, budget 0 |

Reading:
- Horizon's own work is small: its CPU time is ~1 ms per frame, and a quarter of the pixels or a sixth of the
  geometry gains only 3-15 %.
- What costs ~4-5 ms per frame scales with the **size of the presented drawable**: the 1280x720 window doubles the
  frame rate, while the same pixel count rendered into a full-size drawable does not.
- The GPU reads "busy" at ~94 % but draws ~195 W at ~2.95 GHz, far below a real load on this card (389 W at
  `metalbench render 2000`, 2026-11-22): it spends that time waiting, most likely on memory over PCIe.
- The drawable is an IOSurface in system memory (the kext keeps scanout and CPU-mappable BOs out of VRAM; the plugin
  imports IOSurface pages as host memory). Each frame, Horizon writes its 2560x1440 image over PCIe and WindowServer
  reads it back over PCIe to composite it into its VRAM display surface, on the same gfx ring. A real Mac's driver
  keeps these surfaces in VRAM, and fullscreen games can skip the composite. The ~4-5 ms is more than the raw
  transfers alone would take (~1 ms at the measured 44 GB/s), so synchronisation between the app's and WindowServer's
  presents probably adds to it (unverified).
- Linux has all 24 GiB of VRAM CPU-visible (Resizable BAR, `vram_vis_size` 24560 MB); macOS sees BAR0 at 256 MiB
  (`ResizeAppleGpuBars = 0`, decisions 2026-11-18).

Next steps, in order:
1. Measure per-context GPU time (a timestamp at each submission's start and RELEASE_MEM in the kext), to split
   Horizon's frame from WindowServer's composite and from waits.
2. Put app drawables (IOSurfaces from CAMetalLayer) in VRAM. This needs the accel kext and the plugin, and
   WindowServer must read them from VRAM.
3. A direct path for a fullscreen app's frame to the display (no composite), as on a real Mac.
4. Resizable BAR in macOS (an EFI change plus kext work: BAR0 is the kext's VRAM window, validated at 256 MiB). That
   also lets Shared buffers live in VRAM, as RADV does on Linux.
A Linux twin of this Horizon run (same world, `horizon-ctl bench`) should come first, to put a number on "3x".
