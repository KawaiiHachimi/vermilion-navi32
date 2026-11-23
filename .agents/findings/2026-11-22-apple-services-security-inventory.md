# 2026-11-22: Apple services, DRM and security, inventory (thread 05)

> **Correction (2026-11-23, thread 00, from thread 05's final report):** "`amfi=0x80` -> no FairPlay" is unproven:
> Apple Music failed because `en0` wasn't built-in and plays since that was fixed; Apple TV fails on the missing
> protected decoder, without `amfi=0x80` too. The Music crash cause noted at 00:15 ("no height / allocation check")
> is wrong; the 00:40 update is right (the destination memory was unmapped). The TODO claim that a changed AMFI layout
> makes VermilionTrust refuse cleanly ("no acceleration, no crash") is probably wrong: its check can't detect shifted
> slots, so a 26.x update most likely panics or wraps the wrong hook (thread 09). "Privacy prompts work again" was
> inferred from the `csops` flag; only the Screen Recording prompt was seen since (thread 06).

Read-only inventory of the live XTX machine, 35-40 min after the 22:23 boot (macOS 26.7.1 25G241, EFI `vermilion`
stage, VermilionBoot b150, boot-args `-v keepsyms=1 debug=0x100 pci=0x8000000 amtk_awdl_offchan=0 amfi=0x80
ipc_control_port_options=0 phxdebug=1`, SIP `0x0A03`, 1440p desktop). Nothing was changed. Sources: `ioreg`,
`nvram`, `csrutil`, `spctl`, `fdesetup`, `sysadminctl`, `codesign`, `softwareupdate`, `xprotect`, unified log
(`/usr/bin/log show --last 40m`; in zsh, plain `log` is a shell builtin), the user's `~/Library/Preferences/com.apple.*`
plists, and the crash reports. No serial, MLB, ROM, UUID or MAC is written here.

## Summary

| Area | State | Verdict |
|---|---|---|
| SMBIOS / PlatformInfo | MacPro7,1, board-id `Mac-27AD2F918AE68F61`; serial 12 chars, MLB 17 chars (macserial); ROM = the I225-V's MAC; `UpdateSMBIOSMode = Create` | well-formed |
| ROM / MLB in NVRAM | both present (OpenCore sets them; `DisableVariableWrite` doesn't stop that) | OK |
| en0 | **the iPhone's USB Ethernet** (`AppleUSBEthernet`, not built-in). Built-in: Ethernet `en3`, Wi-Fi `en4`. en1, en2, en5 and en6 are iPhone / iPad NCM interfaces | off-spec, but iServices work anyway (below) |
| Apple Push (APNs) | connected ("Connected on 1 interfaces", presence responses) | works |
| iMessage / FaceTime | IDS registration answered HTTP 200, delivery responses code 0, messages sent; `com.apple.imservice.ids.iMessage` and `.FaceTime`: 1 active + 1 online account each. Only the `Calling` service (iPhone call relay) "not able to register", which is normal without a phone-number handle | registered *(the owner hasn't confirmed a sent / received message yet)* |
| iCloud | signed in; Messages in iCloud enabled, not paused; iCloud Drive (`bird`), CloudKit running; "Keychain Circle Notification" running (iCloud Keychain may be waiting for approval on another device) | works *(Photos, Keychain sync, Drive sync not tested by hand)* |
| FairPlay | `fairplayd`, `fairplaydeviceidentityd`, `adid` run; `adid` logs 8 errors in 40 min | see DRM below |
| SIP | custom: Kext Signing, Filesystem Protections off; Debugging, DTrace, NVRAM, BaseSystem verification on; Authenticated Root disabled (allowed), but the system volume is sealed and booted from its snapshot (thread 03) | as designed |
| AMFI | `amfi=0x80` + AMFIPass 1.4.1. `amfid` running | as designed; costs below |
| Gatekeeper | `spctl`: assessments enabled. XProtect 5287 (installed 2026-11-19, the macOS install), **launch scans disabled**, background scans enabled | partly reduced |
| Security data updates | `/Library/Preferences/com.apple.SoftwareUpdate`: `AutomaticDownload = 0`, `AutomaticallyInstallMacOSUpdates = 0`, **`ConfigDataInstall = 0`, `CriticalUpdateInstall = 0`**, `SplatEnabled = 1`; automatic checking on. Pending: Safari 27.2 | XProtect / MRT data never updated since the install |
| Code signing | NVMTLDriver.bundle and VermilionAccel.kext ad-hoc signed (no team); AuxKC holds `org.vermilion.accel`; no system extensions | as designed |
| TCC | Apple apps and services get answers normally. `Platform binary prompting is 'Deny' because: is Platform Binary` for system settings extensions (`kTCCServiceListenEvent`) | third-party prompts not tested yet (upstream: denied) |
| FileVault | off. The login user has a Secure Token (the only crypto user of the Data volume) | off |
| Automatic login | the boot daemon's `autologin` flag: `/etc/kcpassword` exists, `autoLoginUser` set by the daemon | a trade-off, below |
| OpenCore security | `SecureBootModel = Disabled`, `Vault = Optional`, `DmgLoading = Signed`, `BlacklistAppleUpdate = true`, `ScanPolicy = 0` | as designed |

## Findings

### iServices work with en0 = iPhone

Dortania's iServices guide wants `en0` to be a built-in interface. Here `en0` is the owner's iPhone USB tethering
interface, because the iPhone and iPad were on USB when `NetworkInterfaces.plist` was first written, and the I225-V's
DriverKit driver attaches late (`en3`). Registration still succeeded at 22:23. ROM is the I225-V's MAC, not
en0's. Whether Apple's validation data also uses en0's MAC is unclear *(unverified)*. If it does, a boot without the iPhone plugged in could look like a different machine.

Fix (proposed, not done): with the iPhone and iPad unplugged, move `NetworkInterfaces.plist` aside (keep a copy)
and reboot. Ethernet and Wi-Fi then get en0 / en1. **Do not** delete `preferences.plist`, which the guide also
lists: it holds the network services and thread 04's Wi-Fi setup. Cost: the interfaces are renumbered, and IDS may
register once more as the "same" Mac. Do it only if the owner wants it; nothing is broken now.

### DRM: the hardware decoder blocks it before AMFI does

- **FairPlay 2.x-4.x** (Apple TV app, TV+ / Netflix / Prime in Safari) needs a hardware video decoder (AppleGVA on a
  supported AMD GPU or the T2 path; Dortania DRM guide). Vermilion has **no VCN decode** (PROJECT_KNOWLEDGE, Machine
  state), and WhateverGreen's `shikigva` / `unfairgva` don't apply (no WhateverGreen, no AMD Apple driver). So
  protected Safari / TV video can't work now **whatever AMFI does**. The upstream note "FairPlay lost with
  `amfi=0x80`" can't be separated from that until there is a decoder.
- **FairPlay 1.x** (Apple Music streaming, purchased audio) is software DRM and should work *(unverified, next test)*.
- **Widevine** (Chrome, Firefox) uses the browser's own CDM and software decode, with no setup (Dortania). No
  third-party browser is installed, so it's untested.

### Music.app crashes in the Metal plugin (owner: thread 00 / 00.1)

`~/Library/Logs/DiagnosticReports` holds 25 Music 1.6.6 crashes from 2026-11-22 (20:47-22:21, bursts of 5-11 within
a minute), all identical:

```
EXC_BAD_ACCESS (SIGSEGV) KERN_INVALID_ADDRESS
  libsystem_platform  _platform_memmove
  NVMTLDriver         nvmtl_vk_image_read
  NVMTLDriver         -[NVMTLTexture nvmtlSurfaceOutDirtied:]
  NVMTLDriver         -[NVMTLCommandBuffer(EventSubmission) nvmtlAdvancePart]
  NVMTLDriver         __nvmtl_pump_buffers_block_invoke_2
```

A texture readback copies past the end of its mapping. It is a GPU-side bug, not DRM: thread 05 doesn't touch the
plugin (cross-thread rule 3). Two Music `.hang` reports (20:33, 22:25) sit in `/Library/Logs/DiagnosticReports`.

### Security data never updates

`ConfigDataInstall` and `CriticalUpdateInstall` are 0 (System Settings > General > Software Update > Automatic
Updates: "Install Security Responses and system files" off). So XProtect has stayed at 5287 since the install, and
nothing replaces it. Keeping macOS itself pinned to 26.7.1 is required (the plugin uses that build's private Metal
interfaces). XProtect data, however, lives outside the sealed system volume and doesn't change Metal.

Proposed: run `sudo xprotect update` by hand (Sequoia+ command) after a check of what it downloads, then decide
whether `ConfigDataInstall = 1` alone is safe. "Install Security Responses and system files" in the UI also turns on
`CriticalUpdateInstall`, and with it Rapid Security Responses / Background Security Improvements (`SplatEnabled`),
which change system frameworks and the build letter. That stays off until thread 00 says a framework change is safe
*(which key gates the Background Security Improvements on 26 is unverified)*.

### Costs and trade-offs that stay

- `amfi=0x80` turns library validation off for every process. Upstream saw third-party privacy prompts (camera,
  microphone) denied without a prompt. Whether AMFIPass alone can carry the plugin without `amfi=0x80` is open:
  OCLP describes AMFIPass as a way to keep AMFI on. Testing it means a boot where WindowServer may not load the
  plugin. That is GPU-side (00), needs the owner, and is one EFI change with the GOP fallback. Not proposed yet.
- `SecureBootModel = Disabled` + `Vault = Optional`: anyone with write access to the ESP (Linux root included, the ESP
  is shared) can change OpenCore's config or kexts. Vaulting would need a re-sign after every EFI build *(build.py
  doesn't do it)*.
- Automatic login keeps the login password in `/etc/kcpassword` (reversibly obfuscated), and FileVault is off, so
  anyone with the disk (Linux included) can read the Data volume and recover that password. This is the owner's
  choice for the accelerated login (daemon flag `autologin`). **FileVault and automatic login exclude each other**
  in macOS: turning FileVault on means a password login at the FileVault prompt and the end of the daemon's
  `autologin` path (thread 00 owns the daemon).
- SIP `0x0A03` includes `CSR_ALLOW_UNAUTHENTICATED_ROOT` (0x800), which INSTALL.md says only the optional video
  decoder needs, and the decoder is off. Dropping it to `0x0203` is a possible hardening step *(unverified that nothing
  else needs it; one boot)*.

## Performance and reliability baseline

| What | Result |
|---|---|
| CPU used by security and service daemons since boot (39 min): `trustd` 9.7 s, `cloudd` 7.1, `tccd` 6.0, `identityservicesd` 2.7, `imagent` 1.8, `apsd` 1.5, `itunescloudd` 1.2, rest < 1 s each (`ps -axo time,comm`) | ~37 s in total, ~1.6 % of one thread on average: not a bottleneck |
| Log volume, 40 min: `tccd` 49.5 k lines, `apsd` 21 k, `identityservicesd` 14.7 k, `imagent` 9 k | high but normal for a fresh login, no error loop |
| Crash loops | Music (Metal plugin, above); no crash of any security or iServices daemon |
| `shutdown_stall` reports | 60 in `/Library/Logs/DiagnosticReports` (not thread 05's: 07 / 00) |

## Next tests (need the owner)

1. Send and receive an iMessage, place a FaceTime call (audio, then video: the video depends on the GPU path).
2. Play an Apple Music track (FairPlay 1.x) while watching for a Music crash.
3. Install Firefox or Chrome and play a Widevine test stream (e.g. a public DRM demo player): Widevine L3, software
   decode.
4. A third-party app that asks for the microphone or camera: does a prompt appear?
5. iCloud Keychain: approve the Mac on another device if the circle notification is pending; then Photos and
   iCloud Drive sync.

## Update 23:11: first changes, and why AMFIPass can't replace `amfi=0x80`

- `sudo xprotect update`: "Activated update CloudKitUpdate[v5377]"; `xprotect version` 5287 -> 5377. Then
  `ConfigDataInstall = 1` in `/Library/Preferences/com.apple.SoftwareUpdate` (XProtect / Gatekeeper / MRT data update
  automatically). `CriticalUpdateInstall = 0` and `AutomaticallyInstallMacOSUpdates = 0` unchanged. Nothing on the
  boot path changed.
- AMFIPass 1.4.1 (`com.dhinakg.AMFIPass`, hooks AMFI, CoreTrust, AppleSystemPolicy, Sandbox, EndpointSecurity;
  boot-args `-amfipassoff`, `-amfipassdbg`, `-amfipassbeta` in its strings) lets AMFI stay on for code signed with
  **Dortania's key** (OCLP's root patches; OCLP 0.6.8 changelog: "Removes need for disabling Library Validation and
  AMFI outright"). OCLP forks that can't sign with that key still need `amfi=0x80` (MacRumors Tahoe thread). Vermilion's
  plugin is ad-hoc signed, so it is in the same position: AMFIPass alone won't load it *(inferred, not booted)*.
  OCLP's `amfi_data.py`: `amfi=0x80` = `AMFI_ALLOW_EVERYTHING` (skips library validation, allows invalid
  signatures) and "breaks TCC support in OS (ex. Camera, Microphone, etc prompts)".
- AMFI sysctls now: `developer_mode_status 1`, `launch_constraints_enforced 0`, `trust_cache_interface 4`.
- Plan: decisions 2026-11-22, "Security posture target = a real Mac".

## Update 23:40: first VermilionTrust boot (log only)

Boot 23:31, EFI change 5 (`vermilion-trustlog`, VermilionTrust 1.0.0, `amfi=0x80` kept). 3 min after boot:

```
debug.phxtrust_stat: state=installed mode=log list=5 libval_calls=923 libval_denied=0 mmap_exec=939
  mmap_denied=19826 overrides=99 denied_unlisted=19727 candidates=0 candidates_listed=0 log_lines=256
```

- The kext loads after Lilu and AMFIPass, finds the AMFI policy and swaps the hooks. The accelerated desktop is
  unchanged: WindowServer maps NVMTLDriver, `libnvmtl_translate.dylib` and `libvulkan_radeon.dylib`, at 2560x1440
  240 Hz, Metal supported.
- dyld calls `F_CHECK_LV` even with `amfi=0x80` (923 calls, 0 refused), so the library-validation hook is exercised.
- Bug 1: `mmap_denied` 19.8 k. AMFI takes `PROT_EXEC` out of the **maximum** protection of ordinary data mappings
  even with `amfi=0x80`; 1.0.0 counted that as a refusal and put `PROT_EXEC` back for the listed files (99 times).
  Harmless (our own code), but not "log only". 1.0.1 looks only at mappings that request `PROT_EXEC`.
- Bug 2: 0 candidates. The filter skipped platform binaries, and with `amfi=0x80` every binary counts as platform
  (the same effect that makes tccd deny third-party prompts). 1.0.1 uses the code directory's `CS_ADHOC` flag.
- The kernel log lines were lost: the 256-line cap filled early, and `dmesg` was overwritten by Wi-Fi scan lines;
  the IOLog lines didn't reach the unified log. 1.0.1 keeps a per-file report in `sysctl debug.phxtrust_files`.

## Update 23:52: `amfi=0x80` removed, VermilionTrust enforcing (EFI change 7)

Boot 23:45, stage `vermilion-trust` (VermilionTrust 1.0.2), boot-args
`-v keepsyms=1 debug=0x100 pci=0x8000000 amtk_awdl_offchan=0 ipc_control_port_options=0 phxdebug=1`, SIP `0x0A03`.

- **Accelerated desktop as before:** daemon `SUCCESS at 23:48:05 ... accelerated desktop survived`, 2560x1440 240 Hz,
  Metal supported. Safari, Chess and Calculator map NVMTLDriver and RADV (10 mappings each).
- **AMFI enforces again** and refuses our plugin, and the kext lets exactly that through. The unified log shows AMFI's
  own `Library Validation failed: Rejecting '/Library/GPUBundles/...' for process 'WindowServer' ... reason: mapping
  process is a platform binary, but mapped file is not` (also Music, Finder, SystemUIServer, Safari). `debug.phxtrust_stat`
  after opening the three apps: `libval_denied=25 mmap_exec_denied=25 overrides=50 denied_unlisted=0`. Overrides per
  file: NVMTLDriver 30, RADV 14, translator 6; libLLVM / libzstd not loaded.
- **Third-party apps are no longer platform binaries:** T3 Code's `csops` flags `0x26014201` (with `amfi=0x80`) ->
  `0x22014201`, `CS_PLATFORM_BINARY` gone. WindowServer and Finder keep it (`0x26016a01`), as on a real Mac.
- No new crash report; Wi-Fi router ping 2.2 ms avg (max 3.6); Bluetooth on, bluetoothd up.
- **Pre-existing, not caused by this:** WebKit's GPU process (`com.apple.WebKit.GPU`) has no plugin mapped. AMFI's
  refusal is overridden, but its sandbox may map executables only from system paths (INSTALL.md 5.6:
  `relocate_gpubundle.sh`). Safari's GPU process therefore isn't Vermilion-accelerated. Thread 00 / 00.1.
- Cost: the hooks do work only for executable mappings that AMFI refuses (~1 k executable mappings and 50 overrides
  per boot plus the app launches); not measurable against app launch times *(not benchmarked)*.

## Update 2026-11-23 00:00: Apple Music stops after a few seconds; cause = en0, not AMFI

Owner, 23:54: Apple Music plays ~5 s, then stops or skips to the next track; sometimes Music crashes.

- The crashes are the Metal plugin bug above (`nvmtl_vk_image_read`), thread 00.
- The stops are FairPlay. Music gets its subscription lease (`play> lease> fp status = 1, havelease 1`) and plays
  the start on the shared preview key (`skd://itunes.apple.com/P000000000/s1/e1`). Every request for the song key
  (`skd://itunes.apple.com/p<id>/c6`) then fails: `ICKeyServerError Code=-1003 "Key server returned non-zero status in
  response"`, and AVFoundation `-11835 "Cannot Open" ... "This computer is not authorized to play"` (`-12166`).
- Song-key results per minute in the unified log (`Completed content key request`, c6):
  boot 22:23 (`amfi=0x80`, iPhone on USB at boot = en0): status 1 (ok) at 22:25 ... 23:28, also after the iPhone was
  unplugged at 22:59; boot 23:40 (`amfi=0x80` still, iPhone **not** plugged in at boot: no en0): status 5 (fail)
  at 23:43, 23:44; boot 23:45 (AMFI on): fail. So **AMFI isn't the cause**; the difference is a boot without en0.
  FairPlay's machine identity follows the primary interface en0, which on this Mac was the iPhone's USB Ethernet.
  `/Users/Shared/SC Info` (FairPlay's authorization store) is empty.
- Upstream's "FairPlay doesn't work with `amfi=0x80`" doesn't hold for Apple Music on this machine (it played at
  20:01 and 22:25-23:28 with `amfi=0x80`).

Fix staged (no EFI change): `/Library/Preferences/SystemConfiguration/NetworkInterfaces.plist` now names the
built-in Ethernet (I225-V, whose MAC is the ROM) `en0` and the Wi-Fi `en1`, as on a real Mac; the iPhone / iPad
entries are dropped (they get the next free numbers when plugged in). `preferences.plist`: the Ethernet and Wi-Fi
services follow the new names (Wi-Fi's `JoinMode` kept), the iPhone USB service is removed. Originals in
`~/vermilion-rollback/netcfg-20261122/`. Takes effect at the next boot; iPhone and iPad must not be plugged in before it.

## Update 2026-11-23 00:10: en0 fix booted; Apple Music plays full songs

Boot 00:01 with the iPhone and iPad unplugged: `en0` = Ethernet (built-in, no cable), `en1` = Wi-Fi (active, router
ping 1.6 ms avg, default route). APNs connected; IDS registration answered HTTP 200 (00:02:29). Owner: Music plays
whole songs. Unified log since 00:01: song keys (`.../c6`) `status:1` (granted), no `status:5`, no
"not authorized". Without Account > Authorizations; nothing else changed.

Music still crashes on some tracks (owner, 00:04:52, "Drake - Jaded"). No report was written:
`osanalyticshelper: Report of type '309' not saved because the limit of 25 logs has been reached` (macOS keeps at
most 25 crash reports per app). The 25 reports of 2026-11-22 (all `nvmtl_vk_image_read`) moved to
`~/vermilion-captures/music-crashes-20261122/`, so the next crash is recorded.

## Update 2026-11-23 00:15: the Music crash, analysed for thread 00

Reproducible: playing "Drake - Jaded" in Music crashes it (owner, twice, 00:04:52 and 00:07:27). Report
`Music-2026-11-23-000727.ips` (copy and the process's plugin log in `~/vermilion-captures/music-crashes-20261122/`):
`EXC_BAD_ACCESS KERN_INVALID_ADDRESS at 0x1464c4000`, thread queue `nvmtl.execution`:
`_platform_memmove` <- `nvmtl_vk_image_read` <- `-[NVMTLTexture nvmtlSurfaceOutDirtied:]` <- `nvmtlAdvancePart`
<- `__nvmtl_pump_buffers_block_invoke_2`, the same stack as all 25 crashes of 2026-11-22. The fault address is 1 byte
past the end of an `Image IO` region (`1464c3000-1464c4000`), followed by a 2 MiB gap: the copy runs off the end of
an IOSurface that ImageIO allocated.

Cause (from the source, `src/metal-plugin`): `nvmtlSurfaceOutDirtied:` (`NVMTLObjects.m`, the readback branch)
locks the surface, takes `IOSurfaceGetBaseAddressOfPlane` and `IOSurfaceGetBytesPerRowOfPlane`, and calls
`nvmtl_vk_image_read(q, &r->_i, base, row)`. That (`nvmtl_vk.c`) copies `rows = blocks(img->h)` rows of
`cols * bpp` bytes, taken from the **texture** (`img->w`, `img->h`, `img->bpp`), and checks only
`row_bytes >= cols * bpp`. Nothing checks the texture against the **plane's** height or the surface's allocation
(`IOSurfaceGetHeightOfPlane`, `IOSurfaceGetAllocSize`), so a texture taller than its surface, or one with a larger
pixel size than the surface's elements (Music wraps 600x600 surfaces as fmt 115 RGBA16Float and fmt 80 BGRA8), writes
past the mapping. Which mismatch Jaded's artwork hits isn't in the log *(unverified)*.

Suggested fix (thread 00): in `nvmtlSurfaceOutDirtied:`, before the read, require `img->h <= plane height`,
`cols * bpp <= bytesPerRow` and `rows * bytesPerRow <= alloc size - plane offset`; otherwise clamp the rows (or
refuse the copy) and `nvlog` the texture and plane geometry once, which also names the mismatch.

Build note for any plugin fix: since 2026-11-22 AMFI is on, so a new NVMTLDriver cdhash is refused in Music,
WindowServer and every other platform process until VermilionTrust lists it (`scripts/build/trust.sh` + EFI +
reboot). Thread 05 can make the list keep the previous cdhashes too, so a plugin update and the kext update don't
have to land in the same boot.

## Update 2026-11-23 00:40: the Music crash, cause corrected and fixed; Apple TV playback

**Music.** The crash report's VM map corrects the 00:15 analysis: `0x1464c4000` is in **no region**; the region before
it is a 4 KiB ImageIO guard page (`---/---`), then a 2 MiB gap. `r15 = 2400` = one row of a 600-wide BGRA8 texture
(the fmt 80 surface, row 2432). So the plugin's readback writes to the IOSurface's base address after the memory
behind it was freed in Music: the surface wraps ImageIO memory, and the plugin's existing size checks
(`nvmtlSurfaceCopyable`: width, height, bytes per row and allocation size) all pass. On a real Mac the GPU writes the
surface's pages and never uses that CPU mapping. The plugin source already had the allocation check, so the "no
height / allocation check" guess at 00:15 was wrong.

Fix (owner's go, 2026-11-23): `nvmtl_plane_mapped()` in `src/metal-plugin/NVMTLObjects.m`, after `IOSurfaceLock`,
walks the plane's byte range with `mach_vm_region` (write access for surface-out, read for surface-in). If any part
is unmapped, the copy is refused the way a non-copyable surface is (surface-out returns NO, surface-in skips), and
the plugin logs "G13 surface N plane P NOT synced: ... not mapped in this process" (8 times per process at most). HEAD
rebuilds to exactly the installed cdhash (`8f6eb933...`), so the new build (`33b9a186...`) changes nothing but this.
Installed together with VermilionTrust 1.0.3 (both NVMTLDriver cdhashes listed, `PHXTRUST_KEEP=1`), EFI change 8.
Not yet tested on hardware *(unverified)*.

**Apple TV.** The app runs, previews and interstitials play (decompression sessions build, error 0), the film stops.
For the main (protected) item: some FairPlay key requests succeed, others time out after 20 s (`-19152`); then
`vmc2BuildDecompressionSession ... decompression session 0x0 (error -12906)` (`kVTCouldNotFindVideoDecoderErr`),
`FailedToPlayToEnd -12167`, AVFoundation `-11839` (decoder temporarily unavailable). The HDCP filter reports
`hdcp0:Unknown hdcp1:Undetermined`. VideoToolbox has no decoder that accepts protected samples on this machine: Apple's
secure decode path runs on a hardware decoder (AppleGVA / the GPU vendor's VA driver), and Vermilion has none (VCN off;
thread 00.1). Neither AMFI nor the account blocks it. A third-party VideoToolbox decoder (the `phxdec` HEVC
decoder) wouldn't help here: protected samples aren't given to an arbitrary decoder *(unverified: the decoder list
VideoToolbox asks is in private log lines)*.
