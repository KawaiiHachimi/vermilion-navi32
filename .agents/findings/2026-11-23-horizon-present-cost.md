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

## GPU time per process (b157, 10:33-10:34, same Horizon setup, VRAM BOs on)

`debug.phxgpu_time` over Horizon's 15 s bench (`~/src/bench/hztime.sh`; the time a process held the gfx queue,
waits inside its IBs included):

| Run | Horizon | WindowServer | other | GPU total | fps |
|---|---|---|---|---|---|
| fullscreen 2560x1440 (2459 frames) | 8.54 s = **3.47 ms per frame** (20502 submissions) | 5.40 s = **2.20 ms per frame** (7383) | - | 92.8 % | 163.9 |
| 1280x720 window (4819 frames) | 5.57 s = 1.16 ms per frame (41880) | 5.86 s = 1.22 ms per frame (14280) | T3 Code 1.2 % | 77.3 % | 321.1 |
| idle desktop, 10 s, nothing running | - | 5.4 % | T3 Code 1.8 % | 7.3 % | - |

Reading:
- The gfx queue is full (93 %), and **WindowServer takes 36 % of it**: ~2.2 ms per Horizon frame to composite the
  2560x1440 frame from system memory into its VRAM display surface. At ~7 GB/s for a 14.7 MB frame, that is a slow
  copy: the GPU samples the system-memory IOSurface over PCIe.
- Horizon's own 3.47 ms per 1440p frame barely changed with a quarter of the pixels (resolutionScale 50: 190 fps), so
  much of it is size-of-drawable work too: writing the final image into the system-memory IOSurface (unverified
  split).
