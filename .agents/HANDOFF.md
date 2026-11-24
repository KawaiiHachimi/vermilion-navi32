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

### Phase 1, audit: where it stands (Megamind 1, 2026-11-24 evening)

Done:
- The last threads (02, 03, 04, auto-login) answered and are archived; their state is in
  [`findings/2026-11-24-audit-thread-handover.md`](findings/2026-11-24-audit-thread-handover.md).
- The audit is written: [`STATUS.md`](STATUS.md), with the evidence in `findings/2026-11-24-audit-*.md`. It sets the
  phase 2 order.
- `ownback/dp-hdmi-audio` merged as #20: hw-safety review done (no wrong value or instance; three sequencing differences, recorded
  as a decision with TODO 04 follow-ups; the replay prototype retired). The installed daemon equals the branch copy,
  including the auto-login lines.
- `fork/ownback/linux-horizon` deleted (its finding is already on main). Stale worktrees of 02, 03 and 07 removed.

Next:
1. Phase 2, item 1 (the safety fixes in STATUS), then the rest in STATUS order. #19 and #20 are merged; main matches
   the machine (EFI change 33, the installed daemon).

**Also open:**
- DDC/CI over DP AUX for brightness and volume: phase 2 item 9. The owner asked for both (thread 04).
- HDMI output: later, before work on ryzentosh starts (owner, 2026-11-24).
- The XT question (keep the detection code, drop it, or rename the project): the owner hasn't decided. Don't work
  on the XT.

### Machine state (2026-11-24)

- **EFI:**
  - change 33 is live: stage `vermilion-trust-bar1g-audio`, VermilionAudio 1.7.2; HDR off by `~/gpu/phxboot-nohdr`,
    DP audio by `~/gpu/phxboot-audio`;
  - `EFI/OC.prev` is change 32;
  - a change-30 copy is in `~/vermilion-rollback/`.
  - The ESP mounts as `/Volumes/NO NAME`.
- **The system volume is unsealed** (the decoder bundle). Every change to it needs the owner's OK. Its volume name
  was changed by hand and is cosmetic; leave it.
- **Disk:** about 33 GB free in the APFS container. The owner declined resizing the partitions.
- **Tools:** described in [`docs/AGENT-TOOLING.md`](../docs/AGENT-TOOLING.md). The private pattern list exists.
  The machine lock is free. The debug LaunchAgent `local.t04watch` is removed (plist in `~/vermilion-rollback/`).
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
