# Roadmap

Where this project is going, in what order, and the rules every work thread follows. One line of status per thread;
tasks live in [`.agents/TODO.md`](.agents/TODO.md), the current state in [`docs/PROJECT_KNOWLEDGE.md`](docs/PROJECT_KNOWLEDGE.md).

Written 2026-11-23 from the final reports of the first round of threads (00 GPU & Display, 00.1 GPU Performance,
01-07) and the owner's decisions of the same day.

## The goal

**The PC behaves 1:1 like a real Mac.** Everything a real Mac does in daily use (sleep, AirDrop, FileVault, Secure
Boot, SIP, updates, iMessage, Continuity, hardware video decode, ...) works the same way here. A feature that works on a
real Mac and not here is a bug, tracked in TODO, unless it is in "Known limits" below with a reason. That list is kept
as short as possible: an entry leaves it as soon as someone finds a way.

Concretely: a fully working macOS Tahoe 26.7.1 on the reference machine (ASUS ROG STRIX B650E-I, Ryzen 9 7900X3D, RX 7900 XTX,
LG 27GS95QE on DisplayPort), installable by others with a Navi 31 card (RX 7900 XT or XTX), any monitor and any board:

```
gh repo clone ownback/ryzentosh && cd ryzentosh && claude
> /ryzentosh-pre-install      # on Linux: checks the machine, dumps ACPI, records the monitor, builds and copies the EFI
  (reboot, OpenCore, install macOS from the installer partition, reboot, Macintosh HD)
> /ryzentosh-post-install     # in macOS: GPU daemon and Metal plugin, checks, validation
```

Then a public release and a Reddit post. Linux (dual-boot or live USB) is required: the ACPI dump, the monitor capture
and the EFI build run there.

## Repositories

| Repo | What | Starts |
|---|---|---|
| `ownback/vermilion-xtx` (this one, renamed from `ownback/vermilion`) | The GPU driver: kexts, Metal plugin, Mesa patches, boot daemon. Fork of `leolivm/vermilion`; the XT keeps working | now |
| `ownback/ryzentosh` | The EFI generator: machine profiles, `./build.sh`, the two skills, the install guide. Fresh git history ("moved from ownback/vermilion-xtx at `<sha>`"). Reference for its shape: `leolivm/ROG-STRIX-B760-I-RX7900XT-EFI` | thread 10 |
| `ownback/MtkWireless` (fork of `Dino-VN/MtkWireless`, GPL) | Wi-Fi and Bluetooth for the MT7922 (one repo holds both). Forked only when a fix needs its source | thread 07 |

Upstream: credit leolivm clearly, send fixes that help the XT upstream as separate PRs (VermilionTrust first), tell
leolivm before the Reddit post. The same for Dino-VN.

### T3 projects

The T3 projects follow the repos:

| T3 project | Folder → repo | Series threads |
|---|---|---|
| `vermilion-xtx` (project id `fe6768b2-5e37-4cb8-950b-0f24ceda7d19`; "Vermilion" until 2026-11-23) | `~/Projects/vermilion` → `ownback/vermilion-xtx` | 00–06, 09, and the coordination thread |
| `MtkWireless` | `~/Projects/MtkWireless` → `ownback/MtkWireless` (fork of `Dino-VN/MtkWireless`) | 07 |
| `Ryzentosh` | `~/Projects/ryzentosh` → `ownback/ryzentosh` | 10, 11 (12 runs across all repos from there) |

- **Numbering stays global** across the projects (00–12), so the order is visible everywhere. Thread titles: "NN – Name".
- **A T3 project is created only when its repo exists.** No empty project points at a missing folder. Thread 07 forks
  MtkWireless on GitHub, clones it to `~/Projects/MtkWireless`, and creates the project (`t3_project_create`) before
  it starts work. Thread 10 does the same for `ryzentosh`.
- **Threads in different projects message each other** with `t3_thread_send`, by thread id.

## Rules for every thread

1. **Workflow:** one numbered thread at a time. Pull `fork/main`, branch `ownback/<topic>`, work, PR, merge with a merge
   commit "Merge #N: ...", then the finishing thread creates the next one (from this file plus its handoff note), in the
   right T3 project ("T3 projects" above): `t3_thread_launch` with that project's `projectId` and a worktree from that
   repo's `fork/main` or `origin/main`. Thread 06 launches 07 in the MtkWireless project, creating it first (fork,
   clone, project) if 07 can't do that itself. Thread 09 launches 10, which creates the Ryzentosh project.
2. **Own worktree per thread.** Never switch the branch of a checkout another thread uses.
3. **One EFI change per boot, always, with the rollback written in TODO.md first.** Only the active thread installs to
   the EFI partition; any other thread asks it. Every EFI change goes into the EFI change log in PROJECT_KNOWLEDGE with
   the thread's number.
4. **Hardware safety (CLAUDE.md rule 1) is not negotiable:** no SMU / PSP / DAL message or register write that Linux
   amdgpu doesn't send on this ASIC, never write the VBIOS, Linux -> macOS is always a full power-off.
