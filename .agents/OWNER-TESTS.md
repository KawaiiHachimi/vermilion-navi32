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
- [ ] **Watch: a power-on after Linux that does not reach macOS** (seen once, 2026-11-23 05:09, the first power-on
      after a Linux session; the second power-on booted). This is the cold-boot hazard: keep watching, it is not
      closed as a one-off. If it happens again, before powering off:
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

## 02 GPU and CPU performance, power

- [ ] **Linux reference boot:** boot Linux once and let its agent session follow
      [HANDOFF-LINUX-REFERENCE.md](HANDOFF-LINUX-REFERENCE.md) (read-only; about 30 minutes). Afterwards **shut down
      Linux fully** (power off, not restart), then power on into macOS.
- [ ] **BIOS photos:** at power-on press F2 (or Del), then F7 for Advanced Mode. Photograph these pages and change
      nothing: Advanced > AMD CBS (Global C-state Control, CPPC, CPPC Preferred Cores, Power Supply Idle Control) and
      Ai Tweaker > Precision Boost Overdrive / Eco mode. Exit without saving.
- [ ] **A Metal game or benchmark** you own: run it for 20 minutes. Report the frame rate, stutter, wrong rendering,
      and whether the fans behave.

## 03 Graphics APIs and video

- [ ] **WebGL and WebGPU in Safari:** load the WebGL Aquarium and a WebGPU sample (for example from
      webgpu.github.io/webgpu-samples). Does it draw, is it smooth, does anything crash?
- [ ] **The same in a Chromium browser** (Chrome, Brave, Edge).
- [ ] **Minecraft:** start it in Prism Launcher, press F3, and read the renderer line and the frame rate. Expected:
      "Apple Software Renderer", slow.
- [ ] **4K video:** play a 4K HEVC or AV1 video (YouTube 4K in Safari) and watch CPU use in Activity Monitor.
      Expected: high (software decode).
- [ ] **Quick Look:** open `/System/Library/Desktop Pictures` in Finder's Gallery view, press Space on a `.heic`
      file and on a video file, and note how long each preview takes.
- [ ] **Video wallpaper:** System Settings > Wallpaper, pick a moving (Aerial) wallpaper. Does it play? Note Activity
      Monitor's CPU use for about a minute.

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
