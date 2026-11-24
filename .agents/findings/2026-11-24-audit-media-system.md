# Audit: video, audio, sleep/power, sensors, boot, release shape (2026-11-24)

Read-only audit of the XTX machine (RX 7900 XTX rev 0xC8, macOS 26.7.1 25G241). The boot checked today was 18:30:20
(EFI change 33, VermilionBoot b160, VermilionAudio 1.7.2, VermilionAccel `2B25514A`, daemon = the
`ownback/dp-hdmi-audio` copy). The probes ran 20:11-20:15. "Today" means I saw it myself on this machine in this audit.
Anything else is cited from a finding or a TODO and marked as such. State values follow ROADMAP "The standard".

Commands run today: `pmset -g`, `pmset -g assertions`, `sysctl debug.phx*`, `tail /var/log/phxboot.log`,
`kmutil showloaded`, `system_profiler SPDisplaysDataType SPAudioDataType`, `ioreg` (NMXAccel), `smcread -s`,
`bash tools/validation/bootcheck.sh`, a VideoToolbox capability query (`scratchpad/audit/vtlist.swift`: decode-support
flags and the encoder list only, no GPU load), and a read of today's crash reports. The guard blocked `diskutil info /`
and `csrutil authenticated-root status`, so I couldn't re-check the seal state today.

## Parts as a Mac user notices them

