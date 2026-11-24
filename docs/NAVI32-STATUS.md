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
| 26 | upstream b126/b127 (fault page + recovery) | **not yet** | build numbers collide with the xtx tree; needs renumbering past 160 and a boot test - see NAVI32.md |

## Tests that need no card

```sh
sh scripts/test/host.sh        # 12 suites, 0 failing on this tree
```

* the mock daemon harness: **260 checks** on `phxboot-navi31.sh` (flags, guards, autologin, VCN gating, rollback);
* 11 C++ suites: asic 23, dpb_retrain 69, edid_full 38, gputime 15, head0_1440 83, p0odm 690621, pipe0_1440 45,
  scan_layout 43, vm_flat 17, vram_bo 625, vrr_ramp 14.

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