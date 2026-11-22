# .agents: the agents' workspace

Working notes for coding agents (and the humans steering them). The public, stable documentation is in
[`../docs/`](../docs/). The rules are in [`../CLAUDE.md`](../CLAUDE.md).

| File | What | How to update |
|---|---|---|
| [TODO.md](TODO.md) | The plan, by phase, with checkboxes | Tick with a date when done; add items where they belong |
| [decisions.md](decisions.md) | Why things are the way they are | Append only; newest at the bottom |
| [runbooks.md](runbooks.md) | Exact procedures (boot safety, captures, build host, EFI install) | Mark *(unverified)* until done once, then date it |
| [findings/](findings/) | One dated file per investigation (`YYYY-MM-DD-topic.md`) | Write a new file; don't rewrite old findings, add a note that supersedes them |

Rules:

- **Facts with sources.** Each finding says how it was found (code read with `file:line`, Linux query, macOS
  test) and on which machine.
- **No secrets or personal data:** no passwords, SMBIOS serials, UUIDs, MAC addresses. No AMD firmware, VBIOS dumps
  or raw register captures with serials; keep those in a local directory outside the repo.
- **Keep it short.** A finding that turns into knowledge everyone needs moves into `docs/`, and the finding links
  there.
