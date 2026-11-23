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

- `apiprobe`: Metal device "AMD Radeon RX 7900 XTX (RADV NAVI31)", Metal 3 families, 6 GiB working set; OpenGL only
  "Apple Software Renderer" (no accelerated pixel format); OpenCL only the CPU; Core Image on Metal; MetalFX spatial
  and temporal supported.
- Quick Look vs `sips` (`Mac Blue.heic`, 512 px, warm): Quick Look 1.00-1.19 s (n=6), `sips` 1.01-1.05 s (n=5). The
  12.2 s of 2026-11-23 was the cold first run; no gap. (The file is 9.3 MB now; the inventory noted 21-25 MB, likely a
  different file.)

## Not done in this session (owner away, no restart, no GPU load)

Zink / OpenGL for LWJGL apps, OpenCL on the GPU per app (rusticl), Safari playback and WebGL / WebGPU by eye, Photos
and video wallpaper by use, the Fable clean-up pass.
