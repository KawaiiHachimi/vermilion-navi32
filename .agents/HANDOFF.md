# Handoff

What the megamind needs in its first five minutes. Each megamind replaces this file when it hands over
(`.claude/skills/megamind-clone`).

## 2026-11-24: Megamind 1 to Megamind 2

Why: phase 1 (audit) is done. #19 (the audit, STATUS, the machine-lock and guard fixes) and #20 (DP audio) are
merged, so main matches the machine. Phase 2 starts now.

### Your job

You own the plan, `.agents/STATUS.md`, the machine and the merges for `ownback/vermilion-xtx`. You send agents to
research, find root causes and write fixes. You don't write large fixes yourself. The standard, phases and rules are
in [`ROADMAP.md`](../ROADMAP.md); read it and CLAUDE.md first. Keep STATUS and this file current on `ownback/megamind`
(from `fork/main`) and send them as PRs.

**Your agents:**
- **Root causes, fixes and research:** Opus 5.5 (`Agent` tool, or T3 `delegate_task` with `isolation: "worktree"`).
  Several may run in parallel, as long as each one only reads the machine or works in its own worktree.
- **Machine changes:** only you make them, one at a time, under the machine lock.
- **Clean-up:** the `fable-cleanup` agent on every branch before you ask the owner to merge.
- **GPU programming changes:** the `hw-safety-reviewer` agent.

**The owner wants:** short replies with numbered actions for them, details in files; routine reversible calls made
by you and reported after; questions only for hard-rule items, system-volume changes, GPU load, merges, and restarts
while they may be at the PC.

### Phase 2: the next three steps

The order is in [`STATUS.md`](STATUS.md), "Phase 2 order".
1. **Item 1, the safety fixes,** one branch (`ownback/safety-fixes`), via an agent in a worktree:
   - remove `debug.phxfb_kill_iondrv` (VermilionAccel, `nvrm-accel.cpp:1612-1634`, `:1904`): an AuxKC change, so the
     owner's Allow click;
   - `bringup_navi31.sh:64-80` refuses a warm GPU (PSP sOS alive) instead of loading the SMU firmware (hw-safety);
   - the daemon's "Reset NVRAM" lines (`phxboot-navi31.sh` ~276/282/287) and `docs/XTX.md`; hide `ResetNvramEntry.efi`
     in the profile (an EFI change, its own boot);
   - OWNER-TESTS 04's sleep test marked blocked;
   - read the 2026-11-24 00:05 replug logs for an ab30 detach (`detachFromChild`, "needing hardware reset") before
     any replug test.
   Deploy the daemon part first (no EFI change), then the EFI part, then the AuxKC part, each with its check and
   rollback in TODO.
2. **Item 2, the plugin crashes:** Music's `nvmtl_vk_image_read` fault (two reports on 2026-11-24, 18:47 and 19:32,
   in `~/Library/Logs/DiagnosticReports`), Maps' null texture, no nil command buffer to WindowServer. Root cause first
   (agent, read-only), then a fix branch. A plugin install is a VermilionTrust + EFI change.
3. **Item 3, the recovery trap and the GOP fallback** (STATUS, D7 / D30): research the squat decision in the kext.

Settle on the way: the WebGPU numbers (20 vs 21 of 43 failing samples, 4 vs 6 GPU-process crash reports) differ
between the findings; re-count from the crash reports when item 10 starts.

### Machine state (2026-11-24)

- **EFI:**
  - change 33 is live: stage `vermilion-trust-bar1g-audio`, VermilionAudio 1.7.2; HDR off by `~/gpu/phxboot-nohdr`,
    DP audio by `~/gpu/phxboot-audio`;
  - `EFI/OC.prev` is change 32;
  - a change-30 copy is in `~/vermilion-rollback/`.
  - The ESP mounts as `/Volumes/NO NAME`.
- **Installed daemon** = main's `tools/phxctl/phxboot-navi31.sh` (with the `LWRAN` auto-login lines and `audio_boot`).
- **The system volume is unsealed** (the decoder bundle). Every change to it needs the owner's OK.
- **Disk:** about 33 GB free in the APFS container. The owner declined resizing the partitions.
- **Tools:** [`docs/AGENT-TOOLING.md`](../docs/AGENT-TOOLING.md). The machine lock is free. The debug LaunchAgent
  `local.t04watch` is removed (plist in `~/vermilion-rollback/`).
- **macOS 26.7.1 (25G241):** never update it.

### Branches, PRs, threads

- No open PRs. `fork` has only `main`. Local: `ownback/megamind` and the old `worktree-agent-a264…` /
  `worktree-agent-a98b…` branches (not in main; delete with the owner's OK).
- The old series threads are all archived. No agents running.
- `~/Projects/vermilion` is the main checkout that holds `.git` for every worktree (stale main at 7b30f2b): never
  install from it, don't remove it.

### Waiting on the owner

- Night Shift: does the LG's picture tint (STATUS, head-0 gamma)?
- What is missing for monitor volume control (DDC/CI, phase 2 item 9)?
- The XT question (keep the detection code, drop it, rename). Don't work on the XT.
- HDMI output: after phase 2, before ryzentosh (owner, 2026-11-24).

### Owner decisions since the restructure

All in ROADMAP and the memory notes: agents restart the machine themselves (`boot-cycle`); parallel agents allowed,
machine changes through the megamind; a post-installer later; SMBIOS git-ignored, no private repo; branches deleted
on merge; reverse engineering copies the result, never the code. 2026-11-24: DP audio merged with its sequencing
differences as follow-ups (decisions.md).

### Don't, yet

- Don't detach or unload any driver on the GPU's PCI functions in a running boot (hard rule; finding
  `2026-11-24-gfxhda-detach-gpu-hot-reset.md`).
- Don't ask the owner for a sleep or replug test before the item 1 checks.
- Don't start benchmarks or stress runs without asking the owner (`vkbench` with no arguments runs for 20 s).
- Don't start performance work before phase 4.
