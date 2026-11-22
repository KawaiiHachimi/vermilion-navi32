# Sleep, power and validation: first inventory (thread 07, 2026-11-23)

Read-only survey of the XTX machine at the start of thread 07 (sleep / wake, power and thermals, stability,
regression, updates). Nothing on the machine was changed. Two read-only tools were added:
`tools/sensors/cpuclk.c` (effective core clock) and `tools/validation/bootcheck.sh` (one report per boot).

**Conditions:** 2026-11-23 00:39-00:52, 19-32 min after a normal restart (boot 00:20:36). macOS 26.7.1 25G241,
EFI stage `vermilion-trust` (VermilionBoot b150, VermilionTrust 1.0.3 = EFI change 8), PhxAccel id 18, daemon flags
`autologin cg p0modes sdma uclk vrrmin waitstep wscdcopy`. Display 2560x1440 240 Hz, 30-bit. Running: T3 Code with
this Claude session (load average 1.7-3.4), Safari closed.

## Summary

| Area | State | Verified |
|---|---|---|
| Idle sleep, display sleep, hibernation | off (`pmset` sleep 0, displaysleep 0, hibernatemode 0, disksleep 0) | yes |
| **Manual sleep** (Apple menu > Sleep, power button) | **still possible**: `SleepDisabled = No`, `IOSleepSupported = Yes`, `Sleep On Power Button 1`. A sleep would most likely lose the GPU (no resume path) | settings yes, the outcome *(unverified)* |
| Sleep count this boot | 0 sleeps, 0 dark wakes | yes |
| ACPI sleep support | firmware DSDT has `_S0` `_S3` `_S4` `_S5`, `_PTS`, `_WAK`; 73 `_PRW` wake packages plus one `_PRW` method (packages: 67 on GPE `0x02`, 3 on `0x19`, 2 on `0x08`, 1 on `0x1A`); FADT: RTC wake from S4, not "Low Power S0 Idle" | tables yes, S3 on this machine untested |
| GPU power management in Vermilion | `PhxAMDFB::setPowerState` only acknowledges and posts a framebuffer event; `kIOPowerAttribute` (display power) is passed to IOFramebuffer; PhxBringup's IH ring turns itself off at restart / power-off. Nothing saves or restores the GPU | code |
| Shutdowns | the last 15 boots in the log all report previous shutdown cause **5** (normal); no panic report, no ESP `panic-*.txt` | yes |
| Shutdown stalls | a `shutdownStall` report at almost every restart (65 since 2026-11-18). The stall is ~2-3 s of normal teardown, no Vermilion code blocks it | yes |
| NVMe after restarts | SMART Unsafe Shutdowns still **212** (as on 2026-11-22 21:07, at least 6 restarts later): macOS shuts the drive down cleanly | yes |
| CPU clocks | 1-12 threads ~5.0-5.15 GHz, 24 threads ~4.8 GHz; **never above 5.15 GHz**, also on the frequency CCD (rated 5.6 GHz) | yes |
| CPU idle | package 40-50 W, Tctl 55-62 C at the desktop with this session running | yes |
| CPU deep idle | firmware `_CST` offers C1 (FFixedHW), C2 (I/O read of port `0x414`, 18 us) and C3 (port `0x415`, 350 us); macOS with `DummyPowerManagement` uses none of them | tables yes, the effect *(unverified)* |
| GPU idle | 63 W board / 50 W socket, gfxclk avg ~170 MHz, hotspot 46 C, memory 64 C, fan 0 % (matches the 2026-11-22 baseline) | yes |
| Fans | GPU fan via SMU metrics; **no motherboard / CPU fan readings** (no Super I/O sensor kext; SMC has only the CPU keys) | yes |
| Hardware errors | `AppleMCEReporterDisabler` is needed on AMD, so macOS reports no machine-check errors | config |
| Recovery | APFS Recovery volume present (1.5 GB); booting it untested | volume yes |
| Restart time | click Restart 00:19:44 -> shutdown 00:19:47 -> kernel 00:20:36 -> daemon 00:20:39 -> accelerated WindowServer 00:20:51: **~67 s**, of which ~45 s is firmware POST + the OpenCore picker | yes (one restart) |

## Sleep

- `pmset -g` (live): `sleep 0 (sleep prevented by runningboardd, mds, caffeinate)`, `displaysleep 0`, `disksleep 0`,
  `hibernatemode 0`, `Sleep On Power Button 1`. `pmset -g cap` offers only displaysleep, disksleep, sleep,
  ttyskeepawake, hibernatemode, hibernatefile on AC: no `womp` (wake on LAN), `powernap`, `standby`, `tcpkeepalive`.
