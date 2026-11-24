# Audit: boot, bring-up and display (2026-11-24)

Scope: `src/vermilion-boot/` (bring-up, DCN pipes, DMUB, N48N), `tools/phxctl/` (boot daemon, bring-up scripts),
`efi/`, the display side of `src/vermilion-accel/` (PhxAMDFB, PhxAGDC). Read-only audit: code read on
`ownback/megamind` (= fork/main + the machine-lock fix), machine checked today with ioreg, sysctl, nvram -p,
system_profiler, `log show`, `/var/log/phxboot.log`, `bootcheck.sh` (no sudo), and two read-only probes in the scratchpad
(CoreGraphics mode list, CoreBrightness Night Shift query). Mock tests: `bash tests/mock/run.sh` on main's daemon, 260
passed, 0 failed (today).

Machine today (XTX machine, seen 2026-11-24 20:10): macOS 26.7.1 25G241, VermilionBoot b160 (`phx-build` 160),
VermilionTrust 1.0.4, VermilionAudio 1.7.2 (EFI change 33, branch `ownback/dp-hdmi-audio`), PhxAccel UUID `2B25514A`.
LG 27GS95QE on DP B at 2560x1440 240 Hz, 24-bit (ARGB8888), HDR off, Variable offered. Boot 18:30:20, daemon
`SUCCESS at 18:32:44`, replay engine `attempts=10 dones=7 failed=0`.

**Drift:** the machine runs the dp-hdmi-audio branch's daemon (main + the loginwindow first-run fix + `audio_boot`) and
EFI change 33; main's PROJECT_KNOWLEDGE still says EFI change 23 and HDR on. `~/gpu/phxboot-nohdr` exists since
2026-11-24 00:59. `CHANGELOG.md` says b160 is "not on the box yet"; it is.

States: works / broken / missing / **not standard** = works, but not the way Apple would ship it (ROADMAP "The standard").
"today" = seen on the XTX machine on 2026-11-24. Size: S about a day, M days, L weeks.

