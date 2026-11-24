#!/bin/sh
# SPDX-License-Identifier: MIT
# audio_dc_replay.sh (thread 04, 2026-11-24, prototype): replay Linux DC's DP audio-enable writes for DIG1 on the
# running stream, from a plan of "reg value" lines (tools/capture/audio_replay_plan.py <trace-modeset.txt>). Root.
#   audio_dc_replay.sh <plan>     write every line in order through phxctl wreg, log the readback
# Every write is one Linux amdgpu sends on this ASIC for this monitor and mode (CLAUDE.md hard rule 1); the plan holds
# no other registers (the script refuses any outside the audio set). Nothing persists: a restart undoes it.
# Env: PHXCTL (default ~/gpu/phxctl.bin), LOG (default stdout).
set -u
PHXCTL=${PHXCTL:-$HOME/gpu/phxctl.bin}
PLAN=${1:?usage: audio_dc_replay.sh <plan>}
ALLOWED=" 0x0052 0x016b 0x016e 0x016f 0x3521 0x3522 0x384c 0x384d 0x5635 0x5638 0x5642 0x5643 0x5645 0x5647 0x5670 0x56f1 0x56f5 "
while read -r r v; do
  case "$ALLOWED" in *" $r "*) ;; *) echo "audio: refused: $r is not a DC audio register" >&2; exit 1;; esac
done < "$PLAN"
n=0
while read -r r v; do
  out=$("$PHXCTL" wreg "$r" "$v" 2>&1) || { echo "audio: wreg $r $v failed: $out" >&2; exit 1; }
  n=$((n + 1)); echo "audio: $n $r <- $v: $out"
done < "$PLAN"
echo "audio: $n writes"
