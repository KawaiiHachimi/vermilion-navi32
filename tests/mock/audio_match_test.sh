#!/bin/bash
# SPDX-License-Identifier: MIT
# audio_match_test.sh: tools/phxctl/audio_match.sh against a fake kmutil / sysctl / ioreg. No root, no kext touched.
# The point: the attach runs once per boot and there is no detach (a detach makes the next attach hot-reset the GPU,
# XTX machine 2026-11-24). macOS, or Linux through lib/portable.sh.
set -u
HERE=$(cd "$(dirname "$0")" && pwd); S=$HERE/../../tools/phxctl/audio_match.sh
. "$HERE/lib/portable.sh"
T=$(mktemp -d "$MOCKTMP/audiomatch.XXXXXX"); PASSN=0; FAILN=0
fake() {   # fake <match 0|1> [busy]: the sysctl state in $T/match; "busy" = the kernel refuses a write with EBUSY
  mkdir -p "$T/bin"; echo "$1" >"$T/match"; : >"$T/calls"; rm -f "$T/busy" "$T/noengine" "$T/evengine" "$T/noevent"; [ "${2:-}" = busy ] && : >"$T/busy"
  cat >"$T/bin/kmutil" <<EOF
#!/bin/bash
echo "kmutil \$*" >>"$T/calls"
EOF
  cat >"$T/bin/sysctl" <<EOF
#!/bin/bash
echo "sysctl \$*" >>"$T/calls"
case "\$1" in
  -n) case "\$2" in debug.phxaudio_match) cat "$T/match";; debug.phxaudio_stat) echo "patched: test";; *) echo 0;; esac;;
  -w) case "\$2" in debug.phxaudio_match=*) [ -e "$T/busy" ] && { echo "sysctl: Device busy" >&2; exit 1; }
                    echo "\${2#*=}" >"$T/match";;
                    debug.phxaudio_event=8) [ -e "$T/noevent" ] && exit 1; [ -e "$T/evengine" ] && rm -f "$T/noengine";; esac;;
esac
EOF
  cat >"$T/bin/ioreg" <<EOF
#!/bin/bash
[ "\$(cat "$T/match")" = 1 ] || exit 0
case "\$*" in
  *AppleGFXHDAController*) echo "+-o AppleGFXHDAEGController@0,1  <class AppleGFXHDAEGController>";;
  *AppleGFXHDAEngineOutputDP*) [ -e "$T/noengine" ] || echo "+-o AppleGFXHDAEngineOutputDP  <class AppleGFXHDAEngineOutputDP>";;
esac
exit 0
EOF
  chmod +x "$T/bin/"*; }
run() { ENGINE_WAIT=${EW:-1} KMUTIL="$T/bin/kmutil" SYSCTL="$T/bin/sysctl" IOREG="$T/bin/ioreg" sh "$S" "$@" >"$T/out" 2>&1; echo $? >"$T/rc"; }
check() {   # check <name> <cond> ...: every cond (a shell test) must hold
  local name=$1 ok=1 c; shift
  for c in "$@"; do eval "$c" || { ok=0; echo "    failed: $c"; }; done
  if [ $ok = 1 ]; then PASSN=$((PASSN+1)); echo "PASS  $name"; else FAILN=$((FAILN+1)); echo "FAIL  $name"; sed 's/^/    /' "$T/out"; fi; }

fake 0; run on
check on_first '[ "$(cat "$T/rc")" = 0 ]' 'grep -q "debug.phxaudio_match=1" "$T/calls"' 'grep -q "kmutil load" "$T/calls"' \
  'grep -q "debug.phxaudio_codecinfo=327936" "$T/calls"' 'grep -q "controllers attached: 1" "$T/out"' \
  'grep -q "DP audio engine up after 0 s" "$T/out"'

fake 0; : >"$T/noengine"; run on
check on_no_engine '[ "$(cat "$T/rc")" = 1 ]' 'grep -q "no DP audio engine after 1 s" "$T/out"' 'grep -q "controllers attached: 1" "$T/out"' \
  '! grep -q "phxaudio_event" "$T/calls"'

fake 0; : >"$T/noengine"; : >"$T/evengine"; EW=4 run on
check on_event_brings_engine '[ "$(cat "$T/rc")" = 0 ]' 'grep -q "debug.phxaudio_event=8" "$T/calls"' \
  'grep -q "display power-on event delivered" "$T/out"' 'grep -q "DP audio engine up after 2 s" "$T/out"'

fake 0; : >"$T/noengine"; : >"$T/noevent"; EW=3 run on
check on_event_refused '[ "$(cat "$T/rc")" = 1 ]' 'grep -q "debug.phxaudio_event=8 refused" "$T/out"' 'grep -q "no DP audio engine after 3 s" "$T/out"' \
  '[ "$(grep -c phxaudio_event "$T/calls")" = 1 ]'

fake 1; run on
check on_again_noop '[ "$(cat "$T/rc")" = 0 ]' '! grep -q "phxaudio_match=" "$T/calls"' '! grep -q "kmutil" "$T/calls"' \
  'grep -q "already attached this boot" "$T/out"'

fake 1; run off
check off_refused '[ "$(cat "$T/rc")" = 2 ]' '! grep -q "phxaudio_match=0" "$T/calls"' 'grep -q "hot-reset the GPU" "$T/out"' \
  '[ "$(cat "$T/match")" = 1 ]'

fake 0 busy; run on
check on_kernel_refuses '[ "$(cat "$T/rc")" = 1 ]' 'grep -q "failed" "$T/out"'

fake 1; run status
check status '[ "$(cat "$T/rc")" = 0 ]' 'grep -q "attached this boot: 1" "$T/out"' '! grep -q "phxaudio_match=" "$T/calls"'

rm -rf "$T"
echo "audio_match: $PASSN passed, $FAILN failed"
[ $FAILN = 0 ]
