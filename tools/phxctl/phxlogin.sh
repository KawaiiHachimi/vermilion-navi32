#!/bin/sh
# phxlogin.sh: keep the console coming up as the desktop user on this lab machine.
#
# Why this exists: phxboot-navi31.sh deletes autoLoginUser at every start and shutdown (so a user cannot log in
# while it arms), and macOS tears /etc/kcpassword down with it. The daemon re-enables auto-login just before its
# WindowServer restart, but by then the credential is gone, so the console stays at the login window on most
# boots. This daemon restores the credential early and then, once phxboot has finished, retries the loginwindow
# restart until the console belongs to the user (auto-login only happens when loginwindow starts).
set -u
G=${PHX_G:-/Users/cool/gpu}
USER_NAME=${PHX_LOGIN_USER:-cool}
SRC=$G/kcpassword.bin
LOG=$G/phxlogin.log
say(){ echo "$(date '+%T') $*" >>"$LOG"; }

restore(){
  if [ -s "$SRC" ] && [ ! -s /etc/kcpassword ]; then
    cp "$SRC" /etc/kcpassword && chmod 600 /etc/kcpassword && chown root:wheel /etc/kcpassword && say "restored /etc/kcpassword"
  fi
  /usr/bin/defaults write /Library/Preferences/com.apple.loginwindow autoLoginUser -string "$USER_NAME" 2>/dev/null
}

console_user(){ /usr/bin/stat -f %Su /dev/console 2>/dev/null; }

say "start (console $(console_user))"
restore
# Restarting the loginwindow is only ever an aid: it is off unless ~/gpu/phxlogin-force exists, because a
# killall races with the daemon's own single restart and with macOS tearing the credential down, and a boot that
# ends with the console at the login window is a normal, usable state (the user can log in). The credential
# restore above is the part that matters: it is what the daemon needs at its own restart.
FORCE=$G/phxlogin-force
if [ ! -e "$FORCE" ]; then say "credential restored; loginwindow restart not requested (no $FORCE)"; exit 0; fi

# phxboot owns /var/log/phxboot.log and writes a done line per boot; wait for this boot's.
for i in $(seq 1 150); do grep -qa '^done ' /var/log/phxboot.log 2>/dev/null && break; sleep 2; done
say "phxboot finished (or 300 s elapsed), console $(console_user)"

# Auto-login is consumed by loginwindow at its start, so restart it until the user owns the console.
attempt=0
for i in $(seq 1 12); do
  [ "$(console_user)" != root ] && { say "console now $(console_user)"; exit 0; }
  [ $attempt -ge 2 ] && break
  restore
  attempt=$((attempt + 1))
  say "attempt $attempt: restarting loginwindow with auto-login set"
  /usr/bin/killall loginwindow 2>/dev/null
  sleep 20
done
[ "$(console_user)" = root ] && say "gave up after $attempt attempt(s); console still root"
exit 0