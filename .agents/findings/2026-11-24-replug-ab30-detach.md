# The 00:05 replug and the GPU's audio function: no detach, and nothing attached to detach (2026-11-24)

Phase 2 item 1, read-only. XTX machine, macOS 26.7.1 (25G241). Question: did the owner's DP unplug / HDMI / DP replug
at about 00:05 on 2026-11-24 detach a driver from the GPU's audio function (PCI 3:0:1, 1002:ab30, the nub `HDAU`)? A
detach marks the nub "needing hardware reset", and the next match on it hot-resets the whole GPU
([finding](2026-11-24-gfxhda-detach-gpu-hot-reset.md)). And is a replug test safe to ask the owner for now?

## Sources

- The unified log (`/usr/bin/log show`), 2026-11-24 00:00-00:20 and everything since 00:24.
- `~/vermilion-captures/thread04-input-20261124/last40m.logarchive` (saved by thread 04; kernel lines from 00:08:02 to
  00:48).
- `~/vermilion-captures/replug-black-20261124.txt` (thread 04's register read-out at 00:12:04).
- `/var/log/phxboot.log`, the 23:18:49 boot (daemon clock).
- `/var/log` and `/Library/Logs/DiagnosticReports`: nothing about the audio function in that window.

## What the logs show

1. **The kernel lines of the replug itself are gone.** The unified log has 329,243 lines for 00:00-00:20, but none
   from the kernel. Its kernel lines start with the boot after 00:22. The saved archive starts at 00:08:02. The
   minutes around 00:05 (unplug, HDMI, back on DP) have no kernel record.
2. **Nothing was attached to ab30 in that boot.** It ran EFI change 23 (VermilionTrust for the video decoder). That
   stage has no VermilionAudio, and without VermilionAudio's personality AppleGFXHDA doesn't match ab30. The daemon
   log of that boot has no `audio:` line. The archive (132,651 kernel lines, 00:08-00:48) has no AppleGFXHDA line
   before the next boot, and AppleGFXHDA logs its probe and start when it attaches (01:41:41 below).
3. **No detach or hot reset in the part of that boot that is on record** (00:08:02 to the restart at 00:13:35). The
   archive has no `detachFromChild`, `needing hardware reset` or `Performing hot reset` line. IOPCIFamily logged at
   the same level as in the 07:46 boot that recorded the 07:55 detach:
   `(IOPCIFamily) pci (build 17:35:17 Aug 18 2026), flags 0x80c3080, log mode flags 0x4, log flags 0x80000001`.
   The only IOPCIFamily lines from 00:08 to 00:13 are USB: an Xbox controller reconnecting (`childPublished()`).
4. **The replug left the display black, not the GPU reset.** At 00:12:04 the register read-out shows the link
   trained on DP-B (`lane01=0x77 lane23=0x77 align=0x01 sink_count=1`), the replay engine `result=DONE` at 144 Hz,
   `recoveries=0/2`, `dmub_dead=0`. That is the known black-after-replug problem (phase 2 item 5).

So the 00:05 replug can't tell us whether a replug detaches AppleGFXHDA. At that time there was no AppleGFXHDA on
ab30.

## Every detach on record (the unified log since 00:24, all boots)

```
2026-11-24 01:44:04.055 (AppleGFXHDA) [0x30001]AppleGFXHDAControler[0]::terminate
2026-11-24 01:44:04.055 (IOPCIFamily) [detachFromChild()] Marking HDAU as needing hardware reset
2026-11-24 01:55:14.032 (AppleGFXHDA) [0x30001]AppleGFXHDAControler[0]::terminate
2026-11-24 01:55:14.032 (IOPCIFamily) [detachFromChild()] Marking HDAU as needing hardware reset
2026-11-24 07:55:14.864 (IOPCIFamily) [detachFromChild()] Marking HDAU as needing hardware reset
2026-11-24 07:55:18.317 (IOPCIFamily) [getResources()] Performing hot reset on nub HDAU
```

- **01:41 to 01:55** were thread 04's tests by hand, before VermilionAudio 1.6.0 (committed 17:43). AppleGFXHDA was
  attached at 01:41:41 (`childPublished() child HDAU ... (3:0:1)`, then `AppleGFXHDAController[0]::start`) and again
  at 01:49:12, and terminated at 01:44:04 and 01:55:14. What terminated it isn't in the logs I read: no `sysctl` or
  `audio_match` line, and the next daemon start is about two minutes later (unverified whether a restart caused
  it). **Neither mark was followed by a hot reset.** Nothing matched HDAU again before the boot ended.
- **07:55** is the known case: `audio_match.sh off` then `on`, and the re-match hot-reset the GPU.
- **Since 17:27** (VermilionAudio 1.6+, EFI change 33 with DP audio at every boot): no detach and no hot reset. The
  current boot (since 18:30) has the controller attached: `AppleGFXHDAEGController@0,1 ... registered, matched,
  active`, and `debug.phxaudio_stat` says `match: framebuffer properties set on 1 PhxAMDFB, AppleDisplay notification
  re-delivered for 1 display(s)`.

What this means: the mark alone does nothing. The GPU is reset only at the next match on HDAU. VermilionAudio 1.6+
removes its ab30 personality right after the attach, and `debug.phxaudio_match` refuses a second attach. So in a boot
with audio attached, nothing can match HDAU a second time. The exception would be a personality that matches ab30 by
class, added or loaded later (unverified: none seen; at boot no driver takes HDAU before VermilionAudio's
personality).

## Is a replug test safe to ask for?

**Not yet; not because of the audio function.**
- **Through the audio function** the risk is low: a replug would hot-reset the GPU only if it terminated AppleGFXHDA
  *and* something matched HDAU again in the same boot. The second can't happen with VermilionAudio 1.7.2
  (unverified on hardware: no replug with audio attached has been done).
- **But the test can't be read today.** After a DP replug the LG stays black until a restart (phase 2 item 5).
  Whether sound comes back can't be judged on a black screen, and the restart ends the test anyway. That is the
  "blocked by the black screen" of `2026-11-24-audit-thread-handover.md`.

**Recommendation:** ask for the replug test with phase 2 item 5, once a replug brings the picture back. During the test:
- keep a `log stream --predicate 'process == "kernel" AND (eventMessage CONTAINS "detachFromChild" OR eventMessage
  CONTAINS "hot reset" OR eventMessage CONTAINS "AppleGFXHDA")'` running;
- no `audio_match.sh`, `kmutil load` or other kext load until the next restart;
- if a `Marking HDAU as needing hardware reset` line appears, restart at once. That line means the next match would
  reset the GPU.

The sleep half of OWNER-TESTS 04 stays blocked (system sleep is disabled; display sleep is phase 2 item 6).

## Not covered

- What AppleGFXHDA does on a display unplug while it has an engine running: the code wasn't read, and no such event
  is on record.
- What terminated AppleGFXHDA at 01:44 and 01:55.
- The daemon's clock runs about 1-1.5 minutes off the unified log's that night (`=== system wallclock time
  adjusted`). The two sets of times can't be lined up to the second.