5. **Models:** Opus 5.5, high effort, does the work; ultracode on for 01, 02, 05, 07. Before every merge, a Fable 5.2
   clean-up pass with no behaviour change. Thread 12 is Fable 5.2, ultracode, max effort, over every repo.
6. **Subagents** are allowed for anything (research, review, builds). Rule 3 still holds: a subagent never installs to
   the EFI partition on its own.
7. **Threads may message each other** (T3), for questions and handoffs. They don't take over each other's scope.
8. **Skills are part of done.** Each thread writes or updates the steps for its area in
   `.claude/skills/ryzentosh-pre-install/` and `.claude/skills/ryzentosh-post-install/` (drafted here, moved to ryzentosh
   in thread 10), and runs them once. Skills orchestrate; tested scripts do the risky work (`efi/build.py`,
   `tools/capture/*`, ...). A skill never improvises a register access, a partition change or an ESP layout.
9. **Mac parity is the measure.** "It loads" or "it shows in System Information" is not done; done is "it behaves
   like on a real Mac", checked by use. A thread that finds a feature that works on a Mac and not here adds it to TODO
   under the thread that owns it.
10. **Say what is verified** (CLAUDE.md rule 7). No claim goes into the README that a test on the machine didn't show.
11. **Performance:** measure before and after, under the same conditions. Record baselines in PROJECT_KNOWLEDGE.
12. **No macOS update** (not even 26.7.x) until thread 09 has the VermilionTrust layout check: today an update can make
    it hook the wrong functions and panic.

## Known limits: the only accepted differences from a real Mac

Each one goes into the README as it is. Everything not listed here is meant to work like on a Mac; until it does, it
is an open bug, not a limit.

| Limit | Why |
|---|---|
| macOS stays on 26.7.1 | The Metal plugin uses private interfaces of exactly this build; Tahoe is the last Intel macOS |
| No Apple TV films or Safari FairPlay video (Apple Music works) | Needs Apple's protected hardware decode path plus HDCP; not realistic, legally risky |
| OpenGL and OpenCL only on the CPU system-wide | Apple's driver-bundle interfaces are private; per-app Zink (e.g. Minecraft) is the realistic ceiling |
| No Hypervisor.framework: no Docker Desktop, UTM (accelerated), Parallels, VMware | Apple's hypervisor implements Intel VT-x only, not AMD SVM. Use containers on Linux over SSH |
| No native AMD CPU power management, no CCD-aware scheduling on X3D parts | Apple never shipped it; custom kexts only |
| Touch ID, Apple Pay, iPhone Mirroring, Apple Intelligence | Need Apple silicon or a T2 |
| Remaining VRR flicker on OLED | The panel's own behaviour, the same on Windows and Linux |

## The series

Status: `planned`, `active`, `merged (#N)`.

