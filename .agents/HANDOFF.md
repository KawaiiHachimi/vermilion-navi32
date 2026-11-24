# Handoff

What the megamind needs in its first five minutes. Each megamind replaces this file when it hands over
(`.claude/skills/megamind-clone`).

## 2026-11-24: to Megamind 1

From the coordination thread that ran the 00-13 series and then the restructure. That series is closed. Its history
is in PRs #9-#18 and in `.agents/findings/`.

### Your job

You own the plan, `.agents/STATUS.md`, the machine and the merges for `ownback/vermilion-xtx`. You send agents to
research, find root causes and write fixes. You don't write large fixes yourself. The standard, phases and rules are
in [`ROADMAP.md`](../ROADMAP.md); read it and CLAUDE.md first. Work this repo to a stable release before
`ryzentosh-airdrop` and `ryzentosh` (research for those may run earlier).

**Your agents:**
- **Root causes, fixes and research:** Opus 5.5 (`Agent` tool, or T3 `delegate_task` for an isolated worktree).
  Several may run in parallel, as long as each one only reads the machine or works in its own worktree.
- **Machine changes:** only you make them, one at a time, under the machine lock.
- **Clean-up:** the `fable-cleanup` agent on every branch before you ask the owner to merge.
- **GPU programming changes:** the `hw-safety-reviewer` agent.

### Phase 1, audit: the next steps

1. **Ask the last threads for their state** (`t3_thread_read` first, then one `t3_thread_send` each with a short
   question: what's done, what's open, what's uncommitted, what you'd do next). They are idle; don't give them new
   work:
   - [04 – DP / HDMI audio, HDMI output](t3-thread://v2/1b2ac41d-d88f-4ef7-b69d-8bf2b20b4a43/mcp:launch:7c5b0c1b0e79e764e0877efa60c481f35aadb8f68e8ae935e54d34c5f92077a4):
     paused for the restructure. It has the unmerged branch `ownback/dp-hdmi-audio` (VermilionAudio, which is
     live on the machine, the auto-login change to the daemon, and the 2026-11-24 findings).
   - [03 – Graphics APIs and video](t3-thread://v2/1b2ac41d-d88f-4ef7-b69d-8bf2b20b4a43/mcp:launch:fb04b262075c61349ba55c0dabd5f7f1698fbba4762412243bcc105614245f4d):
     4K AV1 at about 13.3 ms per frame (synchronous); VP9 was suggested as a stopgap; the WebGPU sample failures are open.
   - [02 – GPU+CPU Performance & Power](t3-thread://v2/1b2ac41d-d88f-4ef7-b69d-8bf2b20b4a43/mcp:launch:621ef509b6015ef32dcf81009defc95b1554ebf96e9c3479dc5995283c92a150):
     the performance work was moved to phase 4 on the owner's request. Its ranked backlog is in TODO,
     "Performance".
   - [Ryzentosh Auto-Login Not Working](t3-thread://v2/1b2ac41d-d88f-4ef7-b69d-8bf2b20b4a43/27573923-113a-40e5-9130-a0b7fcf6a67f)
     (other project): changed the `LWRAN` auto-login handling in the installed `~/gpu/phxboot-navi31.sh`. Don't
     overwrite that change when the daemon is reinstalled. Its claim that main was behind came from the stale
     `~/Projects/vermilion` checkout.
2. **Make main match the machine:** get `ownback/dp-hdmi-audio` reviewed (Fable pass, hw-safety where it touches the
   GPU), ask the owner, merge, delete the branch. Then check that the installed daemon equals main's
   `tools/phxctl/phxboot-navi31.sh`, including the auto-login lines.
3. **Audit and write STATUS.md:**
   - Read all the code (Serena and clangd help here: `sh scripts/dev/compile-commands.sh`). Check the machine with
     read-only tools (`tools/validation/bootcheck.sh`, the phxboot log, `ioreg`, `system_profiler`).
   - Rank every part by what a Mac user would notice:
     - works;
     - broken;
     - missing;
     - works but doesn't meet the standard (the ROADMAP list).
   - Each part gets its evidence and its next step.
   - Then decide the phase 2 order and tell the owner in a few lines.

**Also open, decide during the audit:**
- `fork/ownback/linux-horizon` has one unmerged findings commit: merge it as a finding or close it.
- DDC/CI over DP AUX for brightness and volume (owner requirement from thread 04): MonitorControl and BetterDisplay
  must work, as on a real Mac with a non-Apple monitor. Ask the owner what is missing for volume control (no answer
  yet).
- HDMI output: later, before work on ryzentosh starts (owner, 2026-11-24).
- The XT question (keep the detection code, drop it, or rename the project): the owner hasn't decided. Don't work
  on the XT.

### Machine state (2026-11-24)

- **EFI:**
  - change 33 is live: stage `vermilion-trust-bar1g-audio`, VermilionAudio 1.7.2;
  - `EFI/OC.prev` is change 32;
  - a change-30 copy is in `~/vermilion-rollback/`.
  - The ESP mounts as `/Volumes/NO NAME`.
- **The system volume is unsealed** (the decoder bundle). Every change to it needs the owner's OK. Its volume name
  was changed by hand and is cosmetic; leave it.
- **Disk:** about 33 GB free in the APFS container. The owner declined resizing the partitions.
- **Tools:** described in [`docs/AGENT-TOOLING.md`](../docs/AGENT-TOOLING.md). The private pattern list exists.
  The machine lock is free.
- **macOS 26.7.1 (25G241):** never update it.

### Owner decisions since the restructure

All of them are in ROADMAP and in the memory notes:
- agents restart the machine themselves (the `boot-cycle` skill);
- parallel threads: unsure, so parallel agents are allowed, but machine changes go through you;
- a post-installer comes later; no ryzentosh pre/post-install skills;
- the SMBIOS stays git-ignored, and there's no private repo;
- branches are deleted on merge;
- reverse engineering copies the result, never the code.

### Don't, yet

- Don't detach or unload any driver on the GPU's PCI functions in a running boot: IOPCIFamily hot-resets the GPU
  and the machine hangs (seen on the XTX machine, 2026-11-24; the finding is on `ownback/dp-hdmi-audio`).
- Don't install anything from `~/Projects/vermilion`: it's a stale checkout of an old main.
- Don't start benchmarks or stress runs without asking the owner (`vkbench` with no arguments runs for 20 s).
- Don't start performance work before phase 4.
