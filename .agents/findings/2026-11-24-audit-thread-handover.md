# 2026-11-24: the last threads' state (phase 1 audit, step 1)

Megamind 1 asked threads 02, 03 and 04 for their state before archiving them. Their answers, condensed. All
hardware claims are on the XTX machine (macOS 26.7.1, EFI change 33).

## 04: DP / HDMI audio, HDMI output

- **Works, verified:** DP audio to the LG after every boot (VermilionAudio 1.7.2, `phxaudio` programs the display
  engine's audio from the EDID like Linux DC: 90/90 writes match the Linux trace; daemon step `audio_boot`, flag
  `~/gpu/phxboot-audio`). The owner tested music, volume, switching outputs, 240/144/240 Hz while playing.
- **Auto-login fix** (`LWRAN`, commit 8502256) is on the branch and checked over several boots. The installed
  `~/gpu/phxboot-navi31.sh` equals the branch copy (checked again by the megamind).
- **Open:** HDMI output (nothing built; the capture script is DP-only and needs one Linux session with the LG on
  HDMI); audio after a DP replug (blocked by the black screen after a replug) and after sleep; DDC/CI brightness and
  monitor volume (owner request); an AppleGFXHDAEngineOutputDP assertion (PhxAMDFB doesn't answer `'aud '`,
  harmless); the link gate passed a kext that can't load from OpenCore (1.7.0 linked IOGraphicsFamily, which is in
  the System KC); docs for EFI changes 24-33 in PROJECT_KNOWLEDGE and decisions.md.
- **Machine-only:** `~/gpu/phxaudio` (built from branch source), the flag, `~/vermilion-rollback/` (change-30 EFI copy
  with serials, the pre-audio daemon), `~/vermilion-captures/thread04-*` (sink data, stays out of git).
- **Cleaned up by the megamind:** commit 3e88ad5 pushed; the debug LaunchAgent `local.t04watch` (0.5 s ioreg/sysctl
  logger) stopped, its plist moved to `~/vermilion-rollback/`.

## 03: Graphics APIs and video

- **Works, verified 2026-11-23:** VCN 4.0 at every boot (`phxboot-vcn`); HEVC 8/10-bit and AV1 hardware decode through
  VideoToolbox (bit-exact against Apple's decoder and dav1d); WebGL 1/2 and WebGPU in Safari; OpenGL 4.1 on the GPU
  (Apple's AppleMetalOpenGLRenderer, `debug.phxgl`, `phxboot-gl`; Minecraft 26.3 runs); OpenCL on the GPU; Vulkan
  window surface (Mesa patch 0020, opt-in loader in `/usr/local/lib/vermilion`).
- **Open, with the suspected cause:**
  1. 4K AV1 at ~13.3 ms per frame (decode 6.45 + VCN wait 4.03 + copy 2.81, spikes to 64 ms): one frame at a time,
     blocking fence, a copy into the IOSurface. Glitches: output IOSurfaces from a small pool reused while still on
     screen (unverified). Fix: frames in flight, decode straight into a larger IOSurface pool.
  2. WebGPU render bundles crash Safari's GPU process: `newIndirectCommandBufferWithDescriptor` returns nil, the
     translator lacks `air.draw_indexed_primitives_render_command`.
  3. WebGPU shader failures (21 of 43 samples): "unknown SSA value" (casts / helper results not emitted), "graph walk
     opcode=tail", an `ImageQuerySizeLod` type and a sampler operand.
  4. Black textures from `copyExternalImageToTexture` / ImageBitmap (suspected; unverified).
  5. VP9 stopgap via a lower `VTMaxPlaybackLevel` (unverified that VideoToolbox reads it; system volume).
  6. AV1 fallback fails with -12906 instead of Apple's SW decoder; H.264 on VCN not built. Both are bundle changes
     (system volume), so do them in one change.
- Nothing unpushed. The local branch had been reset to the stale 7b30f2b outside the thread; the worktree is removed.

## 02: GPU+CPU performance and power

- Nothing unpushed (all in #16). Its worktree is removed.
- **Stability and correctness bugs for phase 2:**
  1. Boots that stop with no macOS log: two so far (cold boot b155; warm restart b159 with OpenCore's "Missing
     compatible GOP"). Cause unknown: the 1 GiB BAR (`ResizeAppleGpuBars` 10), b159, or warm restarts.
  2. A GPU hang in clpeak's MPS / CoreML pass (finding `2026-11-23-clpeak-hang.md`): any MPS / CoreML app could hit it.
  3. `gpuSubmit` uses `fbAper` without the null check `gpuTimeAccount` has: a possible kernel panic (unverified).
  4. `gpuTimeReset` also retires the VCN queue, which the other callers skip.
  5. `efi/build.py build` without `--stage` builds `vermilion-trust` with a 256 MiB BAR, not the live
     `vermilion-trust-bar1g`: a plain rebuild drops the CPU-visible VRAM pool.
  6. The tiled drawable is unverified on Chromium / Electron windows (off switch `drawable-tiled-off`).
  7. ocvalidate warns the 1 GiB BAR quirk is "unstable with macOS sleep-wake".
- **Performance ranking changes it proposes (phase 4):** M3 plus the faster buffer clear first (apps not on the list
  get 43.7 GB/s instead of ~840); the Linux twins second; per-submit cost (#7a) up; the direct Vulkan ICD (#6) as the
  main fix for MoltenVK apps, multi-draw (#5) only as an interim; drop the CPU `_CST` item (#7b) or make it a known
  limit.

## Repo housekeeping done

- Removed the clean, merged worktrees of 02, 03 and 07 and their local branches.
- Kept: `~/Projects/vermilion` (the main checkout that holds `.git` for all worktrees; stale main at 7b30f2b, never
  install from it) and the local branches `worktree-agent-a264…`, `worktree-agent-a98b…` (not in main; old).
- Fixed `.claude/hooks/machine-lock`: the `esac` was missing since 3ca13c0, so the script didn't parse.
