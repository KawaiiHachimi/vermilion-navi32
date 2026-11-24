# Agent tooling

What the coding agents on this project use, how it's installed, and why. Everything here is checked in, except the
tools themselves, which live outside the repo on the build machine, and the private pattern list.

## In the repo

| Path | What |
|---|---|
| `.claude/settings.json` | Project hooks: the guard on every Bash/Write/Edit call, rtk's output compression on Bash calls (only when `rtk` is installed), Serena enabled |
| `.claude/hooks/guard.py` | Enforces the hard rules a machine can check: no Reset NVRAM, no VBIOS tools, no force-push or push to main, no `reset --hard`/`clean -f`/`branch -D`/bare `stash`; the machine lock for changes to the EFI partition, the system volume, `~/gpu`, kexts and restarts; no commit that contains this machine's private values. Tests: `sh tests/hooks/guard_test.sh` |
| `.claude/hooks/machine-lock` | `acquire "<reason>"`, `release`, `steal` (only for a lock older than 12 h), `status`. The lock file is `~/.vermilion/machine.lock` |
| `.claude/agents/hw-safety-reviewer.md` | Checks GPU programming changes against Linux amdgpu (hard rule 1) |
| `.claude/agents/fable-cleanup.md` | The no-behaviour-change pass before every merge, with byte-identical binaries as proof |
| `.claude/skills/boot-cycle/` | Restart to boot a change and continue on your own afterwards |
| `.claude/skills/linux-queue/` | Queue Linux jobs on the EFI partition for one Linux session |
| `.claude/skills/megamind-clone/` | Hand the coordinating thread over to a successor |
| `.mcp.json` | Serena (symbol-level code navigation and editing over clangd) |
| `scripts/dev/preflight.sh` | Checks a built kext before it costs a boot: plist, architecture, link gate (AuxKC), the boot-KC rule (OpenCore), kmutil's diagnostics, `PHX_KEXT_BUILD` bump |
| `scripts/dev/compile-commands.sh` | Writes `compile_commands.json` (git-ignored) for clangd and Serena |
| `scripts/dev/private-patterns.sh` | Writes `~/.config/vermilion/private-patterns.txt` (0600), the values the guard keeps out of commits |

## On the build machine (outside the repo)

Installed 2026-11-24 on the reference machine. The downloads were checked against the publishers' SHA-256 sums.

| Tool | Version, source | Used for |
|---|---|---|
| clangd | Apple clangd 21 from the Command Line Tools, plus Anthropic's `clangd-lsp` Claude Code plugin | C/C++ diagnostics and navigation |
| [Serena](https://github.com/oraios/serena) | 2.1.0, `uv tool install --python 3.12 "git+https://github.com/oraios/serena@v2.1.0"` | Symbol-level navigation and edits, so agents don't read whole files |
| [rtk](https://github.com/rtk-ai/rtk) | 1.2.1, the Apache-2.0 `rtk-oss-x86_64-apple-darwin` release in `~/.local/bin` | Shorter shell output in the agent's context. Telemetry is off by default. The full output of a failed command is kept |
| [REA](https://github.com/morluto/rea) | 6.10.1, `npm install --prefix ~/.local/opt/rea rea-agents@6.10.1`, registered with `rea setup --client claude_code --tool-catalog-profile hot-cold` | Reverse engineering of binaries over MCP |
| [Ghidra](https://github.com/NationalSecurityAgency/ghidra) | 12.2 in `~/.local/opt`, native decompiler built locally (`support/gradle/gradlew buildNatives`; the release has none for Intel Macs) | REA's analysis engine |
| [Temurin JDK](https://adoptium.net/) | 25 in `~/.local/opt` | Ghidra 12.2 needs Java 25 or newer |

## Rules for the tools

- **Reverse engineering:** we study how Apple's drivers behave (interfaces, sequences, which properties macOS
  expects) and write our own implementation of that result. No Apple code, decompiler output or disassembly goes
  into the repo, and Ghidra databases stay outside it.
- **New MCP servers, plugins or skills:** official repositories only, pinned versions, source read before install,
  nothing listening on the network. They run with full access on a machine that holds the sudo password and the
  SMBIOS. Note each new one in the table above.
- **rtk can hide detail:** when a failure needs the full output, run the command with `rtk bypass <cmd>` or read
  the saved raw output that rtk points to.