- `IOPMrootDomain`: `SleepDisabled = No`, `IOSleepSupported = Yes`, `System Capabilities = 15`, `Hibernate Mode 0`.
  `sleep 0` stops only idle sleep. **Apple menu > Sleep and a short press of the power button still start a system
  sleep.** Nothing in the stack can resume the GPU: Vermilion has no resume path (ARCHITECTURE.md "No sleep"),
  and the bring-up assumes a cold GPU loaded once. The most likely result is a black screen or a hang after wake
  *(unverified)*. `PowerTimeoutKernelPanic` is on in the EFI, so a driver that misses a power-change acknowledge
  doesn't panic.
- What Vermilion does today (`src/vermilion-accel/accel/phxfb.cpp`): two IOPM power states, `setPowerState` posts
  `kIOFBNotifyWillPowerOff` / `DidPowerOn` and returns `kIOPMAckImplied` (an old 45 s acknowledge wait froze
  WindowServer restarts). `setAttribute(kIOPowerAttribute)` (display power) goes to IOFramebuffer and posts the same
  events. `src/vermilion-boot/phx_ih_irq.cpp` registers `registerPrioritySleepWakeInterest` only to switch the IH
  ring off at `kIOMessageSystemWillRestart` / `WillPowerOff`. No `kIOMessageSystemWillSleep` handling anywhere.
- Display sleep (DPMS only: the system stays up, the monitor goes dark) is the cheaper first step. It has never been
  tried on this machine. The risk is a black screen that needs a restart: a failed DP link training at a mode switch
  was seen once (2026-11-22 20:19, PROJECT_KNOWLEDGE problem index).

## Shutdowns, panics, stalls

- Shutdown causes from `log show --predicate 'eventMessage CONTAINS "Previous shutdown cause"'`: 15 boots in the log
  archive (2026-11-22 17:26 to 2026-11-23 00:20), all cause 5. Older boots have rotated out of the log.
- No `.panic` report in `/Library/Logs/DiagnosticReports`, no `panic-*.txt` on the ESP. With `DisableVariableWrite`
  the kernel can't leave its panic log in NVRAM, so OpenCore's `ApplePanic` has nothing to save: **a panic is only
  visible on screen** (`-v`, `debug=0x100`). The panic at a restart on 2026-11-19 stays unexplained.
- Reports on disk: 65 `shutdownStall`, 3 `JetsamEvent`, 2 `Music.hang`, 4 `WindowServer` crashes (2026-11-18, before
  the current stack).
- The 00:19:52 stall (`spindump -i` of the binary report): "sampling started after 2 seconds", 1 s of samples.
  `launchd` had sent `System shutdown initiated` at 00:19:46.98. Left alive: WindowServer (in `CGXShutdownDisplays`,
  then a zombie), `amsaccountsd`, `duetexpertd`, `PowerUIAgent`, the DriverKit E1000 and DockChannel drivers, the HID
  drivers. In 8 other reports the same set appears; the boot daemon (`bash` + `sleep`) is sometimes still there, but
  its log shows it handled the TERM at once (00:19:47). Vermilion's kernel threads (`NMXAccel`, `NMXDisplayPipe`)
  were idle. Cost: ~2-3 s per restart. Not a hang; low priority.

## CPU power management

- Clock probe `tools/sensors/cpuclk.c` (dependent adds, 0.5 s windows):

  | Threads | GHz min / median / max | Note |
  |---|---|---|
  | 1 | 4.97 / 5.02 / 5.12 | |
  | 2, affinity tags 1 / 2 | 4.97 / 5.12 / 5.14 | xnu puts the tags on different last-level caches |
  | 4, tags 1 / 2 | per-thread medians 5.05 5.07 / 5.13 5.14 | one CCD ~0.07 GHz faster |
  | 12, tags 1 / 2 | 4.91-4.94 / 5.03-5.07 | |
  | 24 | 3.72 / 4.77 / 5.02 | Tctl 65 C, ~78 W package (one ALU per thread) |

  macOS sees two L3 domains of 12 CPUs each (`hw.cacheconfig`; `hw.cachesize` reports 96 MiB for both). The tags
  split the threads into two groups with a steady 0.07-0.13 GHz difference, so they do land on different CCDs.
  **No window on either CCD exceeded 5.15 GHz.** AMD rates the 7900X3D's frequency CCD at up to 5.6 GHz.