- Without the composite alone, Horizon would reach about 1 / (3.47 + 0.4) ms = ~260 fps (estimate). Linux's >3x
  (~480+ fps, owner's report) needs both halves: the frame in VRAM (so the final pass writes VRAM) and no composite,
  or a VRAM-to-VRAM composite.

Next (design, not started): put the app drawables (CAMetalLayer IOSurfaces) in VRAM, so that Horizon writes and
WindowServer reads VRAM; or let a fullscreen opaque app's frame go to the display directly, as a real Mac does.
Both touch the accel kext and the plugin. The cheaper first step to measure: how the plugin's WindowServer present
path reads the app's IOSurface, and whether a DMA copy of it into VRAM beats the shader sampling it.

## One frame, submission by submission (b158 trace, 10:59 VRAM BOs off / 11:00 on)

`debug.phxgpu_trace` over ~0.6 s of Horizon fullscreen 1440p (`~/src/bench/hztrace.sh`; raw:
`~/src/bench/results/hztrace-20261123-1059*`, `-1100*`). Every frame repeats the same pattern on the gfx queue:

| Submission (IB dwords) | Who | VRAM BOs off | VRAM BOs on | Reading |
|---|---|---|---|---|
| 6 small ones (128-720 dw) | Horizon | 10-70 us each; one (224 dw) 332 us | 10-60 us each; the 224-dw one 41 us | setup, uploads, culling |
| **main pass (1904 dw)** | Horizon | **5386 us** | **2345 us** | the world: VRAM BOs halve it, still >10x Linux's whole frame (0.22 ms) |
| 488-504 dw | Horizon | 750 us | 747 us | unchanged by VRAM BOs: the pass into the system-memory drawable (14.7 MB in 0.75 ms = ~20 GB/s, a PCIe write rate) (unverified) |
| 136 dw, 472 dw, 136 dw | WindowServer | 808 + 812 + 520 us | same | WindowServer's three composite submissions per Horizon frame, ~2.1 ms (it reads and writes system-memory surfaces; not opted in) |

What is still slow, and why (candidates, in order of size):
1. **The main pass, 2.3 ms with VRAM BOs on.** Its private buffers and textures are now in VRAM; what is left in
   system memory is every CPU-visible buffer: wgpu asks for MAPPABLE_PRIMARY_BUFFERS, and Horizon's section vertex
   arenas (240 MB) and uniforms are Shared, i.e. system memory read over PCIe while drawing. On Linux, Resizable
   BAR makes all 24 GiB CPU-visible and RADV puts such buffers in VRAM; on macOS the CPU-visible VRAM is the 256 MiB
   BAR, already used (`NVMTL_SHARED_VRAM=1` found a budget of 0). (unverified: which buffers the pass reads.)
2. **The present: Horizon's 0.75 ms pass into the drawable plus WindowServer's ~2.1 ms composite**, both on
   system-memory IOSurfaces. Real Mac drivers keep a VRAM-resident copy of GPU-used IOSurfaces.
3. Small: six submissions of setup per frame, ~0.2 ms.

These are not Horizon-specific: (1) hits every app that streams data through CPU-visible buffers (wgpu, MoltenVK and
most games), (2) every app that presents frames, at a cost that grows with resolution.

## b159: CPU-visible buffers in VRAM (2026-11-23 15:52, XTX, EFI change 20)

Horizon fullscreen 2560x1440, same world and view (`drawn_sections` 392-394), VRAM BOs on for `horizon-client`,
15 s bench each (`~/src/bench/results/hzdiag-20261123-155251`, `-155317`):

| run | avg fps | p50 ms | p99 ms | 1 % low fps | GPU (SMU) |
|---|---|---|---|---|---|
| b159, Shared in system memory (default) | 168.1 | 5.86 | 10.44 | 89.7 | gfx 94 %, 2.96 GHz, board 225 W |
| b159, `NVMTL_SHARED_VRAM=1` (Shared buffers in BAR0 VRAM) | 167.6 | 5.93 | 6.66 | 136.9 | gfx 95 %, 2.96 GHz, board 199 W |

The CPU-visible pool was used (311 BOs, peak 207 MiB) and nothing fell back. The average does not move, but the
stutter does: p99 10.4 -> 6.7 ms, 1 % low 90 -> 137 fps, and 26 W less board power. The GPU is busy 94-95 % at full
clock in both runs, so the GPU itself is the limit: it does far more work per frame than on Linux (0.22 ms p50 for
the whole frame there; main pass alone 2.35 ms here, b158 trace). Where the main pass spends that time (shader code
from the AIR -> SPIR-V translator, descriptor / buffer access patterns, render target formats) is the next question.
The `win720` run in that directory ran fullscreen too (`options.txt` has `fullscreen:true`): ignore it.

## Where the main pass goes: the frame is drawn into linear system memory (2026-11-23 16:22-16:29, XTX, b159)

Shader statistics (`NVMTL_PSTAT=1` plus the plugin trace; raw: `~/src/bench/results/hzdiag-20261123-162249`) and the
plugin's per-pass GPU timer (`/tmp/nvmtl-passtime-request`; `hzdiag-20261123-162830`, `-162856`, `-162922`):

- The terrain fragment shaders are large (`fs_opaque` 3469 ACO instructions, `fs_solid` 3537, no spills, 32 waves
  per SIMD). That is static size: Horizon's WGSL (`sprite_grad` / `sprite_level`) switches over 8 sprite arrays
  (`SPRITE_ARRAYS` > 1, `SPRITE_SPLIT` folded to 512 on macOS), and a face takes one case. Not the cause.
- The main pass is 2 indexed terrain draws (531k and 540k indices) plus sky, entities, water and clouds.

| run (median of 134 frames) | main pass | vignette pass | fps |
|---|---|---|---|
| normal | 2.42 ms | 0.74 ms | 167 |
| `NVMTL_SKIP_FS=fs_opaque,fs_solid` (the terrain draws skipped) | 1.59 ms | 0.77 ms | 183 |
| `RADV_DEBUG=nohiz` | 2.50 ms | 0.73 ms | 166 |

The terrain is only 0.83 ms; the rest of the pass (sky, entities, water, clouds) costs 1.6 ms, and a 74-instruction
full-screen vignette pass 0.74 ms. Hi-Z barely matters. The trace's `PASS` lines show why: **every pass renders
straight into the CAMetalLayer drawable** (IOSurfaces 73/84/85, rotating), and the plugin draws an IOSurface as a
LINEAR image on its own system-memory pages (host import). So the clear, the sky, every terrain layer, the blending
and the vignette are linear writes across PCIe. On Linux the swapchain image is VRAM, tiled and compressed. This hits
every app that draws into its drawable (most Metal games, wgpu, MoltenVK).

Fix (plugin, `NVMTL_DRAWABLE_TILED`, on by default outside WindowServer): an IOSurface named `CAMetalLayer...` at
least 256x256 is drawn into a tiled image, and copied to its pages once, in the command buffer that presents it. This
is upstream's `NVMTL_WS_SCANOUT_TILED` for WindowServer's display surface (probe there: 1.77 ms linear, 88 us tiled,
491 us with the copy), applied to app drawables. Expected (unverified): main pass and vignette near their VRAM/tiled
cost, plus one 14.7 MB copy (~0.6 ms at PCIe write speed). Off switch without a restart: the file
`/Library/GPUBundles/nvmtl/drawable-tiled-off` (read for each new drawable). Next after it: WindowServer's
composite, ~2.1 ms per frame (`NVMTL_WS_SCANOUT_TILED` is off on this machine; to test in a later boot).

## The tiled drawable, measured (2026-11-23 16:43, XTX, EFI change 21: b160 + NVMTLDriver with `NVMTL_DRAWABLE_TILED`)

Same setup, VRAM BOs on for `horizon-client`, `horizon-ctl bench 15`, pass timer on (`hzdiag-20261123-164308` tiled,
`-164335` with `NVMTL_DRAWABLE_TILED=0`). The plugin logged all three drawables (surfaces 73, 64, 81) as "drawn TILED".

| | avg fps | p50 | p99 | main pass | vignette pass | UI pass |
|---|---|---|---|---|---|---|
| tiled drawable (default) | **292.0** | 3.41 ms | 4.13 ms | **0.190 ms** | 0.024 ms | 0.010 ms |
| drawable on its pages | 171.7 | 5.82 ms | 6.70 ms | 2.375 ms | 0.726 ms | 0.054 ms |

The screenshots of both runs match (only a particle differs). The main pass is 12.5x faster and now close to what the
Linux frame suggests. What is left of the ~3.4 ms frame: the once-per-frame copy of the 14.7 MB drawable to its
system-memory pages (outside the passes; not timed yet, ~0.6 ms expected) and WindowServer's composite of it (~2.1 ms
at b158), both the present path. Next: WindowServer's display surface tiled (`NVMTL_WS_SCANOUT_TILED`, a boot-daemon
environment change), then VRAM-resident IOSurfaces so the copy and the system-memory read go away.

