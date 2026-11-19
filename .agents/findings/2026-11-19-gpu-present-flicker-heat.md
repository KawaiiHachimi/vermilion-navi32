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

## Boot test 2 result (2026-11-19, 10:28 Pacific boot): E3 fixes the flicker (verified on the XTX machine)

- `phxboot.log`: `run-once: NVMTL_WS_PRESENT_CDCOPY=1 for this boot (async switch present)`. WindowServer (the one the
  daemon restarted) logged `presenting CoreDisplay's display surfaces, not SkyLight's composite targets` and six
  `surface N is a display surface (written by an unwaited CoreDisplay commit)` lines, then `ON - 2560x1440`.
- Owner: **the flicker is gone** ("works so good now").
- First 3000 presents: 0 dropped, copy + present **729 us** average (default mode: 1.4 ms), present gaps `<8ms 8, <16
  397, <33 2473, <100 81, longer 40`: under 8 ms went from 37-44 % to 0.3 %, so the duplicate presents are gone and
  most frames come one or two refreshes apart. WindowServer CPU 12-13.5 % of one thread (`top`), about as before;
  wakeups not measured yet. Race counters: `accepted flips forgotten 1, on-screen slot reported reusable 0`.
- The variable lasts one boot: the next boot without the run-once is back to the default mode and the flicker.

## Heat, measured again with E3 (2026-11-19 22:03, restart 1 of the permanent flag, uptime ~3 min)

Same `powermetrics --samplers tasks,interrupts` (6 x 10 s), owner using the desktop:

