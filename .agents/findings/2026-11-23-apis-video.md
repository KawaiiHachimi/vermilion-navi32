# Graphics APIs and video (thread 03), 2026-11-23

XTX machine (RX 7900 XTX rev `0xC8`, macOS 26.7.1 25G241), VermilionBoot b160, 1440p240 HDR. Everything here was run on
that machine unless marked. Raw outputs: `~/vermilion-captures/thread03/` (not in git; no serials).

## 1. `gpuTimeReset` and the VCN queue (TODO 03, Fable follow-up from #15)

`gpuTimeReset` retires all three queues, the VCN queue included; `gpuTraceSet`, `gpuTraceDump` and `gpuTimeStat` skip
it. Retiring the VCN queue is harmless: every VCN entry is queued with `fctx = kNoCtx` (`gpuSubmitVcn`), so
`gpuTimeAccount` returns at once, and `retireLocked` only moves `head` / `doneEnd` past entries whose fence has passed,
the same thing `gpuSubmitVcn` does under the same `gpuLock` before every VCN submission. `queueFence` on the VCN queue
widens the 32-bit fence correctly. **No change needed**; the three `!vcn` skips are unnecessary but harmless (folding
the loop into one helper stays with thread 13 item 9).

## 2. VCN 4.0 on the XTX

`tools/phxctl/vcn_up.sh` and `vcn_m1_gen.py` were written from a Linux 6.8 trace of `vcn_v4_0_start_dpg_mode` on the
**XT**. Checked for the XTX before any write (CLAUDE.md hard rule 1):

- Same ASIC (Navi 31): every write in the script is one Linux amdgpu sends on Navi 31 (PSP `LOAD_IP_FW` VCN and
  VCN0_RAM, SMU `PowerUpVcn` instance 0, the DPG-mode `UVD_POWER_STATUS` / ring registers, the ring and IB tests).
  The other hard-coded Navi 31 IP bases (GC, MMHUB, NBIO, MP0, MP1, DCN) already work on this card, so the IP
  discovery table matches; Linux on the XTX lists one VCN 4.0 decode queue and loads VCN firmware (instance 0 present).
- Card-specific inputs, all read on the XTX: FB MC base `0x80_0000_0000` (`phx-fb-mc-base`), `GB_ADDR_CONFIG 0x545`
  (pre-flight), kext build 160, `phxdebug=1`. VRAM layout: the VCN area (fw_buf + 8 MiB = 136 MiB) overlaps none of
  the b156-b160 regions (VRAM BO pool from 512 MiB, CPU-visible pool from 352 MiB, gputime 151 MiB).
- Firmware: the pinned `vcn_4_0_0.bin` is ucode `0x09118016`; Linux on this machine loads `0x09118022` (newer
  linux-firmware). The pinned file is the one the XT ran; the PSP accepted it here.

Result, 2026-11-23 18:09 (manual `--run`, owner at the machine): **M1 PASS** in under a second: `PowerUpVcn` OK,
`UVD_POWER_STATUS` 1 then `0x105`, ring test RPTR `0x40` after 1 read, decode create / destroy session IBs, fence 2.
`debug.phxvcn=1` (ring adopted) and `debug.phxmmvm=1` (MMHUB VM: `MX_L1_TLB_CNTL 0x1859`, fault status 0), kext ring
test 158 us. At boot (`~/gpu/phxboot-vcn`, EFI change 23 boot 18:15 and the boot of 18:21): `vcn: up, ring adopted,
MMHUB VM on` after `done`, no fault.

## 3. Hardware decode through VideoToolbox

Chain: `PhxVideoDecoder.bundle` (VideoToolbox plug-in, `/System/Library/Video/Plug-Ins`) -> `libphxdec_hevc.dylib`
(next to the plugin) -> FFmpeg n7.1 `libavcodec_phx` (`hevc` / `av1` + `*_vulkan` hwaccels, `scripts/build/ffmpeg.sh`,
new) -> RADV Vulkan Video -> the kext's VCN queue.

- **In-process test** (`vtdec_test <movie> 30 libphxdec_hevc.dylib`, test clips from `tools/metaltest/mkclip.swift`,
  new: 1920x1080 Apple-encoded HEVC): Main 8-bit 30/30 frames and Main10 30/30 frames **bit-identical** to Apple's
  software decoder; 31 VCN submissions per 30 frames; no MMHUB fault.
