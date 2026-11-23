# Owner tests

Everything only the owner can check: by eye, by ear, with another device, or in the BIOS. This list merges section 4
of the eight final reports of the first round (2026-11-23), without duplicates, grouped by the thread of the series
([ROADMAP.md](../ROADMAP.md)) that needs the answer. A thread may ask for its tests at any time; doing a later
thread's tests early is fine.

How to report: tick the box, add the date and one line of result, or tell the active thread. Where a test asks for a
command's output, paste the whole output. None of these needs an EFI change. Ask first before anything that does.

## Decisions (no test)

- [ ] **Linux in the public guide:** is a Linux dual boot (or live USB) a requirement of the public install? Today
      the ACPI dump, the monitor capture and `phxfb-squat` all need Linux. (thread 10)
- [ ] **Installer partition `disk0s4`:** keep it as the 26.7.1 reinstall path, or delete it and grow the macOS
      container? (thread 10)
- [ ] **HiDPI:** do you want scaled modes ("looks like 1920x1080") now that the reported size is right (2026-11-23)? macOS lists none by
      itself (TODO 01, "Scaled modes"). (thread 01)
- [ ] **File Sharing:** your Public folder is set up as a guest-writable share. Keep that? (thread 11)

## 01 GPU stability

- [ ] **White flash in Variable:** use Variable refresh for 30+ minutes of normal work. Note any white flash and the
      time, then run `sysctl debug.phxvrr_stat` and send the output (expect `dbuf now 0x02000000`, and HUBP0's first
      hex digit 0 or 1).
- [ ] **10-bit banding:** turn HDR on, open a dark grey-to-black gradient full screen (any "gradient test" image) and
      look for steps. Then turn HDR off and compare.
- [x] **Login at 60 Hz HDR:** 2026-11-23, about a dozen restarts on b151-b155, every one landed (one needed the
      b154 re-switch). Kept for the record: in System Settings > Displays pick HDR at 60 Hz, then Apple menu >
      Restart. Repeat 3 times. If the screen stays black after login, wait 2 minutes: the daemon should restart the
      Mac once by itself. If it stays black after that too, run `sysctl debug.phxp0odm_stat` (from T3 Code on the
      phone) and send it. Then set your usual mode back.
- [ ] **NVRAM test boot:** inconclusive on 2026-11-23 (the change may never have booted); redone with the Linux
      session (TODO 01 "Panic reports").
- [x] **Allow click** (PhxAccel with the physical size): done 2026-11-23; size now 591 x 332 mm.
- [x] **Display sleep:** 2026-11-23, the picture comes back; the LG stays on showing black instead of standby (owner).
      The fix moved to thread 05.
- [ ] **Watch: a boot that does not reach macOS and leaves no macOS log** (twice so far, see the counter). The first
      was the cold-boot hazard after Linux; keep watching, it is not closed as a one-off. **On the next restarts,
      glance at the screen while it boots** (OpenCore picker, Apple logo, black) even when nothing goes wrong.
      **Rule (coordination thread, 2026-11-23): a third such boot on b159 -> revert to EFI change 19 before going on.**

      | # | date, time (CET) | build / EFI change | cold or warm | screen | what the logs show |
      |---|---|---|---|---|---|
      | 1 | 2026-11-23 05:09 | b155 / change 14 | cold: first power-on after a Linux session | not seen | OpenCore log ends at ExitBootServices as a good boot's does; no unified log, no daemon log, no panic report ([finding](findings/2026-11-23-gpu-stability.md), section 11) |
      | 2 | 2026-11-23 15:45 | b159 / change 20 (BAR0 1 GiB, first boot) | warm: `shutdown -r` from macOS | not seen (owner: "one restart didn't work, the second one did") | shutdown clean (logs to 15:45:02); OpenCore log `opencore-2026-11-23-144551.txt` has 3 lines and stops: `OCC: Missing compatible GOP - Not Found`, `Missing GOP on ConOut`, `Changed resolution to 0x0@0 ... Unsupported`; no boot.efi line, so macOS's kernel and b159 never ran. The good boot after it (14:49 UTC) has no such lines. Cause unknown (unverified: the firmware's GPU GOP did not come up after a warm restart) |

      Good boots the owner watched: 2026-11-23 16:40 (b160 / change 21, warm restart from macOS): first try, verbose
      text the whole way, no Apple logo (the boots of changes 19 and 20 had shown the Apple logo and a progress bar).

      If it happens again, before powering off:
      1. Photograph the screen (verbose text: the last 10 lines; or the Apple logo / black, and whether the progress
         bar moved).
      2. Note: how long since Linux was powered off, whether the power-off was full (not a restart), and the time.
      3. Power off fully, power on, boot macOS, and tell the active thread. It collects the OpenCore log of the failed
         boot (`<ESP>/opencore-<date>-<UTC time>.txt`, named one hour behind CET), `log show` for that time, and
         `/var/log/phxboot.log` (is there an entry for the failed boot?).