| Part | State | Evidence | Next step | Size |
|---|---|---|---|---|
| HEVC 8/10-bit hardware decode (QuickTime, Safari, Quick Look) | works-but-not-standard | Today: `VTIsHardwareDecodeSupported` hvc1 = true; `PhxVideoDecoder.bundle` is in `/System/Library/Video/Plug-Ins`. Bit-exact vs Apple's decoder (apis-video (`.agents/findings/2026-11-23-apis-video.md`) section 3). Because the bundle sits on the system volume, the seal is broken (ROADMAP "The standard") | Move decode to the route a real AMD Mac uses: AppleGVA loads `PhxVA.bundle` by `IODVDBundleName` from `/Library/GPUBundles`. `src/video-accel/PhxVA.c` is an M0 stub that returns 5. Today `IODVDBundleName` isn't published (ioreg) and PhxVA isn't installed. This route is missing from TODO 09's list of seal-keeping options. Whether AppleGVA accepts it on 26.7.1 is (unverified) | L |
| AV1 decode | works-but-not-standard | Today: av01 = true; the `phxdec-av1` flag is set. If VCN is down or `phxdec-off` is set, AV1 fails with `-12906` instead of falling back (apis-video section 3; TODO 13 item 13) | Decline AV1 in the bundle's matching when VCN is down, or find out why Apple's SW AV1 decoder isn't tried | S-M |
| 4K YouTube in Safari (AV1 2160p60 HDR) | broken | Owner, 2026-11-23: stutters and glitches. Decode is bit-exact (600/600 frames). It's synchronous, ~13.3 ms per frame with spikes to 64 ms (apis-video section 7). Not re-tested today (that would load the GPU) | Several frames in flight, decode straight into the IOSurface (TODO 13 item 11) | M |
| H.264 hardware decode | missing | Today: avc1 = false (Apple's CPU decoder runs instead). FFmpeg `h264_vulkan` and RADV H.264 are already in the build; `phxdec_hevc.c` handles HEVC/AV1 only (TODO 13 item 13) | Add H.264 to the decoder and the bundle | M |
| VP9 hardware decode | missing | Today: vp09 = false. YouTube in Safari gets VP9 "supported, not smooth" (apis-video section 7). Whether a real Mac Pro 7,1 with an AMD card decodes VP9 in hardware is (unverified) | Check what a real Mac does first, then a VP9 path through the same decoder | M |
| HDR video and the HDR desktop | broken (HDR off as a workaround) | Today: `~/gpu/phxboot-nohdr` is set, the log says `hdr: off`, and System Information shows 24-bit ARGB8888. The cause: after a DP replug, WindowServer composites in BGRA and the plugin's CPU widen path shows stale frames, so the picture freezes a few minutes after login (branch finding `2026-11-24-hdr-freeze-after-replug.md`) | Fix the plugin's widen path (refresh every frame or refuse it) and find out why the surfaces are BGRA; then remove `phxboot-nohdr` | M |
| Photos, HEIC, video wallpaper, `.mov` in QuickTime | works-but-not-standard (unverified by eye) | HEIC decodes on the CPU (PROJECT_KNOWLEDGE:53). Photos, the video wallpaper and QuickTime are still open owner tests (OWNER-TESTS 03) | Owner's eye test; HEIC through VCN once the decode route is settled | S |
| Hardware encode (FaceTime, screen recording export, AirPlay / Sidecar sender, Handbrake / Compressor through VideoToolbox) | missing | Today: every encoder in `VTCopyVideoEncoderList` reports hw=false, H.264 and HEVC included. No AV1 encoder (`-12908`, apis-video section 3). A real Mac Pro 7,1 with an AMD card encodes H.264 / HEVC in hardware (unverified on a real unit) | VCN encode through RADV Vulkan Video encode, behind the decoder's chain. New VCN encode programming goes through `hw-safety-reviewer`. Not in TODO yet | L |
| Screenshots and screen recording | works (screenshots); ScreenCaptureKit untested | `screencapture` returns the real GPU desktop (macos-integration (`.agents/findings/2026-11-23-macos-integration-inventory.md`)). Not re-run today. QuickTime screen recording and ScreenCaptureKit are (unverified) | One QuickTime screen recording of a short session | S |
| FaceTime video, camera | untested | iMessage / FaceTime are registered but nothing has been sent or called yet (PROJECT_KNOWLEDGE:73). No webcam is attached. Continuity Camera over USB gives a black picture (a network / AWDL issue, not the GPU) | A FaceTime video call once a camera is there (encode is on the CPU, see above) | S |
| Apple Music | broken (crashes) | Today: two Music crashes, `Music-2026-11-24-184731.ips` and `-193227.ips`. Both are EXC_BAD_ACCESS in `_platform_memmove` <- `nvmtl_vk_image_read` <- `-[NVMTLTexture nvmtlSurfaceOutDirtied:]`. The `nvmtl_plane_mapped` guard (`src/metal-plugin/NVMTLObjects.m:1454`) is in that path and doesn't catch it. This conflicts with "Music works, no problems" (branch OWNER-TESTS 04, 18:30 boot) | Find out why the mapped-range check passes and the copy still faults (size or stride vs the mapping, or the mapping going away mid-copy) | S-M |
| FairPlay video (Apple TV, Safari protected streams) | n/a (Known limit) | ROADMAP "Known limits" | Write it up in the README | — |
| Safari GPU process (video, WebGL, WebGPU) | broken in part | WebGPU render bundles crash `com.apple.WebKit.GPU` (4 reports on 2026-11-23, apis-video section 7). A crash takes every tab's video with it | TODO 13 item 12 (ICB support in the translator) | M-L |
| DP audio to the LG (speakers on the monitor's jack) | works-but-not-standard | Today: "LG ULTRAGEAR+" is the default output, Transport DisplayPort, 8 ch, 48 kHz. Daemon at 18:32: "display engine programmed (90 writes) ... DP audio engine up after 2 s"; `debug.phxaudio_stat` reads patched and delivered. Not standard because user space sets it up after login (`audio_boot`, flag `phxboot-audio`), a Lilu patch adds ab30 to AppleGFXHDA's tables, and the framebuffer properties are set by sysctl rather than published by PhxAMDFB. It's only on branch `ownback/dp-hdmi-audio`, and PROJECT_KNOWLEDGE:59 on main still says "not implemented" | Merge the branch (phase 1). Then PhxAMDFB publishes `connector-type` / `av-signal-type` / `AAPL,DisplayPipe` / `audio-codec-info` and handles `'aud '`, and the kernel programs DC audio from the EDID (branch TODO 04) | M-L |
| DP audio device name | broken (cosmetic) | Today: `system_profiler SPAudioDataType` shows "LG ULTRAGEAR+?" with a stray byte. The display entry reads "LG ULTRAGEAR+" | Trim the monitor-name string (ELD / EDID name) where it's built | S |
| DP audio volume and switching outputs | works (owner, unverified today) | Branch OWNER-TESTS 04: volume changes, switching to the Realtek and back, and 240 -> 144 -> 240 Hz while playing all fine (2026-11-24 evening) | — | — |
| Picture and audio after a DP unplug / replug | broken | Branch TODO and finding: after a replug the LG stays black even though the link trains (HBR3 x4); only a restart brings it back. Audio after a replug isn't tested (branch OWNER-TESTS 04) | Re-run the display enable (and the audio re-program and re-deliver) on hot-plug. First read the logs for whether AppleGFXHDA's controller leaves ab30 on unplug (see the risks section) | M |
| HDMI output (picture and audio) | missing | The LG over HDMI shows black: there's no plan for it (branch TODO 04) | A Linux capture of the LG on HDMI, through `linux-queue` | L |
| System sleep (Apple menu > Sleep, power button) | missing (blocked) | Today: `SleepDisabled 1`, sleep 0, Sleep Count 0 (bootcheck). There's no GPU resume path: `phxfb.cpp` `setPowerState` only acknowledges (sleep-power finding) | The platform S3 test on a `-phxbringupoff` boot, then a GPU resume path (TODO 05) | L |
| Display sleep (monitor to standby) | broken | Today: displaysleep 0. 2026-11-23: `pmset displaysleepnow` gives a black screen but the link and OTG stay up, so the LG never goes to standby (no OLED panel care). The DPMS traces are captured (TODO 05) | The DPMS off/on plans in the replay engine (TODO 05 plan steps 1-4) | M |
| GPU idle power | works-but-not-standard | 2026-11-23 (perf-power finding section 9): with `idledpm`, macOS idles below Linux's 35 W at 240 HDR. It's driven by the daemon's flags (`phxboot-idledpm`, `-dcf`, `uclk`, `cg`, `sdma`), not by the driver | Move DPM into the kext (phase 3) | L |
| Shutdown, restart | works | Today: previous shutdown cause 5. There are 2 shutdownStall reports, one per restart (18:21, 18:29), ~2-3 s of normal teardown (sleep-power finding). A restart to the accelerated desktop takes ~67 s | Low priority | S |
| GPU in System Information | works | Today: "AMD Radeon RX 7900 XTX", 24 GB, 0x744c rev 0xc8, Metal 3, the LG at 2560x1440 @ 240 Hz (24-bit only because of the HDR workaround) | — | — |
| Activity Monitor GPU History, per-process GPU % | missing (unverified by eye) | Today: NMXAccel `PerformanceStatistics` has 76 keys but no "Device Utilization %" (or renderer / tiler utilization), and `git grep 'Device Utilization'` finds nothing. Activity Monitor reads GPU History from that key (unverified on this build). The kext already measures GPU time per context (b157 `phx_gputime.cpp`) | Publish utilization from the existing GPU-time accounting in `PerformanceStatistics` | S-M |
| GPU temperature, fan, power in Mac apps (iStat Menus, Stats) | missing | Today: `smcread -s` lists only CPU keys (TC0E/F/G/J/P/T/p). `PerformanceStatistics` has no Temperature / Fan / Power keys. The only reader is `sudo ~/gpu/smumetrics.py`. The GPU fan is run by the SMU firmware: 0 % at idle on 2026-11-23 (zero-RPM) | Publish the SMU metrics the way AMD's macOS driver does (PerformanceStatistics keys; SMC keys only if a real Mac has them) | M |
| Board and CPU fans | missing | No Super I/O plugin (PROJECT_KNOWLEDGE:64; NCT6799D, TODO). Platform part, goes to ryzentosh | `SMCSuperIO`, its own EFI change | S |
| What the user sees at boot | works-but-not-standard | Today: kernel 18:30:20, daemon 18:30:24, WindowServer restarted onto Metal 18:30:35 (the first WindowServer runs without the GPU), first link training 18:30:47, SUCCESS 18:32:44. boot-args include `-v` (verbose text instead of the Apple logo), and OpenCore shows the picker. ~67 s restart, ~45 s of it POST + picker (sleep-power finding). ROADMAP "The standard": the GPU is powered up by a user-space daemon | The driver brings the GPU up in the kernel before WindowServer starts (phase 3). A release OpenCore without `-v` belongs to ryzentosh | L |
| Auto-login | works-but-not-standard | Today: "autologin: on for '<user>' ... first-run marker removed". The daemon turns `autoLoginUser` off at start and on again before the restart, edits loginwindow state every boot, and is incompatible with FileVault (TODO 06). The fix is only in the branch and machine copy: the live `~/gpu/phxboot-navi31.sh` equals the branch copy and differs from main (today, `diff`) | Merge. Then remove the need for it (no WindowServer restart once bring-up is in the kernel) | M |
| Fallbacks (kill switch, pending mark, GOP revert, watchdog, DARK check) | works | Today: SUCCESS line, the replay engine at `recoveries=0/2`. `nvstuck` appears 22 times in the log: macOS can't clear `phxfb-squat` because of `DisableVariableWrite` | Keep. Reword the Reset NVRAM advice (see the risks section) | S |
| AuxKC "Allow" click | works-but-not-standard | Listed in ROADMAP "The standard". Needed at every VermilionAccel install or update. This boot loaded it from the AuxKC (no `kmutil` line) | Load VermilionAccel from OpenCore (TODO 06, first item) | M-L |
| Release shape ("one kext + config snippet") | missing (far) | Today the user-visible install is: 4 kexts (Boot, Trust, Audio from OpenCore; Accel from the AuxKC); the plugin, RADV, translator and 3 decoder dylibs under the PhxGPU firmlink; the decoder bundle on the system volume; the daemon, 21 `phxboot-*` flag files and the AMD firmware in `~/gpu`; `phxfb-squat` set from Linux; the monitor's replay plans compiled into VermilionBoot; a cdhash list rebuilt for every user-space build; SIP 0x0A03; macOS pinned to 25G241 | TODO 06 in order: Accel from OpenCore, cdhashes as data, the daemon out of `~/gpu`, the squat variable from OpenCore, plans as data. Then bring-up into the kext | L |
| Platform (brief) | other repos | AirDrop fails and Continuity Camera shows black (Wi-Fi/BT, ryzentosh-airdrop). The onboard USB audio offers only jacks plugged in at boot (PROJECT_KNOWLEDGE:58). NVMe shows as "external". No Hypervisor (Known limit) | Other repos / TODO 10-11 | — |

## The 5 things a Mac user would notice most

1. **HDR is off, and a replug needs a restart.** HDR video and the HDR desktop are disabled as a workaround
   (`phxboot-nohdr`) for a frozen picture. After a DP unplug or replug the screen stays black until a restart.
2. **No sleep.** Apple menu > Sleep does nothing (`SleepDisabled 1`), and display sleep blanks the picture but keeps the
   signal, so the OLED never goes to standby.
3. **Video is half there.** HEVC and AV1 run in hardware, but 4K YouTube stutters. H.264 and VP9 decode on the CPU, and
   there's no hardware encode at all (FaceTime, screen-recording export, AirPlay / Sidecar sending, VideoToolbox exports
   all use the CPU).
4. **Apps crash in the GPU plugin.** Music crashed twice today in `nvmtl_vk_image_read`, although the guard meant to
   stop this is installed. WebGPU render bundles take Safari's GPU process down, and every tab's video with it.
5. **Boot and install don't look like a Mac.** Verbose text, the OpenCore picker, a WindowServer restart ~15 s in, ~67 s
   per restart. Installing needs the AuxKC Allow click and an unsealed system volume, and the release is far from one
   kext: DP audio works, but only through the daemon and the unmerged branch. (GPU temperature, fan and load are also
   missing from Activity Monitor and from sensor apps.)

## Hard-rule risks spotted

- **The daemon tells the user to Reset NVRAM.** `tools/phxctl/phxboot-navi31.sh:271,277,282` on main (276/282/287 on the
  branch) prints "Reset NVRAM in the OpenCore picker clears it / for the firmware display". CLAUDE.md hard rule 1 says
  never Reset NVRAM, because it removes `phxfb-squat`. This line appears in every boot log (22 times). Reword it to the
  approved recovery (`OC.prev`, the kill switch) or state the cost.
- **The sleep owner test is unsafe.** Branch OWNER-TESTS 04 asks the owner to "sleep the Mac (Apple menu > Sleep), wake
  it" to test audio. Sleep is blocked and there's no GPU resume path. Lifting `disablesleep` for that test would most
  likely lose the GPU. Mark it blocked until TODO 05.
- **The replug test could hot-reset the GPU.** VermilionAudio 1.6+ makes the ab30 attach one-way, but nobody has checked
  whether a DP unplug makes AppleGFXHDA's controller leave the ab30 nub. If it does, the IOPCIFamily detach rule
  hot-resets the whole GPU (finding `2026-11-24-gfxhda-detach-gpu-hot-reset.md`) (unverified). Read the 00:05 replug
  logs for `detachFromChild` / `needing hardware reset` before asking the owner to replug with audio on.
- **Main doesn't match the machine.** The live daemon is the branch's copy (autologin fix plus `audio_boot`).
  Reinstalling it from fork/main would drop both (the branch TODO warns about this). Merge before any daemon deploy.
- **The system volume is unsealed** (known; the owner agreed, TODO 09). The AppleGVA / PhxVA route is the Mac-like way
  to keep the seal and isn't on TODO 09's research list yet.
- **Hardware encode would be new VCN programming.** It's an encode ring and session messages that today's `vcn_up.sh`
  doesn't send. It has to go through `hw-safety-reviewer` against what Linux sends on Navi 31.
- **Tooling:** the guard blocks read-only `diskutil info` and `csrutil ... status`, so seal checks can't be done without
  the lock. Minor; worth an allow-list entry.
