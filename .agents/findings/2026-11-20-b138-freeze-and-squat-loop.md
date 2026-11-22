# 2026-11-20: b138 froze at 1280x1024, and the recovery rebooted in a loop

Machine: RX 7900 XTX, LG ULTRAGEAR+ on DP, VermilionBoot b138 + PhxAccel b138 + Metal plugin b138.

## Timeline (local time, UTC+1; OpenCore log names are UTC)

- 16:37 boot on b138 (a squat boot). The daemon armed the desktop at 16:38:10 and started its 120 s crash watch.
- 16:39:04 System Settings switched to 2560x1440 at 240 Hz: DONE, GPU present kept running (3000 presents, 0 dropped).
- 16:39:21.600 switch to id 8, 1280x1024 at 75 Hz: DONE, OTG0 measured 75.000 Hz. NMXAccel re-made the framebuffer
  resource at 1280x1024 pitch 5120, WindowServer mapped its new 5 MiB surfaces.
- 16:39:22.145 last line in the unified log, from any process (the kernel's periodic Wi-Fi lines stop too). The
  daemon never finished its watch, so its in-progress mark stayed. The owner powered off at ~19:00.
- 19:00, 19:01, 19:14: three macOS boots, each a squat boot. The first saw the mark (kill switch on, `nvram -d
  phxfb-squat`, reboot "onto IONDRV"). The next two saw the kill switch on a squat boot and rebooted again.
- The owner went to Linux (Omarchy). That session moved the b138 EFI to `EFI/OC.bad`, put b137 (`OC.prev`) back as
  `EFI/OC`, and the `phxfb-squat` EFI variable is gone (most likely deleted through efivarfs). 19:19: not a squat boot,
  kill switch on, GOP desktop without acceleration.

## Why the recovery looped

`DisableVariableWrite` is on (installer fix for this board), so no NVRAM write or delete from macOS reaches the
firmware. `phxfb-squat` was written once from Linux (TODO, 2026-11-19), so it stayed set at every boot: the daemon log
has 35 squat boots and no re-arm at all. The squatter's removal at probe and the daemon's `nvram -d` only changed
macOS's copy. The 2026-11-19 decision predicted exactly this loop for an injected `phxfb-squat`. The documented manual
way out is Reset NVRAM in the OpenCore picker.

The same quirk blocks macOS from saving a panic log to NVRAM, and `debug=0x100` keeps a panicked machine on screen
instead of rebooting. So a kernel panic and a hard hang look the same here: no report on the next boot.

## The freeze (cause not confirmed)

Leading hypothesis: GPU present restarted at 1280x1024. Below 1440p, b138's plane check passes when Linux's pitch
equals the width (1280x1024: pitch 1280; 1024x768: pitch 1024). The plugin's new size-change path retries 0.5 s
after the change, which matches the freeze time (21.60 -> 22.15). That would be the first use on the XTX of the
**small** slot layout (VRAM 24/32/40 MiB, the XT's 1080p layout); every earlier XTX present used the large layout.
Head 0's flip buffer B sits over the small slots at 16-30.1 MiB (phxhead.h), which was only safe because GPU present
never used small slots on this machine. Not proven: the kext's IOLog lines are not in the unified log, and the
plugin's logs were in /tmp.

Checked and unlikely: the CPU copy (`nvrm-accel.cpp`) bounds the rows by the scanout's width and height, and the source
is mapped at its own height x pitch.