- [ ] **Tearing:** at a fixed 60 Hz, drag a window quickly left and right. Film it with the phone (slow motion if
      possible) and note where on screen it tears.
- [ ] **Input lag:** System Settings > Mouse: "Pointer acceleration" off. Compare the cursor at 60 Hz with 240 Hz:
      does 60 Hz still feel laggy?
- [ ] **Cold start:** Shut Down, power on, boot macOS. Does the login screen come up at your saved rate and mode?
- [ ] **If a panic happens:** photograph the whole screen, especially "panicked task" and "Kernel Extensions in
      backtrace".

## 13 Performance parity (from 02; thread 02 is merged)

- [x] Linux reference boot and BIOS photos: done 2026-11-23 06:41-07:10 (results in the perf finding, section 8).
- [ ] **VRAM buffers in daily use** (thread 02 part B): Horizon was only a measuring stick (the owner rarely plays
      it). Before VRAM buffers become the default for every app (M3), they go to the apps used every day; the
      agent picks them from what runs on the machine. Report anything odd (stutter, wrong rendering, a freeze).
      Off again: `rm ~/gpu/phxboot-bovram` and restart.
- [ ] **App windows with the tiled drawable** (thread 02 part C, since 2026-11-23 16:40): every app that draws with
      Metal into its window now draws into a fast tiled image that is copied to the window once per frame. Over the
      next days, watch for a window that shows old content, flickers, stays black or lags behind (games, Electron or
      Chromium apps such as T3 Code, Blender). Off for every newly opened window, no restart:
      `sudo touch /Library/GPUBundles/nvmtl/drawable-tiled-off`, then reopen the app; on again: remove the file.
- [ ] **A Metal game or benchmark** you own: run it for 20 minutes. Report the frame rate, stutter, wrong rendering,
      and whether the fans behave.

## 03 Graphics APIs and video

Since 2026-11-23 (thread 03): Safari's GPU process has the Metal plugin, and HEVC / AV1 decode on the GPU (VCN). Quit
and reopen Safari once before these tests (a Safari that was open before 18:07 keeps the old GPU process).

- [ ] **WebGL and WebGPU in Safari:** load the WebGL Aquarium and a WebGPU sample (for example from
      webgpu.github.io/webgpu-samples). Does it draw, is it smooth, does anything crash?
- [ ] **The same in a Chromium browser** (Chrome, Brave, Edge).
- [ ] **Minecraft:** start it in Prism Launcher, press F3, and read the renderer line and the frame rate.
- [ ] **4K video:** play a 4K video in Safari (YouTube 4K: AV1 or VP9) and an HEVC `.mov` in QuickTime, and watch
      CPU use in Activity Monitor. Expected now: low for HEVC / AV1 (hardware decode); smooth, no blocks or colour
      faults. Note anything odd and the time.
- [ ] **Quick Look:** open `/System/Library/Desktop Pictures` in Finder's Gallery view, press Space on a `.heic`
      file and on a video file, and note how long each preview takes.
- [ ] **Video wallpaper:** System Settings > Wallpaper, pick a moving (Aerial) wallpaper. Does it play? Note Activity
      Monitor's CPU use for about a minute.
- [ ] **Photos:** open a few videos and HEIC photos in Photos. Smooth? Any black or green frames?

## 07 Wireless and Continuity

- [ ] **Router channel (no EFI change):** in the router, set 5 GHz to channel **44** at 80 MHz (not "auto", not 52-64
      or 100-140). Reconnect the Mac's Wi-Fi, set the iPhone's AirDrop to "Everyone for 10 Minutes", and try AirDrop
      iPhone -> Mac. Does the Mac appear? Then tell the thread, which measures the ping.
- [ ] **Universal Clipboard**, after the router test: copy text on the iPhone, wait 5 s, press Cmd+V on the Mac.
- [ ] **Bluetooth beyond AirPods:** pair a Bluetooth keyboard or gamepad and use it for 10 minutes. Then restart once:
      does it reconnect by itself?
- [ ] **Sidecar / Universal Control** (needs an iPad): Control Centre > Screen Mirroring, pick the iPad.

## 09 Security and updates