- **Registered by the plugin only (no bundle):** the plugin registers the decoder in every Metal process when its
  Metal device is created. `VTDecoderXPCService` logged `REGISTERED`, but VideoToolbox had already chosen Apple's
  decoder (`vcn submits` unchanged): registration at Metal-device time is too late. The bundle in
  `/System/Library/Video/Plug-Ins` is needed (INSTALL 5.5; `/Library/Video/Plug-Ins` drops ad-hoc bundles).
- **With the bundle** (installed with `sysvol_install.sh` 18:20, booted 18:21): `vtdec_test` without the dylib
  decodes in `VTDecoderXPCService` on VCN, 30/30 frames identical to Apple's, per frame: decode 0.78 ms, VCN wait
  3.37 ms, transfer 0.86 ms (1080p). `VTIsHardwareDecodeSupported`: HEVC **true**, AV1 **true**, H.264 false, VP9
  false. A Quick Look thumbnail of the clip decodes on VCN (2 submissions; image correct). WebKit `<video>`
  (`webprobe <timeout> <movie>`, a WKWebView, Safari's media stack) decodes on VCN (70 submissions per 2 s play);
  whether the frames reach the screen in Safari is for the owner's eye (`requestVideoFrameCallback` barely fires in
  this command-line window).
- **AV1**: no AV1 encoder on this Mac (`VTCompressionSessionCreate` `-12908`; `AVAssetWriter` refuses `av01`), so five
  AOM conformance vectors (`storage.googleapis.com/aom-test-data`: `av1-1-b8-01-size-226x226`, `-02-allintra`,
  `-04-cdfupdate`, `-23-film_grain-50`, `av1-1-b10-00-quantizer-10`) were remuxed to MP4 with a minimal FFmpeg CLI and
  decoded with `phxdec-av1` set: **55 of 55 frames match the official per-frame MD5s** (`vtdec_test` with
  `VTTEST_I420=1`, new), 8-bit, 10-bit, odd size and film grain.
