# Navi 32 port: feature-by-feature status against the RX 7900 XTX work

Every row is a piece of the XTX work, its state on the RX 7800 XT (Navi 32, `1002:747e`), and - when it is
working - the command that proves it. Measure a row before claiming it.

| # | Feature | State | Evidence / command |
|---|---|---|---|
| 1 | PSP firmware chain (24 binaries, TMR from `LOAD_TOC`, `SETUP_TMR`) | **works** | `navi32_up.sh`: `SETUP_TMR status 0` |
| 2 | SMU features (`EnableAllSmuFeatures`) | **works** | bringup log, no timeout |
| 3 | RLC | **works** | `phxctl regs 0x2004` / RLC `0xc000001f` |
| 4 | GCVM four-level page tables through the **MMHUB** instance | **works** | `ib8h` virgin-VA walk |
| 5 | MES firmware load and RISC-V start | **works** | `mesboot mestrap`, `INSTR_PNTR` advances |
| 6 | MES API frames complete | **works** | `mesboot kiqsched`: three frames `status=1 OK` - **the completion fence must be an MC address** |
| 7 | MES `ADD_QUEUE(gfx, map_legacy_kq)` | **works** | `mesboot gfxq`: `fence=4 OK` |
| 8 | gfx queue executes | **works** | ring test writes `scratch1=0xdeadbeef`, `testbuf=0x6f7a6f7a` |
| 9 | compute queue (MEC) executes | **works** | `mesboot computeq`: `ring test scratch0=0xdeadbeef` |
| 10 | both queues adopted by the accelerator | **works** | `ADOPT=1 ... gfxq` / `computeq`: `adopt -> kr 0x0` |
| 11 | client submission through the accelerator ABI | **works** | `submit_test_vram`: `target = 0x5eed0001`, fence retires |
| 12 | RADV device + compute | **works** | `vkbench compute`: 270k dispatches/4 s, 0 GPU errors |
| 13 | RADV bandwidth | **works** | `vkbench bandwidth`: 47.8 GB/s (read+write) |
| 14 | RADV submit latency | **works** | `vkbench submit`: 162 us wall, 51 us GPU |
| 15 | Metal device | **works** | `metaltest`: 1 device, `COMPUTE RESULT: 4096/4096 correct` |
| 16 | Metal workload | **works** | `metalbench compute`: 68502 frames/3 s, 0 errors |
| 17 | Core Image on the GPU | **works** | `apiprobe`: `CIContext ... (metal-DG 1)` |
| 18 | WindowServer composites on the GPU | **works** | daemon log: `SUCCESS ... accelerated desktop survived`, `debug.nvaccel_iop_ok` climbing |
| 19 | OpenGL on the GPU | **works** | `apiprobe` in the console session: `4.1 Metal - 90.5`, vendor `AMD` |
| 20 | OpenCL on the GPU | **works** | `apiprobe`: device `AMD Radeon RX 7800 XT (RADV NAVI32) (NVMTL over ...)` type GPU |
| 21 | VCN engine power-up + ring | **works** | `vcn_up.sh`: `M1 PASS`, ring test + decode create/destroy IB |
| 22 | VCN MMHUB VM | **works** | daemon log: `vcn: up, ring adopted, MMHUB VM on (mmvm 1 bad 0x0)` |
| 23 | VCN hardware decode (HEVC) | **works** | `vtdec_test clip.mp4 10 libphxdec_hevc.dylib`: 10 frames in 197 ms, `debug.phxmmvm_stat` `vcn submits` 6 -> 17, frame md5s identical to Apple software decode |
| 24 | Vulkan window surface | **works** | `tools/metaltest/vkwsi.m` against `libvulkan_radeon_wsi.dylib` (RADV built with `-Dplatforms=macos`, patch 0020): `VK_EXT_metal_surface` advertised, 640x360 FIFO swapchain, `presented 300/300 frames`, `WSI OK`. The default build enumerates only 8 extensions and no `VK_KHR_surface` - enumerate, never grep the binary |
| 25 | VideoToolbox bundle in `/System/Library/Video/Plug-Ins` | **not yet** | the decoder itself works (row 23); making VideoToolbox find it in other processes needs `tools/phxctl/sysvol_install.sh` (authenticated-root disabled) and VermilionTrust cdhash entries |
| 27 | Refresh-rate switch, live on the trained link | **works** | `debug.phxpipe0=4` with `debug.phxpipe0_mode`: 60/100/120/144/165 all reach `last=done`, `DP_VID_M` at the plan's value, desktop keeps compositing |
| 28 | DTO-aware `identify()` (rates that share OTG totals) | **works** | `phxP0PlanDto`/`planDto`: live `DP_DTO0_PHASE` picks the plan; the 1440p family carries all five rates |
| 29 | Connector found at run time (no build-time DIG) | **works** | `debug.phxpipe0_dig` reports the lit DP block (0 now, was 4) and the plan window is relocated onto it |
| 30 | DISPCLK never lowered (RedNavi's rule) | **works** | `W()`/`latchWait()` accept a faster divider; 144 -> 120 succeeds instead of latching dark |
| 31 | Dragging-window flicker under GPU present | **fixed** | `debug.phxscan_latchfix=1` by default (a latched front slot is never reused) |
| 32 | Resolution change (not just rate) | **works** | b165: `DSCL0_RECOUT_SIZE` (`0x041D2`) + `DSCL0_MPC_SIZE` (`0x041D3`) + `PhxAMDFB` `fFlipOk`/props sync; verified live across `1440p <-> 1080p60 / 1680x1050@60 / 1280x720@60 / 1080p50` (0 underflows) |
| 26 | upstream b126/b127 (fault page + recovery) | **ported** | wired into `VermilionBoot` b161 (`b9eba42`), default off (`debug.phxfaultdef=0`, `debug.phxrecover=0`), host-tested, unverified on hardware |
| 33 | Idle power gating (SDMA idle, GFX CG, idle DPM) | **works** | verified live on RX 7800 XT (2026-11-25): `sdma_idle` + `gfx_cg` + `idle_dpm.sh` (`pipe0@120` settle, `uclk 456 -> 96 MHz`) drop board power from **206–209 W -> 33–36 W** (socket `176 W -> 19 W`) |
| 34 | Warm-GPU refusal + auto-login first-run marker | **works** | verified live on RX 7800 XT (2026-11-25): `sOS check: C2PMSG_81=0x00000000 (cold)` + `first-run marker removed`, clean auto-login into accelerated desktop; `bringup_warm_test.sh` 5/5 |
| 35 | Opt-in HUBP0 hardware cursor (`debug.phxcursor=0`) | **ported** | b163 (`PhxCursor.h`, `phx_cursor.cpp`, `cursor_test` 14/14): live sysctl toggle & `ref2pix` tracking verified on RX 7800 XT (2026-11-25); default off |
| 36 | Display-sleep transaction drop (`debug.nvaccel_iop_sleepdrop=1`) | **works** | inspired by `AMDRadeonRDNA4.kext` `a6e9f37`: self-verified `+0x299` clear + drop while sleeping; live `pmset displaysleepnow` + wake kept `WindowServer` uninterrupted |
| 37 | Generic monitor EDID + dynamic DCN 3.2 modeset (`debug.phxpipe0_dyn=1`) | **works** | b164–b165 (`PhxDynPlan.h`, `dyn_plan_test` 463/463): auto-parsed all 24 progressive EDID modes (6 static + 18 dynamic), auto-downshifts 10 bpc -> 8 bpc on HBR2 x4 (2K 144/165 Hz lit cleanly), 14/14 live cross-resolution/rate switches `done` with `refused=0`, `fell=0`, zero underflow |
| 38 | Scaled HiDPI modes (`IOFBScalerInfo` + scaled timing validation) | **works** | `VermilionAccel` (`phxrotate.h`, `phxrotate_k.cpp`, `phxfb.cpp`, `nvrm-accel.cpp`): publishes `IOFBScalerInfo` (up to `5120x5120`), marks native-raster modes `kDisplayModeNativeFlag`, validates unrotated scaled HiDPI timings (`kDisplayModeNotPresetFlag \| kDisplayModeValidForHiResFlag`), verified live at `1920x1080 HiDPI` (`3840x2160` backing @ `165 Hz`) on a 2560x1440 monitor |
| 39 | macOS Screen Sharing (native & scaled/HiDPI resolutions) | **works** | `NVMTLDriver` (`nvmtl_vk.c` buffer-texture compatible view format list + `NVMTLObjects.m` separate `_swzView` for sampling while keeping identity `_ownView` / `_stex` for storage writes): fixes OpenCL `EncodeVectorized` (`RGBA8` view over `BGRA8` IOSurface) and `clImageHorizontalScale`/`clImageVerticalScale` writes through swizzled views |

The [November 27 graphics verification](../.agents/findings/2026-11-27-navi32-edge-and-plugin.md) records the verified Edge gradient coverage fix, the offline reproducer, and RX 7800 XT results for multisample transfers, buffer initialization and shader cache performance.

## Tests that need no card

```sh
sh scripts/test/host.sh        # 17 suites (2 mock + 15 C++), 0 failing on this tree
```

* the mock daemon harness (`tests/mock/run.sh`: **268 checks**; `tests/mock/bringup_warm_test.sh`: **5 checks**);
* 15 C++ suites: `asic` 23, `cursor` 14, `dpb_retrain` 69, `dyn_plan` (6 Linux captures + EDID/DisplayID + mock `macSet`), `edid_full` 38, `gputime` 15, `head0_1440` 83, `head0_dig4` 5 modes + rotation preflight, `p0odm` 690621, `pick_1440`, `pipe0_1440` 49, `scan_layout` 43, `vm_flat` 17, `vram_bo` 625, `vrr_ramp` 14.

`asic_test` guards the per-die values this port had to change (GC 11.0.3, `GB_ADDR_CONFIG` 0x444, the +0x10/+0x04
GC memory-hub shifts), and `vm_flat_test` guards the client flat-table walk (`va >> 12` indexing and its bounds
check). Both are silent-failure territory on the card, which is why they are tested here.

Every build script also runs on this tree - kext, accel, plugin, translator, tools, metaltest, metalbench, vkbench,
vkwsi - so a fresh checkout is known to produce all of it (see `docs/NAVI32.md`).

## Reproducing rows 1-22 from a cold boot

```sh
cd ~/gpu/phxctl && sudo env ASIC=navi32 bash navi32_up.sh    # bring-up + queues + VCN + vkbench
sudo kmutil load -p /Library/Extensions/VermilionAccel.kext  # if the daemon did not
sudo /usr/local/sbin/mesboot claim                           # publish the accelerator
sudo sysctl -w debug.phxgl=1                                 # OpenGL over Metal (or the phxboot-gl flag)
```

The boot daemon does all of this on its own once it is installed and `phxfb-squat` is armed; rows 18-20 also
need a console login, because WindowServer and OpenGL only create their contexts for a real session.