#!/bin/sh
# SPDX-License-Identifier: MIT
# guard_test.sh: the PreToolUse guard (.claude/hooks/guard.py) blocks and allows what CLAUDE.md says. Hardware-free;
# uses a temp lock file, a temp pattern list and a temp git repo.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
G="$HERE/../../.claude/hooks/guard.py"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
export VERMILION_MACHINE_LOCK="$T/lock" VERMILION_PRIVATE_PATTERNS="$T/pats"
pass=0 fail=0

run() { # run <session> <tool> <json-input> -> exit code of the guard
  python3 -I - "$1" "$2" "$3" "$4" <<'EOF' | python3 -I "$G" 2>/dev/null
import json, sys
print(json.dumps({'session_id': sys.argv[1], 'tool_name': sys.argv[2], 'cwd': sys.argv[4], 'tool_input': json.loads(sys.argv[3])}))
EOF
}
bash_cmd() { python3 -I -c 'import json,sys; print(json.dumps({"command": sys.argv[1]}))' "$1"; }
expect() { # expect <code> <session> <description> <command>
  run "$2" Bash "$(bash_cmd "$4")" "$T/repo"; got=$?
  if [ "$got" = "$1" ]; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL ($got, wanted $1): $3: $4"; fi
}
expect_write() { # expect_write <code> <session> <path>
  run "$2" Write "$(python3 -I -c 'import json,sys; print(json.dumps({"file_path": sys.argv[1], "content": "x"}))' "$3")" "$T/repo"; got=$?
  if [ "$got" = "$1" ]; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL ($got, wanted $1): Write $3"; fi
}

git init -q "$T/repo" && git -C "$T/repo" config user.email t@example.invalid && git -C "$T/repo" config user.name t

# never allowed
expect 2 a "reset NVRAM" "sudo nvram -c"
expect 0 a "nvram read" "nvram -p | grep -c phx"
expect 2 a "VBIOS flash" "amdvbflash -p 0 x.rom"
expect 2 a "force push" "git push --force fork ownback/x"
expect 2 a "force push -f" "git push -f fork ownback/x"
expect 2 a "lease push" "git push --force-with-lease fork ownback/x"
expect 2 a "push main" "git push fork main"
expect 2 a "push HEAD:main" "git push fork HEAD:main"
expect 0 a "push branch" "git push -u fork ownback/agent-tooling"
expect 0 a "push main-like branch" "git push -u fork ownback/main-fix"
expect 0 a "delete remote branch" "git push fork --delete ownback/old"
expect 2 a "reset hard" "git reset --hard HEAD~1"
expect 0 a "reset soft" "git reset --soft HEAD~1"
expect 2 a "clean" "git clean -fd"
expect 2 a "checkout ." "git checkout ."
expect 2 a "restore ." "git restore -- ."
expect 0 a "checkout file" "git checkout -- src/x.c"
expect 2 a "branch -D" "git branch -D ownback/x"
expect 0 a "branch -d" "git branch -d ownback/x"
expect 2 a "bare stash" "git stash"
expect 2 a "stash pop" "git stash pop"
expect 0 a "stash push" "git stash push -u -m tag1"
expect 0 a "stash list" "git stash list"
expect 2 a "push to a full main ref" "git push fork HEAD:refs/heads/main"
expect 2 a "push main with git -C" "git -C /tmp/x push fork main"
expect 0 a "commit message about pushing to main" "git commit -m \"never push to main\""