- **No AV1 fallback:** with the bundle installed and our decoder refusing a session (`phxdec-off`, VCN not up, or
  `phxdec-av1` absent), AV1 sessions fail with `-12906`; VideoToolbox does not try another decoder. HEVC falls back
  to Apple's decoder. So `phxdec-av1` stays set (2026-11-23) and AV1 works whenever VCN is up. Whether AV1 decoded at
  all before the bundle (Apple's `AV1DecoderSW` on this Intel-only build) was not checked (unverified).
- H.264 has no VCN path in our decoder (CPU, Apple's decoder).

## 4. Safari / WebKit GPU process

`com.apple.WebKit.GPU` could not map the plugin: `file system sandbox blocked mmap() of
'/Library/GPUBundles/NVMTLDriver.bundle/Contents/MacOS/NVMTLDriver'` (log, every Safari start). `webprobe` (new,
WKWebView in the same GPU process) before: WebGL 1 / 2 contexts **lost at creation** (every parameter null), WebGPU
**no adapter**. `relocate_gpubundle.sh` (plugin and `nvmtl/` into
`/System/Library/CoreServices/CoreTypes.bundle/Contents/Library/PhxGPU`, a firmlink into the Data volume per
`/usr/share/firmlinks`; symlinks left in `/Library/GPUBundles`) 18:07; after, without a restart: WebGL 1 and 2 live
(renderer "Apple GPU" through NVMTL, 34 / 29 extensions, max texture 16384, 100 full-canvas draws in 2-3 ms, correct
pixel), WebGPU adapter `amd` with 13 features. WindowServer maps the plugin from the new path since the boot of 18:15
(trust list unchanged: cdhashes, not paths). The script's last check (`codesign --verify --strict` on the bundle)
fails for the original too (the plugin is signed as a binary without sealed resources); it now compares cdhashes.

## 5. Vulkan loader, ICD and a window surface

- RADV was built with `-Dplatforms=` (no WSI platform). This Mesa has `wsi_common_metal.c` (KosmicKrisp's), with a
  software path for drivers that cannot render into the drawable's texture. **Mesa patch 0020**: with
  `-Dplatforms=macos` RADV offers `VK_KHR_surface`, `VK_EXT_metal_surface` and swapchains through that path.
- First run: `vkQueuePresentKHR` -> `VK_ERROR_DEVICE_LOST` after "CS has been rejected (-22)". Traced in the Darwin
  layer (temporary prints): the WSI copy went to RADV's private SDMA queue (`radv_wsi_get_prime_blit_queue`), which
  reaches the Darwin layer as a CS with fence, timeline wait / signal and BO-list chunks **and no IB**
  (`acd_cs_plan_init` -> `-EINVAL`); the kext has no SDMA queue. 0020 leaves the blit queue unset with the Metal WSI
  (the copy runs on the presenting queue). The option to test this by hand is `RADV_DEBUG=nodma`, not `nodmablit`.
- `tools/metaltest/vkwindow.m` (new; NSWindow + CAMetalLayer, FIFO, clears): **480 frames at 213 fps** at 640x360 on
  the 240 Hz display; the captured window pixels (`49 7e da` in the LG's HDR profile) equal an AppKit window filled with
  the same sRGB colour `20 80 e0`, so presented frames reach the screen with the right colour.
- Everything in 0020 is under `VK_USE_PLATFORM_METAL_EXT`: the default build (the plugin's RADV) compiles to the same
  objects apart from the git version string and one `__LINE__` constant in `radv_instance.c` (checked object by
  object against the live build).
- Installed **app-only, opt-in** (no restart, nothing WindowServer loads): Khronos Vulkan-Loader v1.3.296 in
  `/usr/local/lib/vermilion/libvulkan.1.dylib`, RADV with 0020 in `/usr/local/lib/vermilion/libvulkan_radeon.dylib`
  (cdhash `e27907f2`), manifest `/usr/local/share/vermilion/vulkan/icd.d/radeon_icd.json`. An app uses it with
  `VK_DRIVER_FILES=<manifest>` and the loader on its library path. Not in `/usr/local/lib` or the standard ICD
  folders on purpose: apps that find a system loader would switch from their bundled MoltenVK to RADV; that switch is
  thread 13's (TODO 13 item 6). Not in `/Library/GPUBundles/nvmtl/` either: the plugin looks for `libvulkan.dylib` and
  `radeon_icd.json` there. Two RADV copies in one process (the app's ICD and the plugin's, for the Metal blit) work.
- Performance of this path (a copy to host memory plus a Metal blit per frame) is thread 13's.

## 6. Smaller results

- `apiprobe`, before sections 8 and 9 (which put OpenGL and OpenCL on the GPU): Metal device "AMD Radeon RX 7900 XTX
  (RADV NAVI31)", Metal 3 families, 6 GiB working set; OpenGL only "Apple Software Renderer" (no accelerated pixel
  format); OpenCL only the CPU; Core Image on Metal; MetalFX spatial and temporal supported.
- Quick Look vs `sips` (`Mac Blue.heic`, 512 px, warm): Quick Look 1.00-1.19 s (n=6), `sips` 1.01-1.05 s (n=5). The
  12.2 s of 2026-11-23 was the cold first run; no gap. (The file is 9.3 MB now; the inventory noted 21-25 MB, likely a
  different file.)

## 7. The owner's by-eye results, triaged (2026-11-23 evening)

Owner: boot clean; the WebGPU `workloadSimulator` sample (useGl) fine; some webgpu-samples error, glitch or are slow;
4K YouTube in Safari stutters and has glitches. The coordination thread's call: continue 03, classify both first.

**4K decode is correct.** The YouTube stream was AV1 3840x2160 59.94 fps 10-bit BT.2020 PQ (format 701,
`av01.0.13M.10`), decoded by our decoder in `com.apple.WebKit.GPU` (log: decode 6.45, VCN wait 4.03, transfer 2.81 ms
per frame, synchronous; VideoToolbox's stats: mean 14 ms, max 64 ms). 10 s of the same stream from a public 4K demo
video (40 MB range download, remuxed to MP4 / IVF with the minimal FFmpeg CLI, now built with the IVF / OBU / WebM
muxers): `vtdec_test` through VideoToolbox on VCN (`vcn submits` +65 for 60 frames) gives per-frame I420 MD5s
**identical to dav1d 1.5.4 for 600 / 600 frames**. A 4K HEVC Main10 clip (`mkclip.swift`, 60 frames) on VCN is
identical to Apple's software decoder (`phxdec-off` for the reference run, VCN counter unchanged) for **60 / 60**. So
the glitch is presentation or timing, not decode: thread 13 (TODO 13 item 11), with the speed work (frames in flight,
decode into the IOSurface without the copy).

**What Safari tells YouTube** (`webprobe`, new media section; Media Capabilities, MSE, 60 fps, 20 Mbit/s):

| Codec | MSE | supported | smooth | powerEfficient |
|---|---|---|---|---|
| AV1 2160p60 10-bit | yes | yes | yes | yes |
| AV1 1440p60 10-bit | yes | **no** | no | no |
| VP9 2160p60 (8 and 10-bit) | yes | yes | no | no |
| HEVC 2160p60 | no | no | no | no |
| H.264 1080p60 | yes | yes | yes | no |

`VTCopyAV1DecoderCapabilitiesDictionary` reports profiles 0-2 hardware, 8 / 10-bit, max decode level 19, max playback
level 17 (6.1), max HDR playback level 16, values our bundle does not set (its matching info has only codec types,
`VTIsHardwareAccelerated`, `VTRating`). Why WebKit says AV1 1440p60 is unsupported was not looked into. **Stopgap not
shipped:** making YouTube serve VP9 at 4K means AV1 advertised only up to a lower level, which needs the bundle on the
system volume or the plugin's registration to change: a restart either way (the coordination thread's rule: skip,
record). Whether VideoToolbox reads a per-decoder `VTMaxPlaybackLevel` from the bundle is (unverified).

**WebGPU samples** (`tools/metaltest/wgsamples.m`, new: each deployed sample 6-7 s in an on-screen 800x600 WKWebView,
page errors + uncaptured WebGPU errors + the plugin's log, window captures in `~/vermilion-captures/thread03/wgsamples*`).
43 deployed samples (the `main` branch has 10 more that are not deployed; they gave 404 in the first pass). Failing:

| Sample | Kind | First error (page / plugin log) |
|---|---|---|
| a-buffer | error | Fragment function failed creation / translator: graph walk `opcode=tail`; unknown SSA value `%.cast` |
| animometer | error, slow (29 fps) | render bundle path: `vsICB` / `vsUshortICB`: unrecognized AIR intrinsic `air.draw_indexed_primitives_render_command`; device destroyed |
| renderBundles | error, slow (16 fps, then 0.6) | same ICB intrinsics; `newIndirectCommandBufferWithDescriptor` -> nil; `newTextureWithDescriptor` failed |
| cornell | error | createComputePipeline failed / translator: `ImageQuerySizeLod` contract; unknown SSA value; sampler operand |
| cubemap, deferredRendering, normalMap, shadowMapping, samplerParameters | error | Fragment function failed creation / translator: unknown SSA value |
| imageBlur, particles | error | createComputePipeline failed / translator: unknown SSA value |
| points, textRenderingMsdf, wireframe | error | Fragment function failed creation / translator: graph walk `opcode=tail` |
| videoUploading, videoUploadingWebCodecs | error | Fragment function failed creation / translator: unknown SSA value |
| texturedCube, cameras, generateMipmap, timestampQuery | glitch | no error; the textured cubes draw black (host-pointer imports are refused by the kext in these, but `newBufferWithBytesNoCopy` falls back to a copy, so that alone does not explain it) |
| primitivePicking | not supported | needs the `primitive-index` feature (not offered) |

**Render bundles crash Safari's GPU process:** four `com.apple.WebKit.GPU` crash reports (22:16 and 22:17 from the
owner's Safari, 22:50 and 22:53 from `wgsamples`), all `EXC_BREAKPOINT` in
`WebGPU::Device::icbCommandClampFunction` <- `RenderBundleEncoder::splitICB` <- `RenderBundleEncoder::finish`: WebKit
traps when the indirect-command-buffer path is not there (the ICB clamp pipeline fails to build: the
`air.draw_indexed_primitives_render_command` intrinsic). A crash takes every tab's WebGL / WebGPU / video with it until
WebKit restarts the process.

The other 22 draw correctly at about 60 fps (`multipleCanvases` 46 fps). None of the failures is a small missing feature
or validation fix (translator bugs in several places, indirect command buffers not built, the black textures not
understood), so all go to thread 13 (TODO 13 item 12; ROADMAP row 13 now takes the GPU robustness leftovers too).

## 8. OpenGL on the GPU: `AppleMetalOpenGLRenderer`

macOS 26.7.1 ships `/System/Library/Extensions/AppleMetalOpenGLRenderer.bundle` with an x86_64 slice: Apple's OpenGL
over Metal (Apple silicon's OpenGL). OpenGL.framework picks a hardware renderer from an IOAccelerator's
`IOGLBundleName`. `NMXAccel` publishes `MetalPluginName` from its personality but no `IOGLBundleName`: upstream sets it
only in `onCountGrewLocked`, on the NVIDIA display path, which the `PHX_FB` flow never runs. Hence the Apple Software
Renderer for every OpenGL app (section 6). `IORegistryEntrySetCFProperty` from user space returns success but changes
nothing. **Built:** `debug.phxgl` in VermilionAccel (default 0; 1 publishes
`IOGLBundleName=AppleMetalOpenGLRenderer`, 0 removes it; only processes started afterwards see the change). The
branch's source rebuilt the installed kext byte-identically first (UUID `0D8DC116`), so the new build differs only by
this.

**Verified (installed 23:05, booted 23:18, `debug.phxgl=1` at 23:25):** `apiprobe` before: accelerated pixel format
refused, "Apple Software Renderer"; after: vendor `AMD`, renderer "AMD Radeon RX 7900 XTX (RADV NAVI31) (NVMTL over
...)", version "4.1 Metal - 90.5" (4.1 core, accelerated only) and "2.1 Metal - 90.5" (legacy). An offscreen GL 4.1
test (FBO, one shaded triangle, three pixels read back) gives the same pixels as the software renderer (one channel
off by 1 in the clear colour, rounding); 200 draws + `glFinish`: 0.82 ms vs 158.34 ms. Minecraft 26.3 (Prism
Launcher, plain instance, owner at the machine): F3 "OpenGL 4.1 Metal", 40-80 fps, no crash report, java ~190 % CPU.
Daemon flag `phxboot-gl` (mock-tested) sets it at every boot.

Zink is the fallback, not the plan: on macOS Mesa's GL front ends are GLX (XQuartz) and EGL, not CGL / NSOpenGL, so
LWJGL / GLFW apps can't load it as is.

## 9. OpenCL on the GPU; Blender

With `debug.phxgl=1`, Apple's OpenCL lists a second device (`apiprobe`): "AMD Radeon RX 7900 XTX (RADV NAVI31) (NVMTL
over ...)", type GPU, OpenCL 1.2, 96 compute units, 6144 MiB global memory. On Intel Macs OpenCL finds GPUs through the
same `IOGLBundleName` path as OpenGL; nothing else was changed. A check (`y = a*x + y; y += sin(y)/2` on 4,194,304
floats, verified on the CPU): **0 mismatches**, max abs error 7.6e-6 (the GPU's `sin`); 1.44 ms per run after the
first (the CPU device: 1.45 ms). The buffers sit in system memory (the process is not in `phxbo_vram_proc`): speed is
thread 13's.

**Blender Cycles** lists only the CPU because of Blender, not the driver: `intern/cycles/device/metal/util.mm`
(`MetalInfo::get_usable_devices`, current `main`) accepts a Metal device only if its name contains "Apple" and not
"AMD" / "Intel", and it has unified memory. Blender 4.2 dropped AMD and Intel GPUs on macOS; a real Intel Mac with an
AMD GPU is CPU-only there too. Renaming our device to pass that check would mislead every other app and is not done.
Blender 4.1 and older had an AMD Metal path (unverified on this machine). EEVEE and the viewport use Metal (not
checked here).

## Not done in thread 03

- By eye, the owner's (OWNER-TESTS 03): Photos, a video wallpaper, an HEVC `.mov` in QuickTime, the WebGL Aquarium,
  YouTube at 1440p.
- Moved to thread 13: 4K playback in Safari (TODO 13 item 11), the WebGPU sample failures (item 12), the AV1 fallback
  and H.264 on VCN (item 13).
- Thread 09: a decoder location that keeps the system volume's seal (TODO 09).
