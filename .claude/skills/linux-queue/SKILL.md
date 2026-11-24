---
name: linux-queue
description: Collect everything that needs Linux (amdgpu captures, ddcutil, register traces, firmware variables, reference benchmarks) into one queue on the EFI partition, so the owner boots Linux once and Claude on Linux runs every job in that session. Use whenever a task needs Linux, instead of asking the owner for a separate Linux session.
---

<!-- SPDX-License-Identifier: MIT -->

# Linux queue

Linux is the hardware reference, because amdgpu is what we compare against, but each Linux session costs the owner
two power cycles. So jobs are queued and run together.

## Adding a job (macOS side, needs the machine lock: it writes the ESP)

1. Mount the ESP read-write only for the copy, then unmount it.
2. Create `<ESP>/vermilion/linux-queue/<NN>-<short-name>/JOB.md` for Claude on Linux:
   - what to capture and why, in one paragraph;
   - the exact commands, and which need root. On Linux, Claude asks for the password through the T3 secret prompt
     and never writes it down;
   - the safety rules: only reads, traces and amdgpu's own paths. No register writes or SMU messages of our own, no
     VBIOS access, nothing that changes firmware variables unless the job is exactly that and says so;
   - expected output files and sizes. The ESP is FAT and small, so keep results to a few MB per job and compress
     with `xz`;
   - any script the job needs, next to JOB.md.
3. Append one line to `<ESP>/vermilion/linux-queue/QUEUE.md`: `NN  short-name  thread  one-line purpose`.
4. Tell the owner only when the queue is worth a session, or a job blocks the work.

## Running the queue (Linux side)

The owner boots Linux and tells Claude there: "do `/boot/vermilion/linux-queue/QUEUE.md`" (`/boot` is where Linux
mounts the ESP on this machine). Claude on Linux runs the jobs in order and writes each job's results into its
`results/` folder. It finishes with `systemctl poweroff`, a full power-off, **never a reboot**: going from Linux to
macOS always needs a cold GPU.

## Collecting (macOS side, lock again)

1. Copy each `results/` folder to `~/vermilion-captures/<date>-<NN>-<short-name>/`, outside the repo. Captures can
   hold EDIDs with serials, MACs or VBIOS data and **never go into git**.
2. Remove the finished job folders from the ESP. Leave QUEUE.md with only the open jobs.
3. Write the findings in `.agents/findings/`, citing the capture folder by name, not its contents.