# the machine lock
expect 2 a "ESP copy without lock" "sudo cp -R build/efi/OC /Volumes/EFI/EFI/OC"
expect 0 a "ESP read" "ls /Volumes/EFI/EFI/OC"
expect 2 a "ESP under its FAT label" "cp x \"/Volumes/NO NAME/EFI/OC/Kexts/\""
expect 2 a "any EFI/OC dir" "rm -rf /tmp/mnt/EFI/OC.prev"
expect 0 a "the repo's own build output" "rm -rf build/efi/EFI/OC"
expect 2 a "daemon install without lock" "cp tools/phxctl/phxboot-navi31.sh ~/gpu/phxboot-navi31.sh"
expect 2 a "restart without lock" "sudo shutdown -r now"
expect 2 a "kmutil load" "sudo kmutil load -p /Library/Extensions/X.kext"
expect 2 a "nvram write" "sudo nvram boot-args=-v"
expect 2 a "diskutil mount" "sudo diskutil mount disk0s1"
expect 0 a "diskutil list" "diskutil list"
expect_write 2 a /Volumes/EFI/EFI/OC/config.plist
expect_write 2 a "$HOME/gpu/phxboot-navi31.sh"
expect_write 0 a "$T/repo/README.md"
expect 0 a "acquire" ".claude/hooks/machine-lock acquire \"EFI change 34\""
expect 0 a "ESP copy with lock" "sudo cp -R build/efi/OC /Volumes/EFI/EFI/OC"
expect_write 0 a /Volumes/EFI/EFI/OC/config.plist
expect 2 b "other session, no lock" "sudo shutdown -r now"
expect 2 b "other session acquire" ".claude/hooks/machine-lock acquire other"
expect 2 b "other session steal (fresh)" ".claude/hooks/machine-lock steal other"
expect 0 b "release by non-holder is a no-op" ".claude/hooks/machine-lock release"
expect 0 a "release" ".claude/hooks/machine-lock release"
expect 0 b "acquire after release" ".claude/hooks/machine-lock acquire b-work"
python3 -I -c 'import json,sys; d=json.load(open(sys.argv[1])); d["since"]=0; json.dump(d, open(sys.argv[1],"w"))' "$VERMILION_MACHINE_LOCK"
expect 0 a "steal a stale lock" ".claude/hooks/machine-lock steal stale"
expect 2 a "release chained with a restart" ".claude/hooks/machine-lock release && sudo shutdown -r now"
expect 2 a "acquire chained with a command" ".claude/hooks/machine-lock acquire \"x\" && echo hi"
expect 0 a "acquire through sh" "sh .claude/hooks/machine-lock acquire \"via sh\""
python3 -I -c 'import json,sys; d=json.load(open(sys.argv[1])); sys.exit(d["reason"] != "via sh")' "$VERMILION_MACHINE_LOCK" && pass=$((pass + 1)) || { fail=$((fail + 1)); echo "FAIL: lock reason"; }
expect 0 a "release through sh" "sh .claude/hooks/machine-lock release"
expect 0 a "grep for the lock command is not a lock command" "grep -n \"machine-lock acquire\" CLAUDE.md"
expect 0 a "ESP read with a null redirect" "ls /Volumes/EFI/EFI/OC 2>/dev/null"
expect 2 a "ESP write by redirect" "echo x > /Volumes/EFI/EFI/OC/x.txt"

# personal data
printf '%s\n' '# test' 'C02ZZ1234567' 'aa:bb:cc:dd:ee:ff' > "$VERMILION_PRIVATE_PATTERNS"
echo "serial C02ZZ1234567" > "$T/repo/leak.txt" && git -C "$T/repo" add leak.txt
expect 2 a "commit with a serial" "git commit -m leak"
git -C "$T/repo" reset -q && echo "fine" > "$T/repo/leak.txt" && git -C "$T/repo" add leak.txt
expect 0 a "commit without private data" "git commit -m ok"
git -C "$T/repo" commit -q -m ok && echo "mac AA:BB:CC:DD:EE:FF" >> "$T/repo/leak.txt"
expect 2 a "commit -a with a MAC (case-insensitive)" "git commit -a -m mac"
expect 2 a "commit <path> with a MAC" "git commit leak.txt -m mac"
expect 2 a "add and commit in one call" "git add leak.txt && git commit -m mac"

echo "guard_test: $pass passed, $fail failed"
[ "$fail" = 0 ]