| | default mode (10:11) | E3 (22:03) |
|---|---|---|
| WindowServer CPU | 129-139 ms/s | 106-163 ms/s |
| WindowServer wakeups | 1300-1400 /s | **700-1050 /s** |
| GPU presents | ~31 /s | ~16 /s (1007 in 62 s) |
| all tasks CPU | 400-455 ms/s | 370-720 ms/s (the boot daemon's watch and late steps still ran) |

- WindowServer wakes ~45 % less, about in step with the presents (~42-45 wakeups per present in both runs): the fence
  waits' 50 us timed steps still dominate. CPU time is about the same.
- Not a clean comparison: less desktop activity, and 3 minutes after boot the daemon was still busy (kernel_task
  600-1165 wakeups/s against ~260 this morning).
- Race counters 0/0/0. Next heat step (live, a sysctl, owner's OK): `debug.phxwait_step_us` 50 -> 200 and measure
  again; costs at most ~150 us more latency per fence wait.

## Live test 3 (2026-11-19 22:07): `debug.phxwait_step_us` 50 -> 200: fewer wakeups, owner unsure, reverted

Restart 1 of the permanent flag (E3 on, boot daemon done 22:03). Owner using the desktop normally. Measured with a
read-only script: snapshots of `debug.phxcalls`, `debug.phxfb_vbl_n`, `debug.phxscan_race_stat`, `debug.phxwait_lat`
around `powermetrics --samplers tasks,interrupts --show-process-energy -i 10000 -n 6` (root). Then `sysctl -w
debug.phxwait_step_us=200` at 22:07:41 and the same measurement. Set back to 50 at 22:09:58.

| 60 s window | step 50 us (22:06) | step 200 us (22:07) |
|---|---|---|
| GPU presents (sel 12) | 15.4 /s | 19.8 /s |
| vblanks | 60.1 /s | 60.1 /s |
| WAITSEQ calls, all clients (sel 64) | 124 /s, 107 of them slept | 145 /s, 135 slept |
| average WAITSEQ time (sel 66) | 479 us | 721 us |
| WindowServer wakeups (6 x 10 s) | 173-950 /s, mostly 640-950 | **254-737 /s, mostly ~255** |
| WindowServer wakeups per present | ~51 | ~18 |
| WindowServer CPU | 32-159 ms/s | 112-253 ms/s |
| all tasks CPU | 390-1063 ms/s | 320-911 ms/s |
| race counters | 0 / 0 / 0 | 0 / 0 / 0 |

- The model holds: a wait that sleeps takes (480 - 20 spin) / ~60 us = ~7-8 timed steps at 50 us. That times ~107
  sleeping waits/s gives ~800 wakeups/s, close to WindowServer's "<2 ms deadline" count. At 200 us each step costs
  ~240 us more per wait (480 -> 720 us), as expected.
- WindowServer wakes ~65 % less at 200 us. CPU time didn't drop: waits are sleeps, not CPU work.
- `debug.phxwait_lat` doesn't count timed sleeps while the IH is off (`gIhOn && gEopIrq` guards the count), so
  "slept" here is WAITSEQ calls minus "waits done in spin".
- Owner: **"I either can't tell or it's worse, I don't know."** Set back to 50 us, following the rule "if anything gets worse,
  set it back at once". Not a clear regression either: the window was 2 minutes, and the owner knew which value was on.
- Whether fewer wakeups means less heat: unverified. There is still no CPU temperature or package-power reading.

## Live test 4 (2026-11-19 22:16-22:28): blind A/B of `debug.phxwait_step_us` 50 vs 200

A root script set the step for 4 stretches of 3 min, two at 50 and two at 200, in an order it picked at random and
kept root-only until the end (neither the owner nor the agent knew it). Then back to 50 by itself, with a stop file for
an early rollback. `powermetrics --samplers tasks -i 10000` ran throughout. Owner using the desktop normally, noting
each stretch. Order: **AABB** (1, 2 = 50 us; 3, 4 = 200 us).

| stretch | step | owner (verbatim, shortened) | WindowServer wakeups/s, median (range) | WindowServer CPU ms/s, median (range) |
|---|---|---|---|---|
| 1 | 50 | "pretty smooth ... my reference"; input lag, a bit of tearing | 1673 (293-3893) | 241 (53-439) |
| 2 | 50 | "can't tell any difference, maybe a bit more laggy" | 2458 (303-5589) | 309 (61-562) |
| 3 | 200 | "more fluent ... the input delay is reduced", the smoothest of the four; guessed it was 50 | 352 (99-1075) | 141 (54-443) |
| 4 | 200 | "bit more screen tearing, bit more laggy" | 137 (53-986) | 71 (24-453) |

- Owner's summary: "the difference between all of these 4 tests is minuscule and I'm sure mostly placebo". The
  stretch they found smoothest ran at 200. So 200 is **not worse** in a blind test (verified on the XTX machine, 12 min).
- WindowServer wakes 5-15x less at 200 in this run. The desktop was busier than in live test 3: up to 5589
  wakeups/s at 50. CPU time was lower too, but activity differed per stretch (no present count per stretch), so the
  CPU difference is not a clean result.
- Input lag and tearing were noted at both values: they aren't caused by the wait step. The owner suspects part of
  the input lag is macOS pointer acceleration ("Pointer precision" off felt "very fluent"). Tearing is a separate open
  item (GPU present has no vsync'd flip yet; unverified).
- Fan noise during stretch 3 (200 us) was attributed by the owner to 50: there is no sensor to check it.

## Restart 2026-11-19 22:44: a kernel panic, most likely at the end of the shutdown; the waitstep run-once ran on the boot after

The owner restarted (Apple menu > Restart) at ~22:44 and came back to a kernel panic screen (AFK, no photo). Then
power-cycled; the machine booted normally at 22:46:36.

- **Where it happened (inferred, unverified):** the unified log runs to 22:44:05.98 (the shutdown: utun/skywalk
  teardown, then `IOAccelDisplayPipe::display_change_handler event 0x91`, the last kernel line) and resumes at
  22:46:36 with the new kernel. The EFI partition has **no OpenCore log between the 22:00 boot
  (`opencore-2026-11-19-210036.txt`, UTC) and the 22:46 boot (`...214633.txt`)**, and `phxboot.log` has no run either.
  So no boot went through OpenCore in between: the panic was most likely at the end of the shutdown, not in a new
  boot. Earlier restarts of this install each left a `shutdown_stall` report (slow daemons, no panic); this one left
  none. A USB stick ("Install macOS Tahoe") was published at 22:44:05, during the shutdown.
- **No panic report:** nothing in `/Library/Logs/DiagnosticReports`, no `aapl,panic-info` in NVRAM. So the backtrace
  and the kext involved are unknown. If it happens again: a phone photo of the panic screen (`panicked task`,
  "Kernel Extensions in backtrace").
- **Not the staged change:** the run-once and the flag were still on the EFI partition and were taken at 22:46:41,
  in the boot after. The 22:46 boot: `daemon-install: installed ... 5c2c1513...` (rollback copy kept), flag
  `phxboot-waitstep` = 200 in ~/gpu, and the old daemon's own `wscdcopy: NVMTL_WS_PRESENT_CDCOPY=1 for the restarted
  WindowServer` line: **restart 2 of the E3 flag verified**.
- **The run-once's own `sysctl -w debug.phxwait_step_us=200` failed: `unknown oid`.** PhxBringup registers its sysctls
  with the first user client (`newUserClient`), after the pre-hook. The daemon's `waitstep_boot` runs after
  "bring-up + claim", when the oid exists (it reads 50 later in the same boot). That boot stayed at 50.

## Next steps (one change at a time)

1. **Flicker:** `/Library/GPUBundles/nvmtl/ws-gpu-present-async` is a live switch (the plugin re-reads it every
   second). With it, the copy runs after the composite has finished, and WindowServer no longer waits for its swap
   commit. Toggle it live with the owner watching; rollback = delete the file (takes effect within 1 s). If it fixes the flicker,
   the lasting fix is to make the non-async path wait for completion before presenting (plugin change, deployed through the
   EFI), or to make async the default for GPU present.
2. **Heat:** after the flicker step, measure again with `powermetrics`. Candidates: present at most once per vblank
   (drop a present whose slot would only replace a pending flip), and waits that do not poll every 50 us
   (`debug.phxwait_step_us` is a live sysctl; the IRQ wait path exists but is off and unverified on the XTX).
