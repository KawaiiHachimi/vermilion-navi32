#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""guard.py: the Claude Code PreToolUse hook that enforces CLAUDE.md's hard rules on every tool call.

Exit 0 lets the call run; exit 2 blocks it and the message on stderr goes back to the agent.

1. Never allowed: Reset NVRAM (`nvram -c`), VBIOS flash tools, force-push, push to main/master, `reset --hard`,
   `clean -f`, `checkout .` / `restore .`, `branch -D`, bare `git stash` / `git stash pop` (the stash is shared by
   all worktrees).
2. Machine lock: writes to the EFI partition, the system volume, /Library/Extensions, /Library/GPUBundles,
   /Library/LaunchDaemons and ~/gpu, and restarts, kmutil/bless/nvram writes and the like need the machine lock.
   One session holds it at a time (`.claude/hooks/machine-lock acquire "<reason>"`), so only one agent changes the
   machine (the one-EFI-change-per-boot rule needs a single owner).
3. Personal data: `git commit` is blocked when the staged diff contains a value from the private pattern list
   (~/.config/vermilion/private-patterns.txt, made by scripts/dev/private-patterns.sh: serials, MLB, ROM, UUID,
   MAC addresses, e-mail, the host and login names). The list stays outside the repo; matches are reported masked.

The git rules follow mattpocock/skills' git-guardrails-claude-code (MIT), changed so that pushing feature branches
stays allowed.
"""
import json
import os
import re
import subprocess
import sys
import time

LOCK = os.path.expanduser(os.environ.get('VERMILION_MACHINE_LOCK', '~/.vermilion/machine.lock'))
PATTERNS = os.path.expanduser(os.environ.get('VERMILION_PRIVATE_PATTERNS', '~/.config/vermilion/private-patterns.txt'))
STALE_S = 12 * 3600

FORBIDDEN = [
    (r'\bnvram\s+-\w*c\b', 'Reset NVRAM is never done: it removes phxfb-squat (CLAUDE.md, hard rules).'),
    (r'\b(amdvbflash|atiflash|atiwinflash|flashrom)\b', 'Never flash or write the VBIOS (CLAUDE.md rule 1).'),
    (r'\bgit\b[^;&|]*\bpush\b[^;&|]*(\s--force(-with-lease)?\b|\s-f\b|\s\+\S)', 'No force-push.'),
    (r'\bgit\b[^;&|]*\bpush\b[^;&|]*\s(\S+:)?(main|master)(\s|$)', 'No direct push to main: open a PR and merge it.'),
    (r'\bgit\b[^;&|]*\breset\b[^;&|]*--hard\b', 'No `git reset --hard`: make a WIP commit or ask the owner.'),
    (r'\bgit\b[^;&|]*\bclean\b[^;&|]*\s-\w*f', 'No `git clean -f`.'),
    (r'\bgit\b[^;&|]*\b(checkout|restore)\s+(--\s+)?\.(\s|$)', 'No `git checkout .` / `git restore .`.'),
    (r'\bgit\b[^;&|]*\bbranch\b[^;&|]*\s-D\b', 'No `git branch -D`: use -d (merged branches only) or ask the owner.'),
    (r'\bgit\b[^;&|]*\bstash(\s+pop\b|\s*($|[;&|]))', 'The stash is shared by every worktree: use `git stash push -u -m <tag>` and `git stash apply <sha>`, or a WIP commit.'),
]

PROTECTED_PATH = re.compile(
    r'/Volumes/(EFI|ESP|NO[ \\]+NAME)\b|(?<!build/efi)/EFI/(OC|BOOT)(/|\b)|(^|[\s"\'=])/System/|/Library/(Extensions|GPUBundles|LaunchDaemons|LaunchAgents|Video/Plug-Ins|PrivilegedHelperTools)\b'
    r'|(~|\$HOME|\$\{HOME\}|/Users/[^/\s]+)/gpu(/|\b)')
WRITE_VERB = re.compile(
    r'(^|[\s;&|(])(cp|mv|rm|rsync|ditto|install|tee|dd|ln|chmod|chown|chflags|touch|mkdir|rmdir|unzip|tar|xattr|'
    r'sed\s+-i|perl\s+-p?i|plutil\s+-(replace|insert|remove|convert)|defaults\s+write|PlistBuddy)\b|>')
MACHINE_TOOL = re.compile(
    r'(^|[\s;&|(])(bless|kextload|kextunload|kextcache|csrutil|shutdown|reboot|halt)\b'
    r'|\bkmutil\s+(install|create|load|unload|clear-staging|rebuild)\b'
    r'|\blaunchctl\s+(bootout|bootstrap|load|unload|kickstart|enable|disable)\s+(-\w+\s+)*system\b'
    r'|\bmount\s+-u?w\b|\bmount_msdos\b'
    r'|\bdiskutil\s+(mount|unmount|umount|erase\w*|apfs\s+(delete\w*|resize\w*|add\w*)|partitionDisk|resizeVolume|rename\w*)\b'
    r'|\bpmset\s+(restoredefaults|disablesleep|schedule)\b'
    r'|\bnvram\s+(-d\b|[^\s-][^\s]*=)'
    r'|\bsystemsetup\s+-set\w+')
LOCK_CMD = re.compile(r'(^|[;&|]\s*)(\S*/)?machine-lock(\.sh)?\s+(acquire|steal|release)\b(.*)$', re.M)


def block(msg):
    sys.stderr.write('BLOCKED by .claude/hooks/guard.py: ' + msg + '\n')
    sys.exit(2)


def read_lock():
    try:
        with open(LOCK) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def write_lock(data):
    os.makedirs(os.path.dirname(LOCK), mode=0o700, exist_ok=True)
    tmp = LOCK + '.tmp'
    with open(tmp, 'w') as f:
        json.dump(data, f)
    os.replace(tmp, LOCK)


def handle_lock_cmd(cmd, session):
    m = LOCK_CMD.search(cmd)
    if not m:
        return
    op, rest = m.group(4), m.group(5).strip().strip('"\'')
    cur = read_lock()
    now = int(time.time())
    held_by_other = cur and cur.get('session') != session
    stale = cur and now - int(cur.get('since', 0)) > STALE_S
    if op == 'acquire':
        if held_by_other and not stale:
            block(f"the machine lock is held by another session since {time.ctime(cur.get('since', 0))} "
                  f"for: {cur.get('reason', '?')}. Wait, ask that thread, or use `steal` if it is stale.")
        write_lock({'session': session, 'reason': rest or 'unspecified', 'since': now})
    elif op == 'steal':
        if held_by_other and not stale:
            block('the lock is not stale (under 12 h old): ask the holding thread or the owner first.')
        write_lock({'session': session, 'reason': rest or 'stolen', 'since': now})
    elif op == 'release':
        if cur and cur.get('session') == session:
            os.remove(LOCK)


def needs_lock(cmd):
    if MACHINE_TOOL.search(cmd):
        return True
    return bool(PROTECTED_PATH.search(cmd) and WRITE_VERB.search(cmd))


def require_lock(session, what):
    cur = read_lock()
    if cur and cur.get('session') == session:
        return
    holder = f" (held by another session for: {cur.get('reason')})" if cur else ''
    block(f'{what} changes the machine and needs the machine lock{holder}. '
          'Run `.claude/hooks/machine-lock acquire "<reason>"` first, and release it when done.')


def mask(s):
    return s[:2] + '*' * max(0, len(s) - 4) + s[-2:] if len(s) > 4 else '****'


def check_private(cmd, cwd):
    if not re.search(r'\bgit\b[^;&|]*\bcommit\b', cmd):
        return
    try:
        with open(PATTERNS) as f:
            pats = [l.strip() for l in f if len(l.strip()) >= 6 and not l.startswith('#')]
    except OSError:
        return
    if not pats:
        return
    diff_args = ['diff', 'HEAD'] if re.search(r'\s(-\w*a\w*|--all)\b', cmd) else ['diff', '--cached']
    try:
        diff = subprocess.run(['git', '-C', cwd] + diff_args + ['-U0', '--no-color'],
                              capture_output=True, text=True, timeout=60).stdout
    except (OSError, subprocess.SubprocessError):
        return
    added = '\n'.join(l[1:] for l in diff.splitlines() if l.startswith('+') and not l.startswith('+++')).lower()
    hits = sorted({p for p in pats if p.lower() in added})
    if hits:
        block('the staged changes contain private values (' + ', '.join(mask(h) for h in hits) +
              '): serials, MAC addresses, e-mail or the host name must never be committed (CLAUDE.md rules 4-5).')


def main():
    try:
        data = json.load(sys.stdin)
    except ValueError:
        return
    tool = data.get('tool_name', '')
    inp = data.get('tool_input') or {}
    session = data.get('session_id') or 'unknown'
    cwd = data.get('cwd') or os.getcwd()
    if tool == 'Bash':
        cmd = inp.get('command', '')
        for pat, msg in FORBIDDEN:
            if re.search(pat, cmd):
                block(msg)
        if LOCK_CMD.search(cmd):
            handle_lock_cmd(cmd, session)
            return
        if needs_lock(cmd):
            require_lock(session, 'this command')
        check_private(cmd, cwd)
    elif tool in ('Write', 'Edit', 'MultiEdit', 'NotebookEdit'):
        path = os.path.expanduser(inp.get('file_path') or inp.get('notebook_path') or '')
        if PROTECTED_PATH.search(' ' + path):
            require_lock(session, f'writing {path}')


if __name__ == '__main__':
    main()
