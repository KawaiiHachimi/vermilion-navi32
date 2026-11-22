# macOS integration and peripherals: first inventory (thread 06)

2026-11-23 00:30-00:40, live XTX machine (macOS 26.7.1 25G241), 11-15 min after boot. Installed state as in
[PROJECT_KNOWLEDGE.md](../../docs/PROJECT_KNOWLEDGE.md) "Known-good configuration" (stage `vermilion-trust`,
VermilionBoot b150, RADV ABI 1.12). Display 2560x1440 @ 240 Hz, 10-bit framebuffer (ARGB2101010). Safari and T3 Code
were open. Read-only probes; nothing was changed on the machine except one TCC grant by the owner (below).

## Summary

| Area | State | Owner of a fix |
|---|---|---|
| Keyboard, mouse | work (USB). Mouse polls at 1 kHz (`ReportInterval` 1000 us), keyboard at 125 Hz (8000 us) | — |
| Screenshots / screen capture | work: `screencapture` returns the real 2560x1440 GPU-presented desktop, not black, once the app has Screen Recording permission | — |
| TCC prompts | work (T3 Code got a Screen Recording prompt; the owner granted it) | — |
| Spotlight | indexing on, queries answer (`mdfind -name` 67 ms) | — |
| Stage Manager, Dark mode | on (owner's settings); window management not exercised yet | — |
| Display scaling (HiDPI) | **no HiDPI / scaled modes offered at all**; the display reports **903 x 508 mm** (a 40.7" panel; the LG 27GS95QE-B is about 597 x 336 mm) | 06 (override), 00 (reported size) |
| Hardware video decode | **none for any codec** (VideoToolbox: H.264, HEVC, AV1, ProRes, VP9 all `false`): HEIC images, Quick Look, Photos and video wallpapers decode on the CPU | 00.1 (VCN) |
| Quick Look thumbnail | works but slow: a 512 px thumbnail of a 6016x6016 HEIC wallpaper took **12.2 s (11.4 s CPU)**; `sips -Z 512` of the same file 1.6 s | 00.1 (decode); 06 to re-measure |
| Virtualization | **`kern.hv_support: 0`**: Hypervisor.framework and Virtualization.framework don't work | limit (see below) |
| Xcode / simulators | Command Line Tools only; no Xcode, no simulators | owner decision (install) |
| File Sharing (SMB) | off (no `smbd` running); a Public share point is configured with guest access | 06, with 05 for the security side |
| Remote Login (SSH), Screen Sharing | off | 06, with 05 |
| AirPlay Receiver | on (listening on 5000 / 7000); iPhone -> Mac works (thread 04) | — |
| Printers / scanners | none set up (`lpstat`: no destinations); untested | owner (has one?) |
| Webcam / microphone | no webcam attached; the only input is the ALC4080 USB audio; Continuity Camera over USB gives a black picture (thread 04 handoff) | 04 / 06 |
| Crash reports (7 days) | no recurring crash in a system app now (details below) | — |

## Details

### Input devices

- `Hama CK-200` keyboard (USB, `0x1c4f:0x0151`) on `XHC0`; `HID Gaming Mouse` (`0x1bcf:0x08b8`) on `XHC1`. Both
  work. The ALC4080 also shows up as a vendor HID device (`0x0b05:0x1a5c`).
- User settings: pointer acceleration off (`com.apple.mouse.linear = 1`, scaling 0.875), key repeat at defaults,
  hot corner bottom-right = Quick Note.
- Input latency end to end is not measured yet. Thread 00.1's TODO has "input lag at 60 Hz".

### Display scaling

`CGDisplayCopyAllDisplayModes` with duplicate low-resolution modes lists only 1:1 modes: 2560x1440, 1920x1080,
1280x1024, 1024x768, 800x600, 640x480. None is HiDPI (`pixelWidth == width` for every one). System Settings shows
"UI looks like 2560 x 1440".

On a real Mac the usual way to get "looks like 1920x1080" sharp text on a 1440p panel is a scaled HiDPI mode: render
at 3840x2160 and downscale. macOS offers that only for displays it considers high-density, or with a display
override plist (`scale-resolutions`) in `/Library/Displays/Contents/Resources/Overrides/` *(unverified on Tahoe
26.7 and on Vermilion)*. Questions before trying it:

- Can PhxAMDFB allocate and scan out a 3840x2160 backing buffer, and what does it cost in GPU time and power at
  240 Hz? That is thread 00 / 00.1's call.
- The reported physical size (903 x 508 mm) is wrong for a 27" panel. Where it comes from (EDID, or a value in the
  driver) is open; it affects the DPI macOS computes. Thread 00 to check.

The owner hasn't asked for scaling yet. Whether they want it is a question for them.

### Media decode and image thumbnails

VideoToolbox reports no hardware decoder for any codec. That is the known "VCN off" state in thread 00.1's
baseline. For this workstream it means HEIC photos and wallpapers, Quick Look previews, Photos and video wallpapers
all fall back to software decode. Measured:

- `qlmanage -t -s 512` of `Mac Blue.heic` (6016x6016, 21-25 MB, from `/System/Library/Desktop Pictures`):
  real 12.20 s, user 11.37 s.
- `sips -Z 512 -s format png` of the same file: real 1.60 s, user 1.11 s. A full-size decode to PNG: 3.18 s.

Quick Look is about 8x slower than `sips` for the same thumbnail. Possible reasons: it decodes every image in the
multi-image dynamic-wallpaper file, or it takes a slower path. Not investigated yet.

Any command-line tool that touches Metal (here through VideoToolbox) prints RADV's init line and two `Failed to
create /var/folders/zz/.../C/nvmtl-mesa for shader cache (Permission denied)` lines to stderr. The cache path is the
`zz` (system) cache folder, not the user's. This is the known "apps get no Mesa shader disk cache" item (00.1). It
also adds noise to the stderr of every Metal-using CLI tool.

### Virtualization and developer workflows

`kern.hv_support` is 0. macOS's hypervisor supports only Intel VT-x, not AMD SVM, so on this Ryzen nothing that
needs Hypervisor.framework or Virtualization.framework can run: Docker Desktop, UTM / QEMU with HVF, Parallels,
VMware Fusion, Tart / Lima / Colima with the vz backend, the Android emulator's HVF backend. This is a platform
limit, not a configuration problem. No kext provides SVM support to macOS *(as far as known; unverified)*. What
still works:

- QEMU (UTM) with TCG emulation: correct but slow.
- Containers on a Linux host over SSH (`docker context` / `DOCKER_HOST=ssh://...`), e.g. the dev box.
- The iOS / watchOS / tvOS simulators don't use the hypervisor on Intel-architecture Macs. They should run once
  Xcode is installed *(untested)*. They draw with Metal, so they also exercise Vermilion.

Xcode is not installed (only `/Library/Developer/CommandLineTools`). It is about a 3 GB download plus simulator
runtimes, so the owner decides.

### Sharing and remote access

- File Sharing: off. `sharing -l` shows the Public folder configured as an SMB share with guest access, but `smbd`
  isn't running.
- Remote Login (SSH): off (`systemsetup -getremotelogin`). Screen Sharing / Remote Management: off.
- Listening TCP ports: 5000 and 7000 (AirPlay Receiver), 49155, 61036 / 61037, and two on localhost.
- Turning any of these on is a security decision (thread 05's scope too), so it waits for the owner. Screen
  Sharing is the one to test first: it relies on WindowServer capture, which works (see screenshots).

### Crash and hang reports, last 7 days

None of these is a current, recurring system-app crash:

- WindowServer x4 (2026-11-18 19:10, `CA::OGL::new_metal_context` abort): the bring-up day, before the plugin worked.
- Safari `ExcUserFault` x2 (2026-11-19, 11-20): WebKit `didReceiveInvalidMessage` (EXC_GUARD, logged, not a crash
  of the app). Possibly linked to the GPU process not mapping the plugin (known issue) *(unverified)*.
- Music x2 + 2 hangs (2026-11-22): the plugin crash thread 05 fixed.
- UserNotificationCenter (2026-11-22 17:57): killed for an invalid code signature while dyld loaded a library.
  That was before the AMFI change at 23:45. Not seen again *(watch it; thread 05)*.
- siriinferenced x2, BiomeAgent x1: Swift assertions inside Apple's frameworks (JSON decoding, GRDB).
  Likely not hardware related *(unverified)*.
- prismlauncher (2026-11-19): an Objective-C autorelease-pool abort in a third-party Minecraft launcher.
- `mutbin` x4 (2026-11-20): a test binary, not part of the system.
- 3 JetsamEvents (memory pressure kills); not looked at.

### Background load at the time

`top -l 2 -s 20`, 13 min after boot, Safari open with active WebKit content: WindowServer 39.9 % of a core,
two WebKit processes 23-24 % each, T3 Code 19 + 11 %, kernel_task 2.9 %. Not an idle baseline (thread 00.1's idle
figure is WindowServer 12-13 %). An idle measurement for this workstream is still to do.

## Next steps (proposed, need the owner)

1. Measure the desktop: app launch times (Finder, System Settings, Safari, Preview), Mission Control / Stage
   Manager animation smoothness, input latency, an idle CPU baseline.
2. Decide on HiDPI scaling (do they want "looks like 1920x1080"?) and, if yes, plan it with thread 00.
3. Turn on and test Screen Sharing, SSH and SMB one at a time, with thread 05's security view.
4. Install Xcode if the owner wants iOS development; test a simulator (Metal on Vermilion).
5. Continuity Camera: tested together with thread 04's AWDL test boot.
6. Printers / scanners: only if the owner has one to test.