| # | Part (as a Mac user sees it) | State | Evidence | Next step | Size |
|---|---|---|---|---|---|
| 1 | Boot picker, boot screen, Apple logo | not standard | OpenCore text picker every boot, 10 s timeout, verbose `-v`, `AppleDebug`, log `Target 67` (`efi/machines/b650e-i-xtx.json:68-69,79`); boot-args today include `-v keepsyms=1 debug=0x100 phxdebug=1`. Owner watched 2026-11-23 16:40: "verbose text the whole way, no Apple logo" (OWNER-TESTS 01). A real Mac: no picker unless Option is held, Apple logo with a progress bar | Release profile for ryzentosh: `ShowPicker false` + hotkey, OpenCanopy, no `-v`, no debug logging; drop `phxdebug=1` once bring-up is in the kernel (#3) | S |
| 2 | Boot to desktop (logo to login to desktop) | not standard | Squat boot: `PhxFBSquatter` holds IOFramebuffer (`phx_classes.cpp:415-460`), so nothing draws until PhxAMDFB takes the panel at the claim (today 18:30:34, ~14 s after the kernel started). The first WindowServer runs without Metal and shows a name + password login (the daemon rewrites loginwindow's `SHOWFULLNAME` to dodge a crash, `phxboot-navi31.sh:134-190`); the daemon then `killall -9 WindowServer` (`:660`), turns on auto-login itself (`/etc/kcpassword`), and the restarted WindowServer switches 144 -> 240 Hz with a link retrain (daemon log today 18:30:35-18:30:47). AGDC is created by a sysctl at arm time, not at boot (`phxagdc.cpp:1-6`) | Bring-up in the kernel (#3), PhxAMDFB and AGDC published at boot so the first WindowServer is the accelerated one: no restart, no login-window rewrites | L |
| 3 | GPU power-up | not standard (ROADMAP list) | A root LaunchDaemon runs `navi31_up.sh` -> `bringup_navi31.sh` (PSP, SMU, RLC, IMU firmware loaded through `phxctl` debug selectors, which need `phxdebug=1`) -> `mesboot` (GART, MES, KIQ, queues). Firmware read from `~/gpu/fw/navi31`. Display runs the GOP's DMCUB build, Vermilion never loads its own (`PhxDmub.h:260`, `PhxDpRun.h:152` gate on `0x07008742`). Works every boot today: `navi31 up: compute + gfx queues ready` | Port the same sequence (Linux amdgpu's order, already traced) into `PhxBringup::start` with the 3-min watchdog as a kernel timer; firmware from the kext bundle or a system path; hw-safety review | L |
| 4 | Cold boot reliability | works (one open case) | Daemon log 2026-11-22..24: 78 boots logged; 66 SUCCESS, 4 safe desktop, 5 restarted by hand during the watch, 2 sessions froze (the HDR freeze, #16), 1 recovery boot. Outside it: 2026-11-23 05:09 cold boot after Linux left no macOS log at all (OWNER-TESTS 01 #1) | Keep the counter; get a panic / early-boot record first (#29) | M |
| 5 | Warm restart | works (one failure, not quiesced) | 16 restarts today 00:54-18:30, all SUCCESS, previous-shutdown cause 5. 2026-11-23 15:45: a warm restart stopped in OpenCore with `Missing compatible GOP` (OWNER-TESTS 01 #2). The kext does not quiesce the GPU at restart: `PhxBringup::stop` has no GPU teardown (`phx_core.cpp:86`), the shutdown notifier only disables the IH (`phx_sysctl.cpp:275-278`); Linux sends the SMU its unload (`amdgpu_pci_shutdown`, `mp1_state = UNLOAD`). Whether that caused the GOP failure is (unverified) | Linux's shutdown sequence for SMU 13.0.0 / PSP 13.0.0 at `kIOMessageSystemWillRestart` / power-off, through `hw-safety-reviewer` | S-M |
| 6 | Shutdown | works (stall reports) | Cause 5 every boot; a `shutdown_stall` report at nearly every restart (18:21, 18:29 today; `/Library/Logs/DiagnosticReports`). Cold power-off with SMART "Unsafe Shutdowns" not checked (TODO 11) | Symbolicate one stall (`spindump -i`), see if the daemon's TERM trap or the GPU is in it | S |
| 7 | Recovery after a freeze or a forced power-off | broken (manual repair) | A boot that does not finish leaves `/var/db/phxboot.pending`; the next boot sets the kill switch, and because the squat cannot be cleared (#30) it runs the "safe desktop" (CPU copy, no GPU present, no high refresh, VRR or HDR) on every boot until someone types `rm ~/gpu/phxboot-off ~/gpu/phxboot-failed` (`phxboot-navi31.sh:273-300, 686`). Seen 2026-11-24 00:17-00:51: four safe-desktop boots in a row after the 00:14 freeze | After N clean safe-desktop boots go back to the full desktop by itself; tell the user in the UI (a notification), not only in the log | M |
| 8 | System sleep | missing | `SleepDisabled Yes`, Sleep Count 0 (bootcheck today); `pmset -a disablesleep 1` since 2026-11-23. No power management in PhxBringup (no `registerPowerDriver` / `setPowerState` in `src/vermilion-boot/`). `ResizeAppleGpuBars 10` is flagged by ocvalidate as "unstable with macOS sleep-wake" (`efi/machines/b650e-i-xtx.json:120-121`) | TODO 05: S3 test on a `-phxbringupoff` boot, then GPU suspend / resume as Linux does it (needs #3) | L |
| 9 | Display sleep | broken | `pmset displaysleepnow`: PhxAMDFB only sends notifications (`phxfb.cpp:1068`), OTG0 / link / stream stay up, the LG shows black and never enters standby (owner, 2026-11-23, TODO 05). displaysleep is 0 today | TODO 05 plan: DPMS off / on plans from the 2026-11-23 Linux capture, run at IOFramebuffer power state 0 / 1 | M |
| 10 | 2560x1440 at 60 / 120 / 144 / 240 Hz | not standard (ROADMAP list) | Works today: CoreGraphics lists ids 3-6 (59.88 / 120 / 144 / 240), current 240 Hz; `debug.phxp0odm_stat` `failed=0`. Each switch replays Linux's captured register writes (`PhxP0Odm.h:1-40`, `phx_p0odm_1440p*.inc`), always through 144 Hz (2-3 link trainings, ~1 s per switch in the daemon log) | Own mode setting: timing, clocks, watermarks, DET, prefetch computed per mode (a DML-equivalent) with own link training; keep the plans as the test oracle | L |
| 11 | Other resolutions of the LG | not standard | Today: 1920x1080 @60 / 120, 1280x1024 @75, 1024x768, 800x600, 640x480 (CoreGraphics probe). Fixed rates, no VRR / HDR, drawn by the CPU copy (GPU present runs only at its start size, `docs/XTX.md`); b138 froze at 1280x1024 (open, TODO 01) | GPU present at every size, then #10 | M |
| 12 | Any other monitor, or this card's other VBIOS builds | missing | The replay engine refuses anything but the GOP's DP B, a known 1440p mode and DMCUB `0x07008742` / `0x07008748` (`PhxP0Odm.h:33-38`, `PhxDpRun.h:152`): another monitor gets one mode (the GOP's), no rate change, no VRR / HDR. Connector table hard-coded (`PhxDdc.h:213-219`); `PhxVbios.h` not wired in | #10; connector table from the VBIOS (TODO 12) | L |
| 13 | Scaled / HiDPI modes ("looks like") | missing | CoreGraphics today: only native-pixel modes, no 2x modes (TODO 01) | Find what Apple's AMD drivers report (scaler info) for a 1440p external display; owner decision pending (OWNER-TESTS) | M |
| 14 | Variable refresh (Adaptive Sync 60-240) | not standard | Offered today (`debug.phxfb0_vrr 1`, floor 60). Only at 2560x1440, because the frame trigger comes from GPU present; the floor is written into the EDID macOS sees, so a floor change makes a new display identity (`docs/XTX.md`); WindowServer's VBL is a kernel timer at the measured rate, not the DCN's vblank interrupt (`phxfb.cpp:1-7`) | Real OTG vblank interrupt; VRR at every mode after #10 | M |
| 15 | EDID / display identity / physical size | works (fallback risk) | Live EDID today (`phx-edid-live Yes`, `phx-edid-src 0x22` = DP B), 591 x 332 mm, LG vendor / model in CoreGraphics. If the live read fails, PhxAMDFB serves upstream's stored Acer EDID for whatever is attached (`phxfb.cpp:991-1023`) | On a failed read, retry and then report no EDID (as macOS does) instead of another monitor's | S |
| 16 | HDR | broken (off as a workaround) | `~/gpu/phxboot-nohdr` since 2026-11-24 00:59 (daemon log today: `hdr: off`). After a DP / HDMI replug at 00:08 every HDR session froze its picture a few minutes after login (input still worked): WindowServer composited BGRA while the scan-out was 10-bit, the plugin's CPU widen path presented stale slots (finding `2026-11-24-hdr-freeze-after-replug.md`, branch `ownback/dp-hdmi-audio`). The HDR mode sets themselves are replays (#10) | Plugin: make the widen path refresh every frame or refuse it; find why WindowServer went BGRA; then remove `phxboot-nohdr` (trust + EFI change) | M |
| 17 | Colour depth in SDR | works (unverified vs a Mac) | 24-bit ARGB8888 today (system_profiler); 10 bpc only with HDR (`phxfb.cpp:844`, `:1111-1112`) | Check what a real Mac with an AMD GPU drives this LG at in SDR (30-bit?) | S |
| 18 | Replug, monitor power button, input switch | broken | DP unplug / replug: LG stays black although the link trains; two mode switches did not help; a restart did (owner, 2026-11-24 00:05, same finding). `debug.phxhotplug0 0` here (flag not set); its design assumes the GOP firmware retrains the link on a replug (`phxhp0.h:4-10`), which does not cover the replayed 240 Hz DSC / ODM state. Power button and input switch: same HPD path (unverified) | On HPD: own detect + link training + stream enable as Linux's DC does, then hp0 tells macOS | L |
| 19 | HDMI | missing | The LG on HDMI stayed black (owner, 2026-11-24 00:05). No XTX plan for HDMI; upstream's HDMI code (DIG3, SCDC) served its own 1080p monitor as a second head | After #10 (own mode set), HDMI 2.1 FRL is its own piece | L |
| 20 | Second monitor, arrangement, mirroring | missing on the XTX (unverified) | Pipe-1 code (`phx_pipe2.cpp`, `m7_boot.sh`) has only upstream's 1080p captures, a HUBP1 aperture sized for 20 GiB (`docs/XTX.md` #4); 240 Hz uses pipe 1 for ODM 2:1, so a second monitor and 240 Hz exclude each other (`PhxP0Odm.h:36`). Never run on this machine | After #10; a pipe allocator instead of fixed pipes | L |
| 21 | Brightness / volume over DDC/CI (MonitorControl, BetterDisplay) | missing | No `IOFramebufferI2CInterface` instance (ioreg instance count 0 today); `AppleMCCSControlModule` is attached to PhxAMDFB with no bus to use; the kext's I2C code allow-lists DDC/CI 0x37 but nothing exposes it (`PhxDdc.h:84-91`). ROADMAP's known limit requires DDC/CI to work | IOFramebuffer I2C (`doI2CRequest`) over DP AUX (I2C-over-AUX) and DC_I2C | M |
| 22 | Night Shift, calibrated profiles (gamma) | broken (unverified by eye) | Head-0 gamma tables are accepted and dropped (`phxfb.cpp:1709`; `debug.phxfb1_gamma_stat head0_n=2` today); WindowServer logged "Attempting to get capabilities from plane with no gamma" 416 times in 100 min (log today). CoreBrightness reports Night Shift supported (probe today), so System Settings offers it | Owner eye test; then head-0 regamma as Linux's DCN 3.2 programs it (hw-safety review), and gamma capability on the display pipe | M |
| 23 | Colour profiles (ColorSync) | works | Owner switches SDR / HDR profiles with `~/gpu/tools/colorprofile.swift` (not in the repo, `docs/XTX.md`); a VRR floor change resets them (#14) | Put the script in the repo or drop it | S |
| 24 | True Tone | works as a Mac (n/a) | MacPro7,1 with a third-party monitor has no True Tone on a real Mac either | none | - |
| 25 | Rotation | unverified | Rotation S1 is a CPU copy that transposes on head 1's display-pipe path (`phxrotate.h:15-20`); GPU present (head 0) does not rotate; `debug.phxfb_rotate` gate 0x3, `sets 0` today | Test on head 0; rotate in GPU present or the DCN | S-M |
| 26 | Screenshots, screen recording | works (partly unverified) | `screencapture` returns the GPU desktop, Screen Recording prompt granted (PROJECT_KNOWLEDGE, 2026-11-23). ScreenCaptureKit / QuickTime recording (unverified) | One QuickTime recording | S |
| 27 | AirPlay display out, Sidecar | unverified | Never tested; Sidecar belongs to 07 | Test once | S |
| 28 | Login window, auto-login, FileVault | not standard | The daemon owns `autoLoginUser` + `/etc/kcpassword` and refuses to arm when a user is logged in (`phxboot-navi31.sh:608, 640`), so FileVault (log-in at preboot) would lose acceleration; FileVault off. Logout / fast user switching (unverified) | Falls out of #2 / #3 | L (with #3) |
| 29 | Panic screen and panic reports | missing | No panic report is saved (`DisableVariableWrite`, TODO 01); with GPU present scanning a VRAM slot, panic text drawn into PhxAMDFB's console surface is likely not visible (unverified) | NVRAM write test as its own EFI change (TODO 01); console fallback to the scan-out on panic | M |
| 30 | Recovery, Safe Mode, the installer | unverified (risk) | `phxfb-squat` stays set in firmware (`nvram -p` today: `phxfb-squat 1`; `~/gpu/phxboot-nvstuck` = `stuck`), and OpenCore injects VermilionBoot into every macOS boot, so Recovery and Safe Mode would squat IOFramebuffer with no daemon to light PhxAMDFB: probably no picture | Owner test with `-phxbringupoff` ready; the kext skips the squat in Recovery / safe boot | S |
| 31 | Boot time | works (slow) | ~67 s from Restart to the accelerated desktop with the 10 s picker (PROJECT_KNOWLEDGE); kernel to desktop ~27 s today (18:30:20 -> 18:30:47); the daemon watches 120 s more before SUCCESS | Follows #1-#3 | - |

## The 5 things a Mac user would notice most

1. **No sleep.** The Mac never sleeps (`disablesleep 1`), and display sleep leaves the LG on showing black instead of
   standby (#8, #9).
2. **Unplugging or power-cycling the monitor, or switching its input, leaves it black until a restart.** HDMI never
   works (#18, #19).
3. **HDR is off.** It froze the picture in every session after a replug, so it is disabled by a flag file (#16).
4. **The boot.** You get a text picker, verbose text, a frozen screen, a WindowServer restart and a refresh switch, about a minute
   in all. After one freeze or forced power-off, every later boot runs a degraded desktop until someone types an `rm`
   in Terminal (#1, #2, #7).
5. **Only this one LG at 2560x1440 is fully supported.** Any other monitor or a second one gets one mode or nothing.
   There are no HiDPI modes and no DDC brightness, and Night Shift most likely does nothing (#12, #13, #20, #21, #22).

## Hard-rule risks seen in the code and on the machine

1. **No warm-GPU check before firmware load** (rule 1). `bringup_navi31.sh:64-80`: if the PSP's sOS is already alive
   (`C2PMSG_81 != 0`), it only destroys the ring and goes on to load the SMU firmware into what may be a running SMU,
   which the script's own comment says Linux never does. Only the power-off procedure enforces the cold-GPU rule. Fix:
   refuse (GOP boot) when PSP / SMU report running firmware. S.
2. **A shipped sysctl terminates a driver on the GPU's PCI function** (rule 1, last bullet).
   `debug.phxfb_kill_iondrv` (`nvrm-accel.cpp:1612-1634`, registered at `:1904`, live today) terminates
   IONDRVFramebuffer on a root write of 1. No script uses it. Remove it.
3. **The GOP-revert fallback cannot run** (rule 1, "keep the fallbacks"). With `DisableVariableWrite`, `nvram -d
   phxfb-squat` never reaches the firmware, so every boot squats. `m2_fail` and `watch_hangs` fall back to the safe
   desktop and then "headless" (dark screen, network only) instead of the firmware display
   (`phxboot-navi31.sh:129-135, 273-300`). Fix: the kext decides the squat itself (for example from a file the daemon
   writes, or "no squat after an unfinished boot"), not from an NVRAM one-shot.
4. **Reset NVRAM is offered and recommended, against rule 1.** The picker has `ResetNvramEntry.efi`
   (`efi/machines/b650e-i-xtx.json:65`). `docs/XTX.md` and the daemon's own log line ("Reset NVRAM in the OpenCore
   picker clears it") tell the user to use it. It deletes `phxfb-squat`, after which macOS can't set it again
   (DisableVariableWrite) and every boot stays on the GOP. Fix: hide the entry in the release profile and fix the
   messages.
5. **Detect, don't overwrite** (rule 3). The connector table is hard-coded (`PhxDdc.h:213-219`). A failed live EDID
   read serves another monitor's EDID (`phxfb.cpp:991-1023`). The replay plans are this monitor's captures, but they
   are gated and refuse anything else, so they don't overwrite.
6. **Not a hard rule, but root access.** The root LaunchDaemon runs `~/gpu/phxboot-navi31.sh` and `~/gpu/phxboot-pre.sh`,
   which belong to the desktop user (`<user> staff`, ls today). Any process of that user can get root and program the
   GPU through the debug selectors (TODO 06: daemon into a system path).
7. **Known-good drift** (rule 2). The machine runs the audio branch's daemon and EFI change 33. Reinstalling the daemon from
   main would drop the auto-login fix (the branch's TODO says never do that). Merge `ownback/dp-hdmi-audio` first (phase 1).
