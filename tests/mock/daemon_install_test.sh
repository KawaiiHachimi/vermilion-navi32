#!/bin/bash
# SPDX-License-Identifier: MIT
# daemon_install_test.sh: tools/phxctl/daemon_install.sh against a fake ~/gpu. No root needed (run it as root too: the
# chown path), nothing outside a temp dir. Also checks the reason the installer renames instead of writing in place:
# a running bash script whose file is renamed over keeps running its old text.
set -u
HERE=$(cd "$(dirname "$0")" && pwd); I=$HERE/../../tools/phxctl/daemon_install.sh
T=$(mktemp -d "${TMPDIR:-/tmp}/daemon_install.XXXXXX"); P=0; F=0
ok()   { P=$((P+1)); echo "PASS  $1"; }
bad()  { F=$((F+1)); echo "FAIL  $1"; }
check(){ if eval "$2"; then ok "$1"; else bad "$1 ($2)"; fi; }
fresh(){ rm -rf "$T/g" "$T/d"; mkdir -p "$T/g" "$T/d"; printf '#!/bin/bash\necho old\n' >"$T/g/phxboot-navi31.sh"
         printf '#!/bin/bash\necho new\n' >"$T/d/phxboot-navi31.sh"; shasum -a 256 "$T/d/phxboot-navi31.sh" >"$T/d/phxboot-navi31.sh.sha256"; }
run()  { bash "$I" "$T/d" "$T/g" >"$T/out" 2>&1; echo $? >"$T/rc"; }

fresh; run
check install_rc        '[ "$(cat "$T/rc")" = 0 ]'
check install_new       'grep -q "echo new" "$T/g/phxboot-navi31.sh"'
check install_prev      'grep -q "echo old" "$T/g/phxboot-navi31.sh.prev"'
check install_mode      '[ -x "$T/g/phxboot-navi31.sh" ]'
check install_no_tmp    '! ls -a "$T/g" | grep -q "\.new\."'
check install_log       'grep -q "daemon-install: installed .*rollback copy" "$T/out"'
run
check same_again        '[ "$(cat "$T/rc")" = 0 ] && grep -q "already installed" "$T/out" && grep -q "echo old" "$T/g/phxboot-navi31.sh.prev"'

fresh; echo "0000 x" >"$T/d/phxboot-navi31.sh.sha256"; run
check hash_refused      '[ "$(cat "$T/rc")" = 1 ] && grep -q "SHA-256 mismatch" "$T/out" && grep -q "echo old" "$T/g/phxboot-navi31.sh" && [ ! -e "$T/g/phxboot-navi31.sh.prev" ]'
fresh; rm "$T/d/phxboot-navi31.sh.sha256"; run
check nohash_refused    '[ "$(cat "$T/rc")" = 1 ] && grep -q "echo old" "$T/g/phxboot-navi31.sh"'
fresh; printf '#!/bin/bash\nif then fi (\n' >"$T/d/phxboot-navi31.sh"; shasum -a 256 "$T/d/phxboot-navi31.sh" >"$T/d/phxboot-navi31.sh.sha256"; run
check parse_refused     '[ "$(cat "$T/rc")" = 1 ] && grep -q "does not parse" "$T/out" && grep -q "echo old" "$T/g/phxboot-navi31.sh"'
fresh; rm "$T/d/phxboot-navi31.sh"; run
check nosrc_refused     '[ "$(cat "$T/rc")" = 1 ] && grep -q "echo old" "$T/g/phxboot-navi31.sh"'
fresh; rm "$T/g/phxboot-navi31.sh"; run
check first_install     '[ "$(cat "$T/rc")" = 0 ] && grep -q "echo new" "$T/g/phxboot-navi31.sh" && [ ! -e "$T/g/phxboot-navi31.sh.prev" ]'

# the running daemon: a script that runs the installer over itself, then prints a line further down its text
fresh
cat >"$T/g/phxboot-navi31.sh" <<EOF
#!/bin/bash
bash "$I" "$T/d" "$T/g" >/dev/null 2>&1
sleep 0.2
echo "old daemon line after the install"
EOF
bash "$T/g/phxboot-navi31.sh" >"$T/self" 2>&1
check running_keeps_old 'grep -q "old daemon line after the install" "$T/self" && grep -q "echo new" "$T/g/phxboot-navi31.sh"'

if [ "$(id -u)" = 0 ]; then
  fresh; chown 501:20 "$T/g"; run
  check root_chown      '[ "$(stat -f %u:%g "$T/g/phxboot-navi31.sh")" = 501:20 ]'
fi
rm -rf "$T"
echo "daemon_install_test: $P passed, $F failed"
[ $F = 0 ]
