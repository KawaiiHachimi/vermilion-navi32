# .agents: the agents' workspace

Working notes for coding agents (and the humans steering them). The public, stable documentation is in
[`../docs/`](../docs/). The rules are in [`../CLAUDE.md`](../CLAUDE.md).

The shared map across all threads is [`../docs/PROJECT_KNOWLEDGE.md`](../docs/PROJECT_KNOWLEDGE.md): read it
first, and update it when the machine's state, the known-good configuration or a cross-thread rule changes.

| File | What | How to update |
|---|---|---|
| [../ROADMAP.md](../ROADMAP.md) | The standard, the repositories, the phases, the rules, the known limits | Rarely edited |
| [STATUS.md](STATUS.md) | What works, what's broken, what's next | Kept by the megamind |
| [TODO.md](TODO.md) | Open tasks, grouped by the threads of the 2026-11-23 series | Delete an item when done (record it where it belongs); add items under the owning thread |
| [OWNER-TESTS.md](OWNER-TESTS.md) | Tests only the owner can do, grouped by the thread that needs the answer | Tick with the date and the result |
| [HANDOFF-LINUX-REFERENCE.md](HANDOFF-LINUX-REFERENCE.md) | The plan for the next Linux boot (read-only captures) | Replace when done; record results in a finding |
| [decisions.md](decisions.md) | Why things are the way they are | Append only; newest at the bottom |
| [runbooks.md](runbooks.md) | Exact procedures (boot safety, Linux sessions, building on the Mac, EFI install, deploying a plugin) | Mark *(unverified)* until done once, then date it |
| [findings/](findings/) | One dated file per investigation (`YYYY-MM-DD-topic.md`) | Write a new file; don't rewrite old findings, add a note that supersedes them |

Rules:

- **Facts with sources.** Each finding says how it was found (code read with `file:line`, Linux query, macOS
  test) and on which machine.
- **No secrets or personal data:** no passwords, SMBIOS serials, UUIDs, MAC addresses. No AMD firmware, VBIOS dumps
  or raw register captures with serials; keep those in a local directory outside the repo.
- **Keep it short.** A finding that turns into knowledge everyone needs moves into `docs/`, and the finding links
  there.
