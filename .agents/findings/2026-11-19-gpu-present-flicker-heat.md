# 2026-11-19: GPU present at 1440p: where the flicker and the heat come from (measured on the XTX machine)

Measured from macOS on the XTX machine itself (b126, GPU present on, 2560x1440 60 Hz, booted 09:59 Pacific), with
read-only tools only: sysctls, the unified log (WindowServer's plugin lines), `phxctl regs` (MMIO reads, root),
`powermetrics` (root). No register, sysctl, SMU or file change. Raw output stayed in the session scratchpad.

## What was measured

| Question | Source | Result |
|---|---|---|
| Is the plane ever lost or restored to the stale console? | kernel + plugin log | No `scanout restored`, no `plane lost`, no restart since boot |
| b74 latch race | `debug.phxscan_race_stat` after 16 min | `earliest lag 0, accepted flips forgotten 2, on-screen slot reported reusable 0`: the race happens (twice), but a slot on screen was never handed out for reuse |
| Display underflow | `HUBP0_DCHUBP_CNTL` 0x3AB3, `ODM0_OPTC_INPUT_GLOBAL_CONTROL` 0x4F8A, 120 samples over 60 s | HUBP0 underflow status (bits 28-30) 0 in every sample; ODM0 current underflow (bit 13) 0 in every sample; ODM0 sticky (bit 10) set, constant (may date from the GOP / arm; it is never cleared) |
| Refresh | `OTG0_STATUS_FRAME_COUNT` 0x500C | 3656 frames in 61 s: 60 Hz |
| Flip pending | `HUBPREQ0_DCSURF_FLIP_CONTROL` bit 8 | set in 28 of 120 samples |
| Present rate and cost | plugin log (`ws gpu present split`) | ~47 presents/s average over the first 6.5 min, ~31/s in the 60 s window; 0 dropped; copy + fence wait 1.4 ms per present; **37-44 % of present-to-present gaps are under 8 ms** (half a refresh) |
| WindowServer CPU | `powermetrics --samplers tasks` (6 x 10 s) | 129-139 ms/s CPU, **~1300-1400 wakeups/s, nearly all "<2 ms deadline" timer wakeups**; all tasks together 400-455 ms/s (of 24 hardware threads) |
| CPU temperature / package power | - | not available on this install: no AMD SMC/power kext, and `powermetrics` has no AMD package power. Only CPU time and wakeups can be measured |
| GPU clocks | `/var/log/phxboot.log` | the daemon pins UCLK hard-min 456 MHz every boot (also in boot 1): not specific to GPU present |

## Flicker / artifacts: an ordering bug in the present path (code, confirmed by reading; the fix is unverified)

`NVMTLObjects.m` `commitAndWaitUntilSubmitted` (WindowServer's swap commit):

```
if (ws && !async && !nowait && !nvmtl_ws_kernel_waits()) [self waitUntilCompleted]; else [self waitUntilScheduled];
if (ws && !async) nvmtl_ws_gpu_present(self);
```

On this machine `debug.nvaccel_surface_gpu_on` is 1, so `nvmtl_ws_kernel_waits()` is true (the plugin logs `WindowServer
frame completion: the swap waits in NVAccel (no composite wait here)`), and `ws-gpu-present-async` is absent. So the GPU
present runs right after `waitUntilScheduled`. `waitUntilScheduled` returns once `nvmtlMarkScheduled` ran, and
`nvmtlMarkScheduled` is called **before** the first part's `nvmtl_vk_submit_begin` (`nvmtlSubmitAhead`,
`nvmtlAdvancePart`). A command buffer's later parts are submitted after that. The present's copy
(`nvmtl_scanout.c`) goes on the same queue, so the GPU can copy the composite target **before or between the
composite's parts**. The barrier at the start of the copy orders it only after work already submitted.

What that shows on screen: SkyLight alternates between composite targets. A copy that runs too early shows the
target's previous content (two frames old) in the regions being redrawn, or a half-drawn frame. That looks like
flicker (old, new, old) and occasional artifacts. `nvmtl_scanout.c` itself states the contract: "call once the GPU work that drew the frame
has finished". The kernel-marks path (v5, 10-30, "plugin audit") broke that contract for the non-async GPU present.
The async path (`ws-gpu-present-async`) keeps it: it presents from `nvmtlFinishExecution`, after every part has
finished on the GPU.

Ruled out or unlikely, from the table: plane loss or restore (none), the latch race (0 slots reused while on screen),
display underflow (HUBP0 clean, ODM0 current bit never set).

## Heat (CPU side)

- WindowServer wakes ~1300 times per second. These are timer wakeups with short deadlines: the kernel's fence wait
  (`phx_wait.cpp` `doWaitSeq`) spins 20 us, then sleeps in **50 us timed steps** (`debug.phxwait_step_us`), because
  interrupt waits are off (`debug.phxwait_irq 0`, `debug.phxih 0`). Each present waits ~1.4 ms for its copy fence on
  WindowServer's thread, so that alone is ~28 wakeups per present. Frequent short wakeups keep cores out of deep idle
  states. That fits "the CPU runs hotter" with low CPU time (unverified: there is no temperature or package-power reading).
- Presents arrive faster than the refresh (37-44 % of gaps under 8 ms). Each one is a 14 MiB GPU copy and a fence
  wait, even though at most one frame per refresh reaches the panel. Probable cause (unverified): several waited swap
  commits per displayed frame. The kernel's `replaced` count would show it, but no sysctl exposes it.
- CPU time is low (WindowServer ~13 % of one thread). The CPU copy of boot 1 cost 10-12 ms of CPU per frame, more CPU
  work per frame than GPU present. So the heat is more likely wakeups x frame rate than CPU work (unverified; boot 1 had
  no `powermetrics`).

## Live test 1 (10:17:35): `ws-gpu-present-async` on: no change to the flicker

Created `/Library/GPUBundles/nvmtl/ws-gpu-present-async` with the owner's OK. The plugin logged `async ON` within 1 s,
the plane stayed acquired, presents kept flowing (331 in 10 s, no plane loss). Owner: **no change**. The flicker
affects only what they interact with (typing, Dock magnification, moving or resizing windows), never the whole desktop,
and comes in cycles: it flickers for a while, stops for a few seconds, starts again. So the schedule-time copy is a
real ordering bug, but not what the owner sees. The switch stays on: it is correct, and E3 below needs it.

That pattern matches upstream's E3 note in `NVMTLWSPresent.m`: SkyLight composes each frame into one of two targets
and draws only what changed, so the regions being redrawn alternate between new and a frame or more old ("Dock band
ping-pong between the two SkyLight targets", E0 traces 2026-10-25). The default mode presents those targets.
`NVMTL_WS_PRESENT_CDCOPY=1` presents CoreDisplay's display surfaces instead (written by unwaited CoreDisplay commits,
every region current), at GPU completion (async only). The cycles of a few seconds fit a beat between WindowServer's
frame timing and the vblank. A half-drawn target is visible only when a flip latches before the next present
replaces it (unverified).

## Boot test 2 (staged 10:25): E3 for one boot

`<EFI>/vermilion/run-once.sh` runs `launchctl setenv NVMTL_WS_PRESENT_CDCOPY 1` in the boot daemon's pre hook, only
if the async switch exists. WindowServer restarted by the daemon after arming inherits it (the same mechanism as the
daemon's `CI_USE_MTL_DAG_FOR_CIKL_SRC`). `launchctl setenv` does not survive a reboot, so the boot after reverts by
itself. Tested as root with a fake `launchctl` (sets it with the switch, does nothing without). No EFI/OC change.
Expected (unverified): the plugin logs `presenting CoreDisplay's display surfaces` and `surface N is a display
surface`, and the regions being redrawn no longer flicker. If pane switches show incomplete frames, tune the live switch
`ws-cdcopy-hold` (microseconds, upstream's fix for that).

## Next steps (one change at a time)

1. **Flicker:** `/Library/GPUBundles/nvmtl/ws-gpu-present-async` is a live switch (the plugin re-reads it every
   second). With it, the copy runs after the composite has finished, and WindowServer no longer waits for its swap
   commit. Toggle it live with the owner watching; rollback = delete the file (takes effect within 1 s). If it fixes the flicker,
   the lasting fix is to make the non-async path wait for completion before presenting (plugin change, deployed through the
   EFI), or to make async the default for GPU present.
2. **Heat:** after the flicker step, measure again with `powermetrics`. Candidates: present at most once per vblank
   (drop a present whose slot would only replace a pending flip), and waits that do not poll every 50 us
   (`debug.phxwait_step_us` is a live sysctl; the IRQ wait path exists but is off and unverified on the XTX).