- Likely cause *(unverified)*: with `DummyPowerManagement` the kernel's idle loop has no CPU power-management driver
  and idles with `HLT` (C1). The firmware's deeper states are I/O-port reads (`_CST` C2 = port `0x414`, C3 = `0x415`),
  which Linux's `acpi_idle` uses to put cores into CC6 and macOS never does. Precision Boost needs idle cores in CC6 for
  its highest single-core clocks, and C1-only idle also keeps package power high (40-50 W measured here).
- **To confirm on Linux** (same machine, needs a Linux boot by the owner): `cpuclk 1 5` pinned with `taskset` to one
  core of each CCD, `cpuclk 24 15`, and idle package power (`turbostat` or the RAPL energy counter) at the desktop.
  If Linux reaches ~5.5 GHz and idles 15-25 W lower, the gap is the missing C-states.

## Thermals and fans

- GPU (`~/gpu/smumetrics.py 3 2`, 00:47): board 63-72 W, socket 50-57 W, gfxclk avg 166-399 MHz, edge 37-40 C,
  hotspot 46-49 C, memory 64-67 C, fan 0-12 %.
- CPU (`cpusensors.py -n 10 -i 1`, 00:44): Tctl 54.6-56.0 C, package 39.8-49.0 W (median 47.0).
- No motherboard fan or chassis temperature keys in the SMC (`smcread -s`: `PCPR PCPT PCTR TC0E TC0F TC0G TC0J TC0P
  TC0T TC0p`). The CPU cooler's fan speed can't be read from macOS. A read-only Super I/O plugin (VirtualSMC's
  `SMCSuperIO`) would need the board's chip to be supported *(unverified)*: an EFI change (thread 02).

## Recovery and updates

- APFS: System sealed (snapshot sealed), Data, Preboot, Recovery (1.5 GB), VM. OpenCore shows auxiliary entries
  (`HideAuxiliary false`), so Recovery should be in the picker *(unverified)*.
- The update policy (pinned 26.7.1, `BlacklistAppleUpdate`, Rapid Security Responses off, VermilionTrust on an AMFI
  change) is thread 05's TODO items 7-8. Thread 07 tests what they decide: Recovery boot, and an update dry run.

## Proposals

In order. 2026-11-23 00:58: proposal 1 done (owner's OK via the coordination thread, verified
`SleepDisabled = Yes`); 2-5 deferred to the new thread series (TODO "Sleep, power and validation").

1. **Block manual sleep until the GPU can resume:** `sudo pmset -a disablesleep 1`. Apple menu > Sleep and the power
   button then do nothing. No reboot. Undo: `sudo pmset -a disablesleep 0`. Check: `bootcheck.sh` shows
   `SleepDisabled Yes`.
2. **Display sleep test** (owner present, thread 00 told): `pmset displaysleepnow`, wake with a key, then read
   `smumetrics.py` while the monitor is dark. If it comes back every time: `displaysleep 10`, which also saves OLED
   wear. If the image doesn't come back: Apple menu > Restart.
3. **Linux reference runs** (the owner boots Linux; back to macOS = full power-off): the CPU clock and idle power
   above, and thread 00.1's GPU idle power. Decides whether CPU deep idle is worth a project.
4. **Stress and soak** at a time the owner picks (hot and loud, but the machine stays usable): 30 min all-thread CPU,
   30 min `metalbench`, both together (PSU and case heat), a `memtester` pass (MacPorts), 8 h idle soak at the desktop
   (fine while the owner is away: no reboot). `bootcheck.sh` and the sensor logs before and after.
5. **Platform S3 test without the GPU driver**: one boot with `-phxbringupoff` (GOP desktop, no acceleration), sleep
   from the Apple menu, wake by keyboard, mouse, power button, then check USB, NVMe, Wi-Fi, Bluetooth, audio. It shows
   whether the board, ACPI and the other drivers sleep and wake (instant wakes from `_PRW` GPE `0x02` are the usual
   AM5 problem), before anyone writes a GPU resume path. One EFI change, rollback written first.
6. **GPU resume path** (thread 00 with 07): on `kIOMessageSystemWillSleep` stop the rings and the display, after wake
   replay the cold bring-up. Large; only after 5.
7. **CPU deep idle** (research, only if 3 shows a large gap): a kext that gives xnu's idle loop the firmware's `_CST`
   I/O-port C-states, as Linux `acpi_idle` does. Risky (the idle path of every core).
8. **Fan readings**: `SMCSuperIO` if the board's Super I/O chip is supported (thread 02, one EFI change).