| # | Thread | Scope (from the reports) | Exit criteria | Status |
|---|---|---|---|---|
| 00 | Housekeeping | ESP clean-up (the `vermilion/handoff/` folder holds SMBIOS serials and an ACPI dump with the VBIOS; old `payload/`, `capture/`, `daemon*`, ~100 OpenCore logs); `~/vermilion-rollback/` pruned to the working set; merged branches and old worktrees deleted (after checking each `build/` for serials); repo renamed to `vermilion-xtx` and links fixed; TODO cut to open items (logs -> one findings file); PROJECT_KNOWLEDGE reconciled (live EFI = `vermilion-trust`, change 8); stale docs fixed (efi/README, ARCHITECTURE, CLAUDE.md layout, REFERENCE-MACHINES, INSTALL marked "expert, manual"); `vermilion-trust` as the profile's everyday stage; `trust.sh` + EFI rebuild in the deploy runbook; mock tests portable to Linux; skill skeletons; one combined Linux handoff note; `OWNER-TESTS.md` | Repo, ESP and docs match the machine; nothing with serials outside `~/.config/vermilion` and the live ESP; no behaviour change | active |
| 01 | GPU stability | Failed DP link training at login leaves a black screen and locks switching for the boot (retry, fall back to a known mode, unlock); the daemon reports SUCCESS during a black screen (read the engine state); panic reports (test one boot without `DisableVariableWrite`; if NVRAM writes work, panics get saved); restart panic of 2026-11-19; tearing and input lag at 60 Hz (measure first); physical size 903x508 mm instead of the EDID's 590x333 (72 dpi fallback; blocks HiDPI); VRR floor change resets the display identity; DRR double buffer for the whole boot; b138 freeze at 1280x1024 (low priority); display sleep | No black screen without recovery; a panic leaves a report; size correct | planned |
| 02 | GPU and CPU performance, power | Linux reference capture (GPU power, clocks, SMU features; CPU `cpuclk` / turbostat; the same shader through Vulkan); a real-app test set (WebGL, a Metal game, Minecraft); XTX RADV profile chosen by PCI revision (96 CU, name); where buffers live (all in system memory today) and VRAM-backed buffers (weeks); interrupt-driven fence waits; idle power (UCLK pinned at 456 MHz: only what Linux sends); CPU idle: a minimal `_CST` idle kext if Linux shows CC6 matters (5.15 vs 5.6 GHz, 40-50 W idle); fan readings (SMCSuperIO, chip from Linux); working-set size from VRAM | Numbers against Linux for each change; no regression in stability | planned |
| 03 | Graphics APIs and video | VCN decode (HEVC, AV1) through VideoToolbox; Vulkan loader + a real window surface; Zink for apps that load their own libGL (Minecraft); Safari's WebKit GPU process (sandbox, plugin location); WebGL / WebGPU; Quick Look and Photos on hardware decode | Each API's real state measured and written down | planned |
| 04 | DP/HDMI audio, HDMI output | Not looked at by anyone yet: first an inventory and a Linux capture | Works, or a written reason why not | planned |
| 05 | Sleep and wake | Platform first (one boot with the GPU driver off: board, USB, NVMe, Wi-Fi, BT), then GPU resume (the driver assumes a cold GPU today); then sleep allowed again (`pmset disablesleep 0`) | Sleep / wake 20 times without a hang, or the limit documented | planned |
| 06 | Fresh-install path | VermilionAccel from OpenCore instead of the AuxKC ("Allow" click; **feasibility unknown, check first**); daemon out of `~/gpu` into a system location; daemon arm compatible with FileVault; `phxfb-squat` set by OpenCore (`nvram_add`) instead of from Linux; **monitor replay plans as data loaded at boot, not compiled into VermilionBoot** (so Linux-only users can add their monitor); VermilionTrust's cdhash list as an `Info.plist` property filled by `efi/build.py` (EFI buildable on Linux); daemon flags from the machine profile; `ws-gpu-present*` files, colour profiles, tools installed by the installer; Vermilion GitHub Releases + CI | A fresh macOS install on this machine becomes accelerated with the skill alone, no hand steps | planned |
| 07 | Wireless and Continuity | First MtkWireless 1.1.2 and the router on channel 44 (owner test, no EFI change); then AirDrop, Universal Clipboard, Continuity Camera (wired and wireless), Sidecar / Universal Control, Auto Unlock; fork only for what's left (adaptive AWDL off-channel, shorter windows); Bluetooth / Wi-Fi coexistence; regulatory domain; the profile field `wifi:` for other cards | AirDrop both ways without the Wi-Fi stalls coming back | planned |
| 08 | (folded into 07) | Universal Clipboard turned out to be Wi-Fi (AWDL), and MtkWireless is one repo for both drivers. Reopened only if a Bluetooth-only problem shows up | — | folded |
| 09 | Security and updates | VermilionTrust: check its hook offsets at boot from the kernel's own functions, plus a pre-update check; `ipc_control_port_options=0` and AMFIPass: find out what needs them, boot without; SIP `0x0A03` -> `0x0203`, then towards `0x0` (after 06); Apple Secure Boot; FileVault (after 06); update policy and rollback | A 26.7.x security update installed safely, or a written reason to stay | planned |
| 10 | ryzentosh | New repo; `./build.sh --machine <profile>` (Linux): pinned downloads, SMBIOS generated locally and never committed; machine-profile guide; generators for the DSDT patches, SSDT-CPUR (from the MADT) and the USB map; BIOS-version guard for the DSDT patches; installer image without a Mac (or a documented USB route); partition guide; OpenCanopy GoldenGate theme; BIOS settings table; `built-in` for the NVMe; `en0` check; the two skills moved in | A clean install on this machine from the README and the skills alone | planned |
| 11 | Final validation | Old thread 07 resumes: the owner test list, stress and soak tests, Time Machine, Recovery boot, Ethernet, USB 3, a full regression and performance pass against the baselines | Every README claim backed by a test | planned |
| 12 | Final refactor | Fable 5.2, ultracode, max effort, over every repo: no behaviour change, then the public release and the Reddit post | Binaries rebuilt and checked against the validated ones | planned |

## Cross-cutting

- **One Linux session, many answers.** Thread 02 (GPU, CPU, the fan chip) and the old thread 03's storage baseline
  (reference numbers, TRIM through LUKS) all need Linux numbers. Thread 00 writes one handoff note for a single Linux
  boot; the results go into PROJECT_KNOWLEDGE's performance record.
- **Owner tests** live in [`.agents/OWNER-TESTS.md`](.agents/OWNER-TESTS.md), one checklist, grouped by the thread
  that needs the answer.
- **Privacy:** no serials, MLB, ROM, UUIDs, MACs, e-mail, EDIDs with serials or AMD firmware in git (CLAUDE.md rules 3
  and 4). Built EFIs and `build/` folders contain serials. The skills keep serials out of the conversation (macserial
  writes to a 0600 file the agent doesn't print) and tell the user that what the agent reads goes to the model provider.
