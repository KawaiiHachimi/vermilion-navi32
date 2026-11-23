# CLAUDE.md: working on Vermilion

This file is for coding agents (Claude Code, Codex, others: `AGENTS.md` links here). Humans should start at
[`docs/README.md`](docs/README.md).

## What this is

Vermilion is a macOS Tahoe 26.7.1 display and acceleration driver for AMD Navi 31 (RDNA 3). It has two kexts, a
Metal plugin that runs on Mesa RADV, and a boot daemon that powers the GPU up from userspace.

Upstream is [leolivm/vermilion](https://github.com/leolivm/vermilion), built for one RX 7900 XT (PCI `1002:744c`,
rev `0xCC`) with two 1080p monitors. This fork, [ownback/vermilion](https://github.com/ownback/vermilion), has these goals:

1. **A fully working, stable macOS on one machine:** RX 7900 XTX (rev `0xC8`), one 2560x1440 DisplayPort monitor at
   up to 240 Hz, an ASUS B650E-I board and a Ryzen 9 7900X3D. That means the GPU driver plus a complete OpenCore EFI
   (Wi-Fi, Bluetooth, Ethernet, audio, USB). The machine is described in
   [`docs/REFERENCE-MACHINES.md`](docs/REFERENCE-MACHINES.md).
2. **Later, the same macOS as a VM** with the dGPU passed through (VFIO), while Linux keeps the iGPU.
3. **Clean, documented, reusable code**, so the community can take over afterwards. Changes for this machine must
   not break the upstream XT setup.

**Start at [`ROADMAP.md`](ROADMAP.md)** (the goal, the numbered thread series and its rules, the known limits) and
**[`docs/PROJECT_KNOWLEDGE.md`](docs/PROJECT_KNOWLEDGE.md)** (what is true now: machine state, known-good
configuration, dependencies, performance record). Open tasks by thread: [`.agents/TODO.md`](.agents/TODO.md); tests
only the owner can do: [`.agents/OWNER-TESTS.md`](.agents/OWNER-TESTS.md).

## Read before working

| You are about to... | Read |
|---|---|
| understand the stack | [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), [`docs/GLOSSARY.md`](docs/GLOSSARY.md) |
| touch anything XTX-specific | [`docs/XTX.md`](docs/XTX.md), [`.agents/findings/`](.agents/findings/) |
| install, boot or deploy | [`docs/INSTALL.md`](docs/INSTALL.md), [`efi/README.md`](efi/README.md), [`.agents/runbooks.md`](.agents/runbooks.md) |
| build | [`docs/BUILD.md`](docs/BUILD.md) |
| rename anything with `phx`, `nv` or `nmx` in it | [`docs/NAMING.md`](docs/NAMING.md) (mostly: don't) |
| make a design choice | [`.agents/decisions.md`](.agents/decisions.md) |

## Layout

| Path | What | License |
|---|---|---|
| `src/vermilion-boot/` | `VermilionBoot.kext`, injected by OpenCore. GPU register access, bring-up helpers, DCN display pipes, DMUB, GPU VM, the `N48N` user client RADV talks to | MIT (nullarc files: MIT or GPL-2.0; see [`LICENSE`](LICENSE)) |
| `src/vermilion-accel/` | `VermilionAccel.kext`, loaded from the AuxKC. `PhxAMDFB` framebuffer, `NMXAccel` accelerator, `PhxAGDC` | **PolyForm Noncommercial** |
| `src/vermilion-trust/` | `VermilionTrust.kext`, injected by OpenCore after Lilu: AMFI stays on, only the cdhashes of the installed Vermilion code pass library validation (replaces `amfi=0x80`); rebuilt with the EFI after every plugin / translator / RADV install | MIT |
| `src/metal-plugin/` | `NVMTLDriver`, the Metal driver WindowServer and apps load; Metal on Vulkan | **PolyForm Noncommercial**, all of it, including the files marked `Our code` |
| `src/translator/` | `libnvmtl_translate.dylib`, Metal AIR to SPIR-V (Rust) | LGPL-3.0-or-later |
| `src/video-decoder/`, `src/video-accel/` | HEVC/AV1 decode via FFmpeg Vulkan (optional); the PhxVA placeholder | MIT / LGPL deps |
| `patches/mesa/` | RADV Darwin backend, on Mesa `f5cb8ee0` | MIT |
| `tools/phxctl/` | boot daemon `phxboot-navi31.sh`, bring-up scripts, `phxctl`, `mesboot`, display power scripts | MIT |
| `tools/firmware/` | pinned linux-firmware fetcher (`fw.lock`) | MIT |
| `tools/capture/` | Linux-side capture tools: `gpuinfo.c`, `linux_display_capture.py` (mode-set traces), `linux_pci_capture.sh`, and the replay-plan generators (`p0odm_from_trace.py`, `p0plan_from_regs.py`) | MIT |
| `tools/linux/` | the Linux reboot guard (a Linux reboot goes back to Linux; only a power-off reaches macOS) | MIT |
| `tools/metaltest/` | `metalbench`, `apiprobe`: Metal performance and what each graphics API reports | MIT |
| `tools/sensors/`, `tools/storage/`, `tools/audio/`, `tools/usbmap/`, `tools/validation/` | read-only measurement tools: CPU temperature / power / clock, NVMe benchmarks, CoreAudio probes, the USB port walk, a per-boot report (`bootcheck.sh`) | MIT |
| `scripts/build/` | one build script per component | MIT |
| `efi/` | reproducible OpenCore EFI builder: pinned components, machine profiles, stages ([`efi/README.md`](efi/README.md)) | MIT |
| `tests/mock/`, `tests/kext/`, `tests/plugin/` | hardware-free tests: the daemon and scripts (mocks), kext logic and replay plans on the host, plugin helpers | MIT |
| `.claude/skills/` | drafts of the `ryzentosh-pre-install` / `ryzentosh-post-install` skills (move to `ownback/ryzentosh` in thread 10) | MIT |
| `docs/` | documentation for humans and LLMs | |
| `.agents/` | agent workspace: TODO, findings, decisions, runbooks | |

## Building and testing

- **Every build needs macOS** with the Command Line Tools (x86_64). Builds run on the XTX machine's own macOS since
  2026-11-22 ([`.agents/runbooks.md`](.agents/runbooks.md), "Building on the Mac itself"); the macbox VM is an
  alternative. Linux can't build the kexts. `efi/build.py` runs on both.
- **A plugin, translator or RADV install is also a VermilionTrust + EFI change** (new cdhashes):
  `PHXTRUST_KEEP=1 sh scripts/build/trust.sh`, rebuild and install the EFI, one boot
  ([runbooks](.agents/runbooks.md), "Deploying a plugin, translator or RADV build").
- Linux *can* run the mock tests, but today they hardcode `/private/tmp` and BSD tools (`stat -f`, PlistBuddy, ...).
  Making them portable is on the TODO list. Until then, run `bash tests/mock/run.sh` and
  `bash tests/mock/b15test.sh` on macOS before deploying a daemon or script change.
- After a kext change: bump `PHX_KEXT_BUILD` (`src/vermilion-boot/PhxBringup.cpp`) and add a line at the top of
  `src/vermilion-boot/CHANGELOG.md`. Nothing enforces it (`kext.sh` doesn't check): do it by hand.
- On Linux, the XTX is live under amdgpu. Read-only queries (libdrm ioctls, sysfs) are fine. Register dumps and traces
  need root: ask the user, never guess a password.

## Hard rules

1. **Hardware safety.**
   - When going Linux -> macOS, the user powers off fully, never a warm reboot. The bring-up loads SMU firmware
     once and assumes a cold GPU.
   - Never add an SMU, PSP or DAL message, or a register write, that Linux amdgpu doesn't send on this ASIC.
   - Never flash or write the VBIOS.
   - Keep the bring-up watchdog and the boot daemon's fallbacks (kill switch, pending mark, GOP revert).
   - Make one EFI change per boot, and keep a rollback copy of every replaced binary.
2. **Detect, don't overwrite.** Board-specific values (XT vs XTX, monitor timings, VRAM size) are chosen by what the
   hardware reports: PCI revision, a register read, VBIOS tables, EDID. Never replace an XT value with an XTX value;
   add the XTX case next to it.
3. **No AMD firmware, VBIOS dumps, register captures with serials, monitor EDIDs with serials, or OpenCore
   `config.plist` with SMBIOS data in git.** A built EFI (`build/efi/`) contains the machine's serials: never commit
   or publish it. `.gitignore` covers most of these; check `git status` before committing.
4. **No personal data in commits, docs or logs:** serials, MLB, ROM, UUIDs, MAC addresses, e-mail, disk or partition
   UUIDs.
5. **Respect the license boundaries.**
   - Code derived from NullMoth stays PolyForm Noncommercial.
   - Changes to the translator stay LGPL.
   - New files start with an SPDX header (MIT unless derived from something else).
6. **Keep the `phx`/`nv`/`nmx` names** of classes, sysctls, NVRAM variables, paths and the ABI ([`docs/NAMING.md`](docs/NAMING.md)).
   The Mesa patches, the daemon and the tools depend on them.
7. **Say what is verified.** Docs and comments follow upstream's style: plain statements, and anything not tested on
   hardware is marked `(unverified)`. A claim about hardware behaviour names the machine it was seen on.

## How we work

- Branches are `ownback/<topic>`. Remotes: `origin` = leolivm upstream (read-only for us), `fork` = ownback/vermilion.
  PRs go to `fork/main`. Changes useful to the XT go upstream later as separate PRs.
- Commit or push only when the user asks.
- When you finish or discover something:
  - tick or add items in [`.agents/TODO.md`](.agents/TODO.md);
  - write a dated note in [`.agents/findings/`](.agents/findings/) for investigations;
  - append design choices to [`.agents/decisions.md`](.agents/decisions.md).
- Keep diffs focused: a refactor and a behaviour change go in separate commits.
- Simulators, macbox and phone builds: follow the user's global instructions (`mac-sim`, `macbox`). The macOS VM is
  only a build host here. It has no AMD GPU, so it can't test the driver.
- The work runs as one numbered thread at a time ([`ROADMAP.md`](ROADMAP.md), rules 1-12): each thread branches from
  `fork/main`, opens a PR, merges with "Merge #N: ...", and the finishing thread starts the next one.