- [ ] **iMessage:** in Messages, send "test" to someone's iPhone and have them reply. Then from your iPhone send to
      this Mac's Apple Account address. Report blue or green, delivered, received.
- [ ] **FaceTime:** call your iPhone from the Mac: 30 s of video, then an audio-only call. Picture and sound both ways?
- [ ] **Camera / microphone prompt:** install Chrome, open `https://webcamtests.com` and start the test. Expected: a
      macOS prompt "Google Chrome would like to access the camera" (and microphone), then Chrome listed under System
      Settings > Privacy & Security > Camera. Without a camera, test the microphone only.
- [ ] **Gatekeeper:** download Firefox's `.dmg` from mozilla.org, open it and launch Firefox. Expected: "Firefox is
      an app downloaded from the Internet. Are you sure...?"
- [ ] **Widevine:** in Chrome and in Firefox open `https://shaka-player-demo.appspot.com`, choose "Angel One
      (multicodec, multilingual, Widevine)" and play 30 s. Optional: Netflix in Chrome.
- [ ] **iCloud Keychain:** System Settings > [your name] > iCloud > Passwords: on (approve on the iPhone if asked).
      Save a test password in Safari, then look for it on the iPhone.
- [ ] **iCloud Drive and Photos:** put a file into iCloud Drive in Finder: does it appear in the iPhone's Files app
      within 5 minutes? Then add a photo in Photos with iCloud Photos on.
- [ ] **Find My:** System Settings > [your name] > iCloud > Find My Mac. Is it on, and does the Mac show on
      icloud.com/find?

## 11 Final validation

- [ ] **USB 3:** run `python3 ~/Projects/vermilion/tools/usbmap/usbwatch.py` in Terminal, then plug a USB 3 stick
      or SSD into rear ports 1, 2, 3, 4 (photo numbering) and a USB-C device into 7 and 8, about 5 s each. Each port
      should print `Super` or `Super+`. Send the output.
- [ ] **Ethernet:** plug a cable into the 2.5G port. System Settings > Network > Ethernet should say "Connected". Then
      run `networkQuality -I en0` and send the result.
- [ ] **Shut Down, not restart:** Apple menu > Shut Down, power on, boot macOS, then run
      `sudo ~/.local/sbin/smartctl -A disk0 | grep Unsafe`. Still 212 means cold shutdowns are clean too.
- [ ] **Time Machine:** attach a spare USB disk (it will be erased), System Settings > General > Time Machine > Add
      Backup Disk. Let the first backup finish, restore one file from the Time Machine browser, report the time and
      any error.
- [ ] **Recovery** (only with the GPU thread's OK): in the OpenCore picker press Space, choose "Recovery 26.7.1",
      check that the Recovery window appears with a cursor, then Restart and boot Macintosh HD as usual.
- [ ] **Speaker distortion:** play the same bass-heavy track from the iPhone over Bluetooth to the speakers, at the
      same loudness as from the Mac. Does it distort too?
- [ ] **Audio hot-plug:** while music plays, unplug the aux cable for 5 s and plug it back in. Does sound come back
      without a reboot?
- [ ] **Microphone:** plug a microphone into the mic jack **before booting**. Speak while watching System Settings >
      Sound > Input (the level bar should move), then record in QuickTime > New Audio Recording.
- [ ] **Webcam:** plug in a USB webcam, open Photo Booth (picture, smoothness), then FaceTime.
- [ ] **Keyboard:** with the German layout type `@ € ä ö ü ß`. Do the Hama keyboard's volume and F-keys do what they
      are labelled for?
- [ ] **Mission Control and Spaces:** press Ctrl-Up, add a desktop with "+", switch with Ctrl-Left / Ctrl-Right ten
      times. Note any stutter, black frames or missing window previews, at 60 Hz and at 240 Hz.
- [ ] **Stage Manager:** with 4 apps open, click between the groups on the left. Note stutter or missing thumbnails.
- [ ] **Screen Sharing:** System Settings > General > Sharing > Screen Sharing on. From another Mac: Finder > Go >
      Connect to Server > `vnc://<this Mac's IP>`. Check the picture and the mouse, then turn it off.
- [ ] **File Sharing:** turn it on, connect from another device, copy a 1 GB file and note the speed, then turn it off.
- [ ] **SSH:** turn Remote Login on, run `ssh <user>@<IP>` from another machine, then turn it off.
- [ ] **Printer or scanner**, if you have one: add it in System Settings > Printers & Scanners, print a test page,
      scan one page.

## Done

- [x] 2026-11-23 **Music crash** ("Drake - Jaded"): plays, "consistently", no new crash report (owner, after EFI
      change 8).
