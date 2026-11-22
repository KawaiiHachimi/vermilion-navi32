# 2026-11-22: the failed Variable -> HDR switch (HUBP0 underflow), and why a new PhxAccel did not load

Machine: RX 7900 XTX, LG ULTRAGEAR+ on DP, macOS 26.7.1, VermilionBoot b149. Everything "on the box" below was read or
run on this machine on 2026-11-22; Linux statements are from the v6.12 sources (`drivers/gpu/drm/amd/display`).

## What the owner saw

- System Settings showed HDR on and "Variable"; the LG was in SDR at a fixed 144 Hz.
- In Variable the whole screen sometimes flashed white for about half a second (rare).

## The stuck state

The replay engine failed one run and then refuses every switch until reboot (`Run.failed`). Twice, on two boots (b147,
b149), the same switch: Variable at 240 (the VRR twin, key 240) -> HDR 240 (key 1440240), leg 1 `1440p240-from`
(240 -> the 144 hub), post check. `debug.phxp0odm_stat`:

```
last_fail_post: hz=144 v_total=0x5eb h_total=0xa9f odm=0xfff00000 stream=0x10001 stream_0.5s=0x10001 hubp0=0x102f0400 ...
```

Everything is the hub's except `HUBP0_DCHUBP_CNTL` (0x3AB3) = 0x102f0400. Decoded with `dcn_3_2_0_sh_mask.h`:

| Bits | Field | Value |
|---|---|---|
| 30:28 | HUBP_UNDERFLOW_STATUS | **1** (the post check's `phxp2::kUnderflow`) |
| 23:20 | HUBP_TIMEOUT_STATUS | 2 |
| 19:16 | HUBP_XRQ_NO_OUTSTANDING_REQ | 0xf (0xe a moment later: a live status, not the failure) |
| 10 | HUBP_UNBOUNDED_REQ_MODE | 1 |

So the plan landed on the hub; the run failed on the underflow status alone.

## The underflow status is sticky

On the box, at the fixed 144 Hz the failed run left: the bit was still set when read later in that boot; `HUBP_UNDERFLOW_CLEAR`
(bit 31) written 1 then 0 cleared it; it did not come back in 25 s (3611 frames). So it records an underflow in the
past, not a running one, and the replay plans never clear it (they write 0x3AB3 with bits 31:28 = 0).

Linux clears it only in `dcn10_did_underflow_occur` (`hubp1_clear_underflow`, a debug / visual-confirm path), never in a
mode set; it also never fails a mode set because of it.

## Where it comes from: V_TOTAL_MIN / MAX written mid-frame

`OTG0_OTG_DOUBLE_BUFFER_CONTROL` (0x501B) reads **0** on macOS: `OTG_DRR_TIMING_DBUF_UPDATE_MODE` = 0, "any time".
Linux sets it to 2 for every OTG at init (`optc3_tg_init` -> `optc3_set_timing_double_buffer(true)`, dcn32's `.tg_init`,
called from `dcn10_init_pipes`): V_TOTAL, V_TOTAL_MIN / MAX and their selectors then latch at the start of a frame. The
GOP does not set it, and no replay plan does (tg_init runs at driver load, outside every captured mode set). So b142's
range and b147's per-flip ramp (6280 window writes in 6497 flips in the failed boot) changed the frame length mid-frame.

Experiment on the box (a scratch tool on the 'N48N' debug selectors; hardware at 144 Hz, range by hand: MIN 0x5eb,
MAX 0xe35, CONTROL 0x00020003; only registers Linux's DRR path writes):

| What | Double buffer mode 0 (b142-b149) | Mode 2 (Linux) |
|---|---|---|
| MAX lowered below the current line | the frame ends at the write (0.00-0.01 ms) | the frame runs to the old MAX (6.1 ms later) |
| CONTROL / MIN / MAX = 0 past V_TOTAL (`vrrOffLocked`) | the frame ends at the write | the frame runs to the old MAX, then the fixed timing |
| Window moved + manual trigger | ends at the write | ends 3-4 lines after the trigger (the normal trigger end) |
| Random +-5 % windows, about 245 per second | **4 HUBP0 underflows in 27 000 window changes** (110 s; status 4, with ODM0 `OPTC_UNDERFLOW_OCCURRED_CURRENT`) | **0 in 19 600** (80 s) |

So without the double buffer a window change can cut a frame short at an arbitrary point, and now and then the pipe
underflows; with it neither happened. The sample is small (0 of 19 600 against 4 of 27 000 is about 1 in 18 by chance), and
it was run at 144 Hz, not at 240 with ODM 2:1 + DSC where the owner's failures were. (unverified at 240)

The white flash: not reproduced on purpose (the stress run flickered, as it rewrote the window 245 times a second). A
frame cut at the write, or the underflow itself, is the likely cause (inferred); b150 on the box will show it.

Not the cause: a MAX below the current line does not make the counter run on to 0x7fff (the OTG compares >=).

## The fix (VermilionBoot b150)

- `vrrOnLocked` sets `OTG_DRR_TIMING_DBUF_UPDATE_MODE` = 2 before the range (Linux's value); the ramp's writes in
  `scanPresent` are unchanged and now latch at the next frame start.
- `vrrOffLocked` writes the boot values, waits for the latch (a frame start, 100 ms cap; Linux
  `optc3_wait_drr_doublebuffer_pending_clear`), then puts the boot mode (0) back, so the replay plans run in the state
  they were always run in on this machine. See decisions.md.
- The replay engine clears HUBP0 / HUBP1 underflow stickies that are set before a run (`stale_cleared` in
  `debug.phxp0odm_stat`), and a post check that fails on the sticky alone clears it once and checks again (`post_retries`);
  an underflow that comes back still fails the run. The pipe-0 engine (b128, stage 10) already treated its own switch's
  stickies this way.
- `debug.phxvrr_stat` shows the double-buffer register, the off wait and HUBP0_DCHUBP_CNTL: bits 30:28 non-zero while
  Variable is on = an underflow happened.

## Why the new PhxAccel did not load

`kextstat` showed UUID F986666C; /Library/Extensions held F954FE49 (b149's). The AuxKC was from 02:22.

- `kmutil load -p` on a kext whose older build is loaded never builds the AuxKC itself: kernelmanagerd logs "auxkc build
  requires reboot, calling out to syspolicyd", syspolicyd logs "Triggered kext upgrade" and posts the "system software
  was updated" alert; kmutil prints "not approved to load".
- The AuxKC is rebuilt when **Allow** is clicked in System Settings > Privacy & Security: syspolicyd
  `updatePolicyItems` -> "Calling KM.framework loadExtensions", and the file is written within a second, before any
  restart (02:22:31 the pane opened, 02:22:37 the AuxKC written; again 18:22 today).
- At 17:56 the kext was installed and the Mac restarted from the Apple menu at 17:57 without the click: the old AuxKC
  stayed, and the old PhxAccel loaded from it. The boot daemon only loads PhxAccel when none is loaded.
- `kmutil install --update-all` does not rebuild it (asks for a KDK, changes nothing).
- Check before a restart: the installed binary's UUID (`dwarfdump --uuid`) must be in
  `/Library/KernelCollections/AuxiliaryKernelExtensions.kc` (the LC_UUID bytes are in the file; `kmutil inspect` could
  not read the collection here).