## C3: WindowServer's display surface tiled, as upstream wrote it: worse (2026-11-23 17:11, XTX)

Boot daemon flag `phxboot-wstiled` -> `NVMTL_WS_SCANOUT_TILED=1` for the restarted WindowServer. It took effect (all
2560x1440 "CoreDisplay Scanout" surfaces drawn TILED) and the desktop looked right (owner). Horizon, per-process GPU
time (b157, `~/src/bench/results/hztime-20261123-171048`), 15 s bench:

| | fps | WindowServer GPU | Horizon GPU |
|---|---|---|---|
| fullscreen 1440p, wstiled | 230.1 (p50 4.33 ms) | 10.43 s = 69.4 % of wall, 10362 subs (~3.0 ms per frame, 3 subs) | 3.16 s = 21.0 % (~0.92 ms per frame) |
| 1280x720 window, wstiled | 317.0 | 72.2 % of wall | 12.9 % |
| fullscreen, linear WindowServer (16:43 run / 10:33 timing) | 292 | ~2.2 ms per frame (10:33, b157) | - |

Upstream's mode copies the whole tiled surface to its pages at the end of every command buffer that drew it, and
WindowServer submits ~3 per Horizon frame: three 14.7 MB writes across PCIe per frame instead of none. Switched off
again. The redo: one copy per frame at the swap commit (as the app drawables do), or GPU present copying the tiled
image straight into its VRAM slot (no page write at all while GPU present runs; the pages only for other readers).
Horizon's own GPU time is now ~0.9 ms per frame (its passes ~0.22 ms plus the drawable copy): WindowServer's composite
(~2.2 ms) is the largest part of the frame.

## Minecraft Java (Fabulously Optimized, Vulkan through MoltenVK) on the XTX, 2026-11-23 17:14-17:20

- The owner's instance (Prism, Fabulously Optimized 15.0.0-beta.4 for 26.3, `preferredGraphicsBackend:"vulkan"`) runs
  Vulkan through MoltenVK 1.4.2 on our Metal plugin: "NVMTL over AMD Radeon RX 7900 XTX (RADV NAVI31)".
- **Its window stayed black with the tiled drawable** (`NVMTL_DRAWABLE_TILED`, C2): the drawables (854x480) were made
  TILED and no copy error was logged, yet nothing reached the window. With `/Library/GPUBundles/nvmtl/drawable-tiled-off`
  it draws. Cause not found yet (MoltenVK presents differently from wgpu); the off switch stays until it is fixed.
- 854x480 window, `java` not in `debug.phxbo_vram_proc` (all its BOs in system memory): GPU 99 % busy at 3.1 GHz,
  java 65.8 % of it (4.84 s in 7.35 s, 1363 submissions, 3.55 ms each), WindowServer 28.3 %.
- Same with `java` added to `debug.phxbo_vram_proc` (and to `~/gpu/phxboot-bovram`): java 8.1 % (0.60 s, 888
  submissions, 0.68 ms each: 5.2x less per submission), WindowServer 26.0 %, GPU 36 % busy. The GPU stops being
  the limit; 825 MiB in the VRAM pool, no leak, 32 fallbacks.
- Systemic causes this shows: VRAM BOs are still opt-in per process name (M3 would make them default); Minecraft's
  Vulkan goes through two translation layers (MoltenVK to Metal, our plugin back to Vulkan) where Linux runs RADV
  directly (a native Vulkan ICD for macOS apps would remove both); WindowServer's composite costs ~0.9 ms per
  submission even for a small window.
