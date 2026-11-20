# Handoff: the display capture on Linux, then back to macOS (2026-11-20)

For the Claude session on the Linux side (Omarchy, Hyprland) of the XTX machine. macOS runs the desktop at
2560x1440 **60 Hz** and has no HDR. To go further it needs Linux's own register values for this card and this monitor
(LG ULTRAGEAR+, DP, 2560x1440 up to 240 Hz, HDR). Background and plan:
[findings/2026-11-19-refresh-and-hdr.md](findings/2026-11-19-refresh-and-hdr.md). Key fact: under macOS the firmware
trains DP B at **HBR2 x4, 12 bpc**; 120 Hz (10 bpc) and 144 Hz (8 bpc) fit that link, 240 Hz needs HBR3.

## Steps

1. Get the branch (same checkout path as the macOS side, or any checkout of `ownback/vermilion`):

   ```sh
   git fetch fork && git checkout ownback/gpu-refresh-hdr && git merge --ff-only fork/ownback/gpu-refresh-hdr
   ```

   No network or no checkout: the same two files are on the EFI partition, `/boot/vermilion/capture-tool/`.

2. Check the preconditions (no root needed for the first two):
   - `python3 tools/capture/linux_display_capture.py /tmp/x --dry-run --hbr2 --hdr` lists 11 steps;
   - `hyprctl monitors` shows the LG on a `DP-n` connector;
   - as root: `ls /sys/kernel/tracing/events/amdgpu_dm/amdgpu_dc_wreg` and `ls /sys/kernel/debug/dri/*/amdgpu_dm_dtn_log`.
     If the tracepoint is missing or debugfs is not mounted (`mount -t debugfs none /sys/kernel/debug`), say so to the
     owner and stop; don't patch the kernel.

3. Run it in three passes, as root. Root password: T3's `request_secret` with file delivery, one request per sudo
   call, never in chat. Tell the owner first: **the screen will switch modes and may go black for a few seconds per
   step.** Each pass ends with `hyprctl reload` (back to the config file).

   ```sh
   sudo -S -p '' python3 tools/capture/linux_display_capture.py /boot/vermilion/capture < "$file"            # base, 60, 120, 144, 240
   sudo -S -p '' python3 tools/capture/linux_display_capture.py /boot/vermilion/capture --steps hdr-60,hdr-120 < "$file"
   sudo -S -p '' python3 tools/capture/linux_display_capture.py /boot/vermilion/capture --steps hbr2-60,hbr2-120,hbr2-144,hbr2-120-10 < "$file"
   ```

   - The HDR pass needs Hyprland's `cm, hdr` monitor option. If Hyprland refuses it (summary shows the mode without
     HDR, or the trace has no SDP/OGAM writes), note that; it is not worth upgrading Hyprland for.
   - The HBR2 pass forces Linux's link to HBR2 x4 through debugfs `link_settings` (amdgpu retrains its own link). It stays
     forced until power-off; run it last.
   - The script itself only reads, apart from those two amdgpu-mediated things (mode sets, link preference). It never
     reads the MMVM semaphores or the AUX/I2C data FIFOs.

4. Check the output in `/boot/vermilion/capture/display-<date>/`:
   - `summary.txt`: per step the mode Hyprland reports, OTG0 frames per second (expect ~60/120/144/240), lanes, VID_M/N,
     DP_PIXEL_FORMAT;
   - each step has `trace-modeset.txt` (thousands of `amdgpu_dc_wreg` lines), `regs.txt`, `amdgpu_dm_dtn_log.txt`,
     `connector.txt` (link_settings, output_bpc, DSC), `dpcd-*.bin`;
   - `edid-noserial.bin` is 384 bytes and has no serial: `xxd edid-noserial.bin | head` shows zeros at bytes 12-15.

5. Write a short "Linux side" section at the end of the findings note: per step the link rate and lanes
   (`link_settings` current), bpc, DSC on/off, DISPCLK/DPPCLK (DTN log), DET segments, the watermark set A values, and
   for HDR which DP SDP / MSA / OGAM registers were written. Mark anything inferred. Commit that note (not the capture)
   and push to `fork ownback/gpu-refresh-hdr`. The capture files stay on the EFI partition only.

6. Tell the owner, in one line: **Shut Down Linux (power off, not restart), power on, and macOS starts by itself**
   (OpenCore first; pick Macintosh HD if the picker waits). The macOS thread reads the capture from the EFI partition.

## Optional, only if the capture is done and time is left

Turn the `hbr2-120` and `hbr2-144` traces into draft pipe-0 plans: the register groups and format of
`src/vermilion-boot/phx_p0_plan_1080p144.inc` (DTO, OTG timing, frame position, HUBP DLG/TTU, MSA), with values from
the steady `regs.txt`, and note every register the trace wrote that the 1080p plans don't cover (expected: FMT/OPP
depth, DP pixel format, DCCG). Drafts go in the findings as tables, not into the kext.

## Don't

- No EFI install, no `efibootmgr`, no EFI variables, nothing under `/boot/EFI` or `/boot/vermilion/{run-once.sh,daemon,flags}`.
- No register writes outside amdgpu (no `umr -w`, no `setpci`, no writes to `amdgpu_regs`), no VBIOS access.
- No amdgpu reload, no kernel parameter change. Don't remove the reboot guard (`vermilion-reboot-to-linux.service`).
- No EDID with a serial, no register dump, no capture file in git.

## Capture v2 (2026-11-20): 240 Hz link training and HDR

Goal now: 240 Hz with HDR on macOS. Linux trains HBR3 through the DMCUB, so the register trace misses the bytes macOS
needs. The tool (branch `ownback/gpu-refresh-hdr`, copy on the EFI partition `vermilion/capture-tool/`) now adds a kprobe
on `dmub_srv_cmd_queue` (16 dwords per DMUB command, `phxcap/dmubcmd*` lines in `trace-modeset.txt`, removed at the end).

1. `git pull` (or use the EFI copy), then as root:
   `python3 tools/capture/linux_display_capture.py /boot/vermilion/capture --steps base,144,240,144,240-10,hdr-240,240-nodsc,hdr-120`
   Each `step: ... N DMUB commands` line in the summary should show N > 0 for 240. If the kprobe line says "none",
   report `grep -i dmub_srv /sys/kernel/tracing/available_filter_functions` and stop.
2. `240-nodsc` forces DSC off for one mode set (debugfs `dsc_clock_en` = 2, back to 0 after). A failed mode set is a valid
   answer: write down what Hyprland reports and what `link_settings` / `odm_combine_segments` show.
3. Decode, in a findings note: for the 144 -> 240 switch, the DMUB commands in order (type/subtype from dword 0: byte 0
   type, byte 1 subtype, bytes 2-3 payload size). Expected: 128/1 TRANSMITTER_CONTROL (dword 2 = symclk in 10 kHz,
   81000 for HBR3), DP_AUX_ACCESS (DPCD 0x100-0x103 writes, 0x120 FEC, 0x160 DSC, TRAINING_LANEx_SET), anything else
   by name from Linux `dmub_cmd.h`. Same table for `240-nodsc` if it lit. Mark inferred fields.
4. Then as before: commit the note (not the capture), push, owner powers off fully, macOS.
