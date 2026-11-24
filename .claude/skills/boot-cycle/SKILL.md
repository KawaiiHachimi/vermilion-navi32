---
name: boot-cycle
description: Restart this Mac to boot a new EFI change, kext or daemon, and continue automatically after the reboot. Use whenever a change has to boot. Covers the preconditions (machine lock, preflight, rollback written down), telling the owner, scheduling the post-boot wake-up of this thread, the restart itself and the post-boot check.
---

<!-- SPDX-License-Identifier: MIT -->

# Boot cycle

The owner allowed agents to restart the machine themselves (2026-11-24). A boot is still the scarcest resource:
**one EFI change per boot** (CLAUDE.md rule 1), and a hung boot stops all work until the owner powers the machine
off. Never use this skill to go from Linux to macOS: that is always a full power-off.

## 1. Before the restart

1. Hold the machine lock: `.claude/hooks/machine-lock acquire "<what boots>"`.
2. Exactly one change is staged. Its check and rollback steps are written in the TODO first, and the previous
   working EFI is kept as `EFI/OC.prev`, with a second copy in `~/vermilion-rollback/`.
3. Every changed kext passed `sh scripts/dev/preflight.sh <kext>` (`--auxkc` for VermilionAccel). Daemon or script
   changes passed `bash tests/mock/run.sh` and `bash tests/mock/b15test.sh`. GPU programming changes passed the
   `hw-safety-reviewer` agent.
4. Auto-login must work, or the machine waits at the login window and nothing resumes. After the first boot with a
   new daemon, check `grep autologin: /var/log/phxboot.log | tail -1`.
5. Write the expected result into the STATUS file: the change, what the post-boot check must show, and what to do
   if it doesn't.

## 2. Tell the owner, schedule the wake-up, restart

1. If the owner may be at the PC, ask first and wait for the answer: a restart under their hands is not OK
   (ROADMAP.md, rule 6). Otherwise send one short notice, e.g. "Restarting at 14:05 for EFI change 34
   (b161). Back by 14:12. If you hear nothing by then: hold the power button, power on; if it doesn't boot, swap
   EFI/OC and EFI/OC.prev."
2. Schedule this thread's own wake-up with the T3 `schedule_task` tool: `schedule: {type: "once", runAt: <now + 8
   min, ISO 8601>}`, `bindToCurrentThread: true`, prompt: `post-boot check for <change>: run the boot-cycle skill,
   part 3`. Whether a once-task whose time passed while the machine was down still fires when T3 starts is
   (unverified). On the first use, note what happened in the TODO.
3. Commit and push anything unpushed, then restart: `sudo shutdown -r now`.

## 3. After the boot

1. `bash tools/validation/bootcheck.sh`, the daemon log (`/var/log/phxboot.log`: `SUCCESS`, no `DARK`), the
   change's own checks from the STATUS file, and `.claude/hooks/machine-lock status`.
2. Pass: record it in the TODO and the STATUS file, and release the lock when nothing else is queued.
3. Fail with a working desktop: put the previous EFI back (`EFI/OC.prev`), restart once more, record it.
4. If the thread was woken late, or not at all, and the owner reports a hang: the boot never reached the desktop.
   Ask the owner for one photo of the screen, then roll back as written in the TODO.
