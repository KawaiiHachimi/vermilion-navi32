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
> **The `navi32` branch of this fork adds RX 7800 XT support** (Navi 32, PCI `1002:747e`, 16 GiB): GC 11.0.3,
> MP0/MP1 13.0.10, SDMA 6.0.3, `GB_ADDR_CONFIG` 0x444, and the board's monitor on OTG0 + DIG4 + DP4 (HPD 4).
> `ASIC=navi32 scripts/build/kext.sh` builds it; the reference driver is unchanged under the default `ASIC=navi31`.
> What is done, what is still missing and how to boot it: **[docs/NAVI32.md](docs/NAVI32.md)**. The 7900 XT/XTX
> path below is untouched by this branch.
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
| **Verified on hardware** | PSP firmware chain (24 binaries, TMR from `LOAD_TOC`, `SETUP_TMR status=0`) · SMU features (`EnableAllSmuFeatures` OK) · RLC (`0xc000001f`) · GCVM page tables (four-level, MMHUB instance) · display (EDID, DP4 link, six timing plans for 1080p and 1440p) · **PM4 execution** on the KIQ and on a MEC compute queue · indirect buffers (VRAM and system memory) · `RELEASE_MEM` fences · **client submission through the accelerator's own ABI** (`submit_test` reaches `target = 0x5eed0001`) · **the MES** (all API frames complete) · **a gfx queue created by the MES** that executes and is adopted by the accelerator · **RADV**: 270k compute dispatches in 4 s with 0 GPU errors, 47.8 GB/s buffer bandwidth, 162 us per waited submit |
| **In progress** | **Metal.** RADV runs (above), so the next step is the Metal stack on top of it: the `NVMTLDriver` plugin, the `libnvmtl_translate` shader translator, the `VermilionAccel` accelerator kext and the boot daemon that moves WindowServer onto the GPU. The port has not yet loaded any of them |
| **Not started** | Metal plugin (`NVMTLDriver`), WindowServer, OpenGL / OpenCL, Vulkan window surface, VCN decode. All of these are the Navi 31 tree's existing work, waiting for the bottom of the stack |

### The fixes that took the port from "no signal" to executing commands

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

### What is left

| Step | Blocks |
|---|---|
| GFX queue (`CP_GFX_HQD_*`, ring base in the MQD) | RADV's submissions - it emits graphics-level packets even for a compute workload |
| Metal plugin + translator on top of RADV | WindowServer, apps' Metal device |
| WindowServer / OpenGL / OpenCL | the accelerated desktop |
| Vulkan window surface (`RADV_PLATFORMS=macos`) | apps that present through Vulkan |
| VCN decode | HEVC / AV1 in VideoToolbox |

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
