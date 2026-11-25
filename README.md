<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/brand/vermilion-logo-dark.svg">
    <img alt="Vermilion" src="assets/brand/vermilion-logo-light.svg" width="480">
  </picture>
</p>

# Vermilion

**A GPU-accelerated macOS desktop on the AMD Radeon RX 7900 XT (Navi 31, RDNA 3).**
Vermilion is a display and acceleration driver for macOS Tahoe 26.7.1: two kernel extensions (this fork adds a third,
VermilionTrust), a Metal driver plugin running on Mesa RADV, and the boot-time bring-up that powers the GPU up before
WindowServer opens the displays.

> [!NOTE]
> **The `navi32-port` branch of this fork adds RX 7800 XT support** (Navi 32, PCI `1002:747e`, 16 GiB): GC 11.0.3,
> MP0/MP1 13.0.10, SDMA 6.0.3, `GB_ADDR_CONFIG` 0x444, dynamic EDID/DisplayID modeset (`b165`) across any DisplayPort
> connector, and idle power gating (**206 W -> 33 W**).
> `ASIC=navi32 scripts/build/kext.sh` builds it; the reference driver is unchanged under the default `ASIC=navi31`.
> What is done, what is still missing and how to boot it: **[docs/NAVI32.md](docs/NAVI32.md)**, the feature-by-feature
> state in **[docs/NAVI32-STATUS.md](docs/NAVI32-STATUS.md)**, and a clean-install runbook in
> **[docs/NAVI32-INSTALL.md](docs/NAVI32-INSTALL.md)**. The 7900 XT/XTX path below is untouched by this branch.
>
> **This fork ([ownback/vermilion-xtx](https://github.com/ownback/vermilion-xtx)) adds RX 7900 XTX support** (revision `0xC8`,
> one 2560x1440 DisplayPort monitor). **The RX 7900 XT keeps working:** XTX values are added next to the XT's and
> chosen by what the hardware reports, never in place of them. On the XTX machine
> ([REFERENCE-MACHINES.md](docs/REFERENCE-MACHINES.md)): an accelerated desktop at 2560x1440 up to 240 Hz, Variable
> refresh and HDR ([docs/XTX.md](docs/XTX.md)); its full state, verified item by item, is in
> [docs/PROJECT_KNOWLEDGE.md](docs/PROJECT_KNOWLEDGE.md). Plan: [ROADMAP.md](ROADMAP.md). How the stack works:
> [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). Upstream is [leolivm/vermilion](https://github.com/leolivm/vermilion).

> [!IMPORTANT]
> Vermilion is specific to **one board**: an RX 7900 XT (PCI `1002:744c`, revision `0xcc`) on the reference machine,
> with the firmware revisions listed in `tools/firmware/fw.lock`. Register plans, watermarks and display timings were measured
> on that card and its two monitors. Other Navi 31 cards, other RDNA 3 GPUs and other macOS versions are untested and
> will most likely not work as is.

> [!WARNING]
> Vermilion is independent and unofficial. It is **not affiliated with, reviewed, endorsed or supported by** NullMoth
> Systems, luuumi-dev (nullarc), AMD, Apple or any other project named in [CREDITS.md](CREDITS.md). Please do not ask
> them for support with it.

## Status

| | |
|---|---|
| **Works** | Metal acceleration (WindowServer composites on the GPU; apps get the Metal device) · 144 Hz and 165 Hz refresh · two monitors at once (DisplayPort + HDMI) · hot-plug · display rotation |
| **Not yet** | Sleep / wake · HDMI / DisplayPort audio from the GPU · a third or fourth monitor (see [Roadmap](#roadmap)) |

Verified on the reference machine (November 2026) with the kexts under their Vermilion names: `VermilionBoot.kext`
injected by OpenCore and `VermilionAccel.kext` loaded from the Auxiliary Kernel Collection give the accelerated
desktop on two monitors at 144 Hz and 165 Hz. The kexts on that machine were built from this tree.

## Navi 32 (RX 7800 XT) port

Vermilion was written for Navi 31 (RX 7900 XTX). This branch ports it to **Navi 32** - PCI `1002:747e`, the
Radeon RX 7800 XT with 16 GiB - on a reference machine with one 2560x1440 DisplayPort monitor. The port's live
record, with every fix, every ruled-out hypothesis and the register values measured on the card, is
[docs/NAVI32.md](docs/NAVI32.md).

**Goal: the same completeness as the Navi 31 work** - gfx and compute queues, RADV and Metal, WindowServer,
OpenGL / OpenCL, and VCN decode - reached step by step, keeping this section current.

### Where the port stands

| | |
|---|---|
| **Verified on hardware (RX 7800 XT)** | PSP firmware chain (24 binaries, TMR from `LOAD_TOC`, `SETUP_TMR status=0`) · SMU features (`EnableAllSmuFeatures` OK) · RLC (`0xc000001f`) · GCVM four-level page tables (MMHUB instance) · MES + gfx & compute queues adopted by `VermilionAccel` · **RADV** (270k compute dispatches/4 s, 47.8 GB/s bandwidth, 162 us submit) · **Metal 3 & Core Image** · **WindowServer GPU compositing** + clean auto-login (`loginwindow.didRunThisBoot` cleared) · **OpenGL 4.1 & OpenCL** · **Vulkan window surface** (`WSI OK`, 300/300 frames) · **VCN hardware HEVC decode** (md5-identical to Apple software decode) · **Dynamic EDID/CTA-861/DisplayID modeset (`b165`)**: auto-parses up to 32 modes from any connected DP monitor, auto-downshifts `10 bpc -> 8 bpc` on HBR2 links (all 24 modes including 2560x1440 @ 60/100/120/144/165 Hz and cross-resolution 1080p/1680x1050/720p switches verified with 0 underflows) · **Display-sleep transaction drop** (`debug.nvaccel_iop_sleepdrop=1`, `WindowServer` uninterrupted across display sleep/wake) · **Idle power gating**: `sdma_idle` + `gfx_cg` + `idle_dpm.sh` (`UCLK 456 -> 96 MHz`) drop idle board power from **206–209 W down to 33–36 W** · **Warm-GPU refusal** before register writes |
| **Ported / opt-in** | Upstream b126/b127 shader fault page & queue recovery (`debug.phxfaultdef=0`, `debug.phxrecover=0`, default off) · Opt-in HUBP0/DPP0 hardware cursor plane (`b163`, `debug.phxcursor=0`, default off) · Opt-in DP reconnect replay (`debug.phxhotplug0=3`) · 90-degree rotation preflight on published modes |
| **Remaining** | VideoToolbox system plug-in bundle install (`/System/Library/Video/Plug-Ins`, needs `sysvol_install.sh` + VermilionTrust cdhash) · Full S3 system sleep/wake · GPU DisplayPort/HDMI audio · Second monitor bring-up |

Host regression tests (no card required): `sh scripts/test/host.sh` — **17/17 suites passing** (`tests/mock/run.sh` 268 checks, `tests/mock/bringup_warm_test.sh` 5 checks, and 15 C++ kext suites including `dyn_plan_test` 463 checks).

### The fixes that took the port from "no signal" to accelerated desktop

1. **TMR** - taken from the `LOAD_TOC` response (size `0xa700000`) and placed in this board's reserved window; a
   constant made `SETUP_TMR` fail and every later step depend on it.
2. **GCVM block shift** - GC 11.0.3 moves the context/aperture block by +0x10 dwords against GC 11.0.0. Without
   it the context registers were written to addresses that do not exist.
3. **Address spaces** - context page-table bases are VRAM physical, MQD and `MAP_QUEUES` addresses are MC, ring
   bases and writebacks are GPU VAs. The port had VAs where MC was required.
4. **The EOP buffer** - `hqd_program` programmed everything except `CP_HQD_EOP_BASE_ADDR/_HI/_CONTROL`, so the
   `RELEASE_MEM` that ends every submission had nowhere to report and hung the CP.
5. **Queue stop before reprogram** - reprogramming a live HQD wedges the CP; the port now dequeues, polls
   `CP_HQD_ACTIVE` clear, re-arms and then hands the queue over.
6. **The MMHUB instance** - the CP fetches through the MMHUB instance of the VM block (0x1A080), not the
   GC-segment one (0x28E8) the port was programming. Four-level tables translate there; they never did at the
   other instance, which is what made the whole depth-3 question look unsolvable.
7. **Dynamic EDID modeset, DSCL0 geometry & HBR2 8-bpc downshift (`b164–b165`)** - parses any connected monitor's
   EDID/CTA-861/DisplayID into DCN 3.2.0 plans, programs `DSCL0_RECOUT_SIZE` (`0x041D2`) and `DSCL0_MPC_SIZE`
   (`0x041D3`) alongside `HUBP0` viewports (fixing cross-resolution striped corruption & HUBP0 underflow), and
   automatically downshifts `10 bpc -> 8 bpc` (`FMT0 = 0xc900`) when a high-refresh mode (e.g. 1440p @ 144/165 Hz)
   exceeds HBR2 x4 bandwidth at 10 bpc.
8. **Idle power gating** - emits the bring-up completion banner so `sdma_idle` + `gfx_cg` run before `claim`, and
   lets `idle_dpm.sh` settle via `debug.phxpipe0_stat` to unpin `UCLK` (`456 -> 96 MHz`), cutting idle board power
   from **206 W to 33 W**.

### Reproducing the current state

```sh
tools/firmware/fetch_firmware.sh                         # AMD microcode (navi32 set)
ASIC=navi32 sh scripts/build/kext.sh                     # VermilionBoot.kext
ASIC=navi32 sh scripts/build/accel.sh                    # VermilionAccel.kext
# deploy both to the ESP / AuxKC, reboot, then:
sudo phxctl --help                                       # MMIO, VRAM, doorbell access
cd ~/gpu/phxctl && sudo env ASIC=navi32 bash navi32_up.sh   # the whole chain, then vkbench
# or step by step:
sudo mesboot mesfull && sudo mesboot gart                # MES running, GART up
sudo mesboot computeq                                    # MEC queue ring test
sudo mesboot ibtest                                      # indirect buffer
sudo mesboot ibtest3                                     # RELEASE_MEM fence
sudo env ADOPT=1 mesboot computeq                        # hand the queue to the kext
sudo ~/gpu/submit_test_vram                              # target = 0x5eed0001
```

`mesboot` carries the port's whole diagnostic set (`ibtest`, `ibtest2`, `ibtest3`, `ibsys`, `ib8`, `ib8h`,
`ib8hs`, `ctx8`, `computeq`, `me0q`, `uconfig`, `hqdscan`, `scan`, `mesfull`, `gart`); `docs/NAVI32.md`
explains what each one proved.
## What is in here

| Component | What it does | Source |
|---|---|---|
| **Boot kext** — `VermilionBoot.kext` | Injected by OpenCore. Starts the GPU (loads AMD's firmware, power management, graphics engines, GPU memory), drives the monitor outputs (DisplayPort and HDMI, monitor detection, refresh rates, hot-plug) and gives the Vulkan driver its access to the card | [`src/vermilion-boot`](src/vermilion-boot) |
| **Accelerator kext** — `VermilionAccel.kext` | Installed in `/Library/Extensions` (Auxiliary Kernel Collection). The part macOS sees as the graphics card: one framebuffer per monitor, the accelerator WindowServer and apps draw through, and display control | [`src/vermilion-accel`](src/vermilion-accel) |
| **Metal plugin** — `NVMTLDriver` | The Metal driver WindowServer and apps load. It runs Metal on top of Vulkan (RADV) | [`src/metal-plugin`](src/metal-plugin) |
| **Shader translator** — `libnvmtl_translate.dylib` | Converts Metal shaders to Vulkan (SPIR-V) for the plugin (LGPL-3.0-or-later) | [`src/translator`](src/translator) |
| **Vulkan driver** — RADV (`libvulkan_radeon.dylib`) | Mesa's AMD Vulkan driver with a macOS backend, built from Mesa with this patch series | [`patches/mesa`](patches/mesa) |
| **Video decoder** (optional) — `libphxdec_hevc.dylib`, `PhxVideoDecoder.bundle` | Hardware HEVC / AV1 decoding for VideoToolbox, through FFmpeg's Vulkan video path | [`src/video-decoder`](src/video-decoder), [`patches/ffmpeg`](patches/ffmpeg) |
| **Video acceleration bundle** — `PhxVA.bundle` | The bundle macOS's video acceleration (AppleGVA) looks for on this card. A placeholder for now: it declines every request, so VideoToolbox falls back to its software decoders | [`src/video-accel`](src/video-accel) |
| **Boot daemon and tools** — `phxboot-navi31.sh`, `phxctl`, `mesboot` | A LaunchDaemon that runs at every boot: powers the GPU up and moves WindowServer onto it, with optional steps (second monitor, refresh rate, display power settings) switched on by flag files. If a step fails, the next boot falls back to the firmware framebuffer. Plus the command-line tools and scripts it runs | [`tools/phxctl`](tools/phxctl) |
| **Metal check** — `metaltest` | The small program the boot daemon runs to confirm that apps get a working Metal device (it also runs a compute kernel on the GPU) | [`tools/metaltest`](tools/metaltest) |
| **Firmware fetcher** | Downloads the AMD microcode from linux-firmware at a pinned commit, checks every file, and generates the one header the boot kext is built with | [`tools/firmware`](tools/firmware) |
| Build scripts | One script per component, no machine-specific paths | [`scripts/build`](scripts/build) |
| Tests | Hardware-free tests of the boot daemon and the scripts it runs | [`tests/mock`](tests/mock) |
| Documentation | [INSTALL](docs/INSTALL.md), [BUILD](docs/BUILD.md), [FIRMWARE](docs/FIRMWARE.md), [EDID](docs/EDID.md), [NAMING](docs/NAMING.md) (why classes, sysctls and tools keep their `phx` names) | [`docs/`](docs) |

## Building

See [docs/BUILD.md](docs/BUILD.md). In short, on an Intel or Apple Silicon Mac with the Command Line Tools:

```sh
tools/firmware/fetch_firmware.sh                        # AMD microcode from linux-firmware into ~/gpu/fw/navi31
MKSDK=/path/to/MacKernelSDK scripts/build/kext.sh       # VermilionBoot.kext
MKSDK=/path/to/MacKernelSDK scripts/build/accel.sh      # VermilionAccel.kext
scripts/build/translator.sh                             # libnvmtl_translate.dylib (needs Rust)
scripts/build/tools.sh && scripts/build/metaltest.sh    # phxctl, mesboot, metaltest
bash tests/mock/run.sh && bash tests/mock/b15test.sh    # boot daemon and display-power mocks
```

## Installing

There is no installer yet: the kexts are installed the way other macOS kexts are. The guide is
[docs/INSTALL.md](docs/INSTALL.md) (expert, manual). The goal of this fork is a release you install by dropping one kext
into `EFI/OC/Kexts` ([ROADMAP.md](ROADMAP.md)).

## License

**Source-available, with noncommercial parts.** Code written for Vermilion is MIT. `VermilionAccel` (`src/vermilion-accel`) and the
Metal plugin (`src/metal-plugin`) are derived from NullMoth's driver and are under the **PolyForm Noncommercial License
1.0.0**: no commercial use. Files from nullarc keep nullarc's MIT-or-GPL-2.0 terms, material from Linux amdgpu keeps
AMD's MIT notice, the shader translator (`src/translator`) is LGPL-3.0-or-later, and the Mesa and FFmpeg patches
follow their projects. Details: [LICENSE](LICENSE),
[NOTICE](NOTICE), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), [LICENSES/](LICENSES).

No AMD firmware, VBIOS or Apple software is distributed with this repository.

## Credits

Vermilion is built on NullMoth's macOS driver and its shader translator (metal2vulkan), nullarc, Cobalt64, RX4Darwin, RedNavi, Linux amdgpu, Mesa, FFmpeg and
Acidanthera's tools: see [CREDITS.md](CREDITS.md) for what came from each.

## Roadmap

This fork's plan, thread by thread, with the known limits: [ROADMAP.md](ROADMAP.md). Upstream's list:

- **Sleep and wake.** Today the machine has to be kept awake ([INSTALL.md](docs/INSTALL.md#10-known-limits)).
- **HDMI and DisplayPort audio** from the GPU.
- **A third and fourth monitor** as macOS displays.
