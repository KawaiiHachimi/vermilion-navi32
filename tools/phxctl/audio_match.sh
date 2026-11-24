#!/bin/sh
# SPDX-License-Identifier: MIT
# audio_match.sh (thread 04, 2026-11-24): put Apple's AppleGFXHDA (the DP / HDMI audio driver of real Macs with AMD
# GPUs) on the RX 7900's audio function 1002:ab30, once per boot. Root. Nothing persists across a restart.
#   audio_match.sh on      load AppleGFXHDA, attach it to ab30 (once; a second `on` does nothing), report what
#                          attached, wait up to ENGINE_WAIT s (10) for the DP audio engine; when none came within
#                          2 s, deliver a display mode change (debug.phxaudio_event=1 then 2, VermilionAudio 1.7.2):
#                          AppleGFXHDA starts the engine on it, as after a display wake (nothing on the screen changes)
#   (run tools/phxctl/phxaudio apply first: the engine starts only for a pin the display engine reports present)
#   audio_match.sh status  the patch state, whether the attach ran, the driver on ab30, the audio engines
# There is no `off`, on purpose: a driver that leaves ab30 makes IOPCIFamily mark it "needing hardware reset", and the
# next attach hot-resets the whole GPU (XTX machine 2026-11-24 07:55: VRAM mappings failed, the display and the SMU
# went down, WindowServer crashed, a power-off was needed). Never terminate or unload AppleGFXHDA on this machine.
# Needs VermilionAudio.kext 1.7.2 (EFI stage vermilion-trust-bar1g-audio). It patches AppleGFXHDA's controller table
# and its two codec factories (they lack ab30) when AppleGFXHDA loads, and on a write to debug.phxaudio_match adds the
# personality, re-delivers the display, then removes the personality again (the attached driver stays). User space
# can't add it: IOCatalogueSendData from root is refused ("Not entitled", macOS 26.7.1, 2026-11-24).
# Run after the GPU bring-up (the daemon's done), never before: the audio controller is part of the GPU.
# CODECINFO (default 0x050100: codec 0, function group 1, pin 5 = codec endpoint 1, which Linux programs for DIG1 on
# the XTX machine; unverified for other machines) becomes head 0's audio-codec-info (debug.phxaudio_codecinfo).
# Env for tests: KMUTIL, SYSCTL, IOREG, ENGINE_WAIT.
set -u
KMUTIL=${KMUTIL:-/usr/bin/kmutil}; SYSCTL=${SYSCTL:-/usr/sbin/sysctl}; IOREG=${IOREG:-/usr/sbin/ioreg}
patch_state() { $SYSCTL -n debug.phxaudio_stat 2>/dev/null || echo "no debug.phxaudio_stat (VermilionAudio not loaded)"; }
match_state() { $SYSCTL -n debug.phxaudio_match 2>/dev/null || echo "none (VermilionAudio older than 1.1.0)"; }
attached() { $IOREG -r -c AppleGFXHDAController -d1 2>/dev/null | grep -c '^+-o'; }
engines() { $IOREG -r -c AppleGFXHDAEngineOutputDP -d1 2>/dev/null | grep -c '^+-o'; }
case "${1:-status}" in
  on)
    if [ "$(match_state)" = 1 ]; then echo "audio: already attached this boot; nothing done (once per boot)"; exit 0; fi
    $KMUTIL load -b com.apple.driver.AppleGFXHDA >/dev/null 2>&1   # loading it runs VermilionAudio's patch
    echo "audio: patch: $(patch_state)"
    $SYSCTL -w debug.phxaudio_codecinfo=$((${CODECINFO:-0x050100})) >/dev/null 2>&1 || echo "audio: no debug.phxaudio_codecinfo (VermilionAudio older than 1.5.0)"
    $SYSCTL -w debug.phxaudio_match=1 >/dev/null || { echo "audio: adding the personality failed" >&2; exit 1; }
    i=0; while [ $i -lt 20 ] && [ "$(attached)" = 0 ]; do sleep 0.25; i=$((i + 1)); done
    echo "audio: AppleGFXHDA controllers attached: $(attached)"
    w=${ENGINE_WAIT:-10}; ev=""; i=0
    while [ $i -lt $((w * 4)) ] && [ "$(engines)" = 0 ]; do
      if [ $i = 8 ] && [ -z "$ev" ]; then   # 2 s: AppleGFXHDA starts the engine on a display mode change
        ev=1
        if $SYSCTL -w debug.phxaudio_event=1 >/dev/null 2>&1 && $SYSCTL -w debug.phxaudio_event=2 >/dev/null 2>&1; then
          echo "audio: no engine after 2 s: display mode change events delivered"
        else echo "audio: no engine after 2 s; debug.phxaudio_event=1 / 2 refused (VermilionAudio older than 1.7.2?)"; fi
      fi
      sleep 0.25; i=$((i + 1))
    done
    if [ "$(engines)" = 0 ]; then echo "audio: no DP audio engine after $w s"; exit 1; fi
    echo "audio: DP audio engine up after $((i / 4)) s";;
  off)
    echo "audio: no 'off': detaching AppleGFXHDA makes the next attach hot-reset the GPU (see the header)" >&2; exit 2;;
  status)
    echo "audio: patch: $(patch_state)"
    echo "audio: attached this boot: $(match_state)"
    echo "audio: head 0 audio-codec-info: $($SYSCTL -n debug.phxaudio_codecinfo 2>/dev/null || echo none)"
    echo "audio: AppleGFXHDA controllers attached: $(attached)"
    $IOREG -r -c IOAudioEngine -d1 2>/dev/null | grep -E '^\+-o' | sed 's/<class.*//';;
  *) echo "usage: audio_match.sh on|status" >&2; exit 2;;
esac
