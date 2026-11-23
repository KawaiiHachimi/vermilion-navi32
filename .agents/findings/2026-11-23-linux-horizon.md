# Horizon on Linux, the twin of the macOS run: ~25x, not 3x (2026-11-23, XTX machine, Omarchy)

The Linux half of [the macOS present-cost run](2026-11-23-horizon-present-cost.md), done as
`HANDOFF-LINUX-HORIZON.md` (on the ESP) asks: the same Horizon build (`nightly-2026-11-13-9f42064`),
world `Bench` from seed 1, `tp 130 70 170`, `look 90 0`, the same `options.txt` / `horizon-graphics.txt` (render
distance 12, `maxFps` 260 = unlimited, vsync off, `resolutionScale` 100, `presentMode` auto, HDR off) and the same
`horizon-ctl bench 20`. GPU busy, board power and clocks from amdgpu sysfs at 1 s (median of the samples inside the
20 s bench).

Context: 10:45-10:48 CET, booted at 10:38 (cold boot from macOS); kernel 7.2.15-1-omarchy; Hyprland, one monitor
DP-3 2560x1440 at 240 Hz, VRR off, no HDR on the desktop; Mesa / RADV 26.2.5 (`radv Mesa 26.2.5-arch1.1`).
Horizon reports `AMD Radeon RX 7900 XTX (RADV NAVI31) (Vulkan, DiscreteGpu)`, present mode Immediate. Running at the
same time: T3 Code, a messaging app (light). GPU idle beforehand: 1 % busy, 38 W.

| Run | drawable | fps (avg) | p50 / p95 / p99 frame | 1 % low | Horizon CPU per frame | sections drawn (before -> after) | GPU busy | board power | sclk |
|---|---|---|---|---|---|---|---|---|---|
| warm-up (world created) | 2560x1440 | 3729 | 0.23 / 0.43 / 0.49 ms | 1731 | 0.16 ms | 375 -> 375 | 80 % | 197 W | 2690 MHz |
| full-1 | 2560x1440 | **4370** | **0.22** / 0.29 / 0.37 ms | 1982 | 0.15 ms | 375 -> 382 | 80 % | 218 W | 2690 MHz |
| full-2 (player drifted, see below) | 2560x1440 | 4902 | 0.18 / 0.31 / 0.37 ms | 1994 | 0.15 ms | 422 -> 3 | 80 % | 135 W | 1950 MHz |
| full-3 (window was tiled, see below) | 2536x686 | 4191 | 0.23 / 0.30 / 0.41 ms | 1872 | 0.17 ms | 520 -> 416 | 92 % | 326 W | 2710 MHz |
| "1280x720 window" (tiled, see below) | 1261x1386 | 4382 | 0.22 / 0.30 / 0.38 ms | 1945 | 0.17 ms | 289 -> 416 | 80 % | 166 W | 2330 MHz |
| **macOS** fullscreen, same build and view | 2560x1440 | 152-166 | 6.0-6.3 ms | | ~1.0 ms | 394 | 93-95 % | ~195 W | ~2950 MHz |
| **macOS** 1280x720 window | 1280x720 | 328 | 2.6 ms | | | | 82 % | ~176 W | |

Caveats:
- full-2: the player moved during the run (survival, water): only 3 sections drawn at the end, so its average and
  its power are not the same view. full-1 and the warm-up are the clean fullscreen numbers.
- full-3 and the window run: Hyprland tiled the game beside the T3 Code window. In full-3 the fullscreen request was
  not honoured (a 2536x686 drawable), and `--width 1280 --height 720` gives a tiled 1261x1386 window, not
  1280x720. Neither changes the result: frame rate on Linux does not depend on the drawable size here.
- The game's log for the last run says `HDR output HDR10 (PQ)` and `configuring the surface (... Mailbox)` although
  `horizon-graphics.txt` has `hdr:false`; the renderer status reports `present_mode: Immediate` for every run.
  Not looked into.

Reading:
- **The gap is ~25x, not 3x**: 4370 fps (p50 0.22 ms) on Linux against ~160 fps (p50 6.0-6.3 ms) on macOS, for the
  same drawable, view, settings and build. The owner's "more than 3x" was a floor, not the size of the gap.
- Horizon's work per frame is tiny on Linux too: ~0.15 ms of CPU and ~0.2 ms per frame overall. The ~6 ms per frame
  on macOS is therefore almost entirely the fixed cost measured there; even the macOS 720p window (2.6 ms) is more
  than 10x a Linux 1440p frame.
- On Linux the frame rate does not change with the drawable size (2560x1440, 2536x686 and 1261x1386 all give
  ~4200-4900 fps, within the run-to-run spread), so nothing on Linux scales with presented pixels the way the
  ~4-5 ms on macOS does. This supports the macOS finding that the cost is in presenting (an IOSurface in system
  memory, a copy over PCIe, the WindowServer composite and its synchronisation), not in rendering.
- Power does not tell the two apart: ~200 W at 80 % busy here at ~4400 fps, against ~195 W at ~94 % on macOS at
  ~160 fps. On macOS the GPU is "busy" for 25x fewer frames, i.e. mostly waiting, as the macOS note concluded.

Owner's usual Linux settings (owner confirmed the saved config, `~/.local/share/horizon/`): fullscreen, render
distance 24, vsync on, `maxFps` 60, HDR on, `framePacing` precise, played through the launcher's
`native-fdc87ae` build. With vsync and a 60 fps cap, the frame rate the owner sees on Linux is capped there, so the
"3x" from the owner's play sessions was never a measurement of the uncapped gap. No fps figure given.

Run notes: `horizon-ctl` needs the launcher's "No AI" setting off (owner turned it off for this run). The
`nightly-2026-11-13` build had never been played on Linux and had no prepared resources; it was prepared with its
own `horizon-prepare --prepare` from the launcher's server jar, Minecraft copy and Java 25 runtime (no sounds), and
the client has to be started from the version folder, since it looks for `./.local/horizon-client/26.3`.
Raw output: `~/hzbench/out/` on Linux, copied without screenshots to the ESP as `/boot/vermilion/results-02c/`.

Next: the macOS next steps in [the present-cost note](2026-11-23-horizon-present-cost.md) stand, with a bigger
target: matching Linux means bringing a 1440p present from ~6 ms down to well under 1 ms. Put app drawables in VRAM
and skip the composite for a fullscreen app.
