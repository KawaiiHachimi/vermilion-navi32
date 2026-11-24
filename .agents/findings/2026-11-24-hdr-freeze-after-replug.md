# Frozen picture on the full desktop after a DP / HDMI replug (thread 04, 2026-11-24)

On the XTX machine (EFI change 23, VermilionBoot b160, plugin `344c621f`), 00:05-01:05 CET.

## What happened

1. ~00:05 the owner tested HDMI: DP unplugged, HDMI plugged into the LG, input switched. HDMI stayed black (expected:
   no plan for the LG on HDMI). Back on DP the LG stayed black although the link trained (HBR3 x4, lanes `0x77`,
   aligned, sink count 1); two mode switches (`CGConfigureDisplayWithDisplayMode`, 240 -> 144 -> 240 -> 144) did not
   bring the picture back; a restart did.
2. 00:08:50 WindowServer reconfigured the display (`MirrorDisplays` helper, `IOFBSetDisplayModeAndDepth mode 0x10`).
   From then on, and in every full-desktop boot after it (00:14, 00:23, 00:29, 00:33, 00:54), WindowServer switched to
   **BGRA** display surfaces some seconds into the session while the scan-out was 10-bit HDR (`ARGB2101010`); the
   plugin then logs `BGRA surface N widened to 10 bits on the CPU`. That message never appears in the 8 hours before
   00:08.
3. Each of those sessions ended with what the owner saw as "can't move the mouse, flicker at the text cursor"; the
   owner powered off or restarted. The safe desktop (no GPU present) was fine.

## Evidence that it is a frozen picture, not lost input

`watch.sh` (00:54 boot, `~/vermilion-captures/thread04-input-20261124/watch-005458.log`): while the screen was frozen
the cursor position in WindowServer moved with the mouse every 0.5 s and the HID idle time stayed at 0. HUBP0 kept
flipping between its three VRAM slots (`0x10000000`, `0x12000000`, `0x14000000`), OTG0 at 240 Hz, no flip or GPU
timeouts. So the slots were presented with stale content: the CPU widen path (BGRA -> 10 bit) stops refreshing them
(unverified which part; plugin code not read yet).

Earlier "no input" readings (`HIDIdleTime` growing) were taken while the owner typed on the phone. The USB audio
`mismatched event TRB` / `aborting 8 requests` errors happen all night, also before 00:08: unrelated.

## Workaround in place (01:00)

`~/gpu/phxboot-nohdr`: full desktop at 240 Hz with Variable refresh, HDR off (scan-out `ARGB8888`). No freeze in the
first 5 minutes (owner, 01:05).

## Open

- Why WindowServer composites in BGRA while HDR is offered since 00:08 (`PreferHDRModes` for the display is still
  true; `system_profiler` reports a 24-bit framebuffer). Candidates: the mode set at 00:08:50, a CoreDisplay per-user
  HDR state.
- The plugin's CPU widen path: make it refresh every frame or refuse it (HDR off / 8-bit scan-out) instead of
  presenting stale slots.
- After a DP replug the LG stays black until a restart.
