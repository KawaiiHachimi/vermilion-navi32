---
name: megamind-clone
description: Hand the coordinating thread (the megamind) over to a fresh successor thread that continues the work, when the context is getting long, a phase is done, or the owner says "clone yourself". Writes the handoff file, frees the machine lock, launches the successor and settles this thread.
---

<!-- SPDX-License-Identifier: MIT -->

# Clone the megamind

The state lives in files, never only in a conversation: `.agents/STATUS.md` (what works, what's broken, what's
next) and `.agents/HANDOFF.md` (what a successor needs in its first five minutes). A successor that reads only those
two files and CLAUDE.md must be able to continue.

1. **Finish or park the current step.** No half-done machine change: either it's booted and checked, or it's not
   staged. Running agents finish or are stopped; note their results.
2. **Update `.agents/STATUS.md`.**
3. **Write `.agents/HANDOFF.md`** (replace the old one):
   - the date, and why the clone happens;
   - the phase and the next three concrete steps;
   - the machine state: live EFI change number, `EFI/OC.prev`, rollback copies, installed daemon and flags;
   - open branches and PRs, and threads or agents still running;
   - decisions the owner made since the last handoff, as pointers to memory or decisions.md;
   - anything the successor must not do yet.
4. **Commit and push** STATUS.md and HANDOFF.md on the megamind's branch.
5. **Release the machine lock:** `.claude/hooks/machine-lock release`. The successor acquires it.
6. **Launch the successor** with the T3 `t3_thread_launch` tool:
   - same project, model `claude-opus-5-5`, title `Megamind <n+1>`;
   - its own worktree from the megamind's branch;
   - prompt: "You are the Vermilion megamind, successor of <this thread's link>. Read CLAUDE.md,
     .agents/HANDOFF.md and .agents/STATUS.md, then continue with the next step in HANDOFF.md."
7. **Tell the owner** in one line with the successor's link, then settle this thread with `t3_thread_organize`.
