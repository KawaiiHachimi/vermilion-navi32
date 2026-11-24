#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# audio_replay_plan.py (thread 04, 2026-11-24): from a Linux mode-set trace (linux_display_capture.py, ftrace
# amdgpu_dc_wreg) of a DP monitor on DIG1, print DC's audio-enable writes as "reg value" lines, for a replay on macOS
# while the stream already runs (tools/phxctl/audio_dc_replay.sh). Prototype: the kext will compute the same values from
# the EDID instead (CLAUDE.md rule 2).
# Kept, from the first DTO write to the last write of reg 0x52: the audio clock (DCCG DTO1, reg 0x52 around it), AFMT1, the codec endpoint 1 (index / data: pin control,
# channel allocation, short audio descriptors, latency, sink info = the ELD), the audio packets. Left out: everything
# before the first DTO write (DC's "audio off" for the stream change) and the stream's own DP_SEC_CNTL / FRAMING4 /
# SEC_CNTL2 writes (the plans already left them at their final values).
# The sink info words come from the monitor (the port-ID words may derive from the EDID serial: unverified), so the
# printed plan is for the machine, not for git.
# Usage: audio_replay_plan.py <trace-modeset.txt>
import re, sys

AUDIO = {0x0052, 0x016b, 0x016e, 0x016f, 0x3521, 0x3522, 0x384c, 0x384d,
         0x5635, 0x5638, 0x5642, 0x5643, 0x5645, 0x5647, 0x5670, 0x56f1, 0x56f5}
writes = []
for line in open(sys.argv[1], errors='replace'):
    m = re.search(r'amdgpu_dc_wreg: reg=(0x[0-9a-f]+), value=(0x[0-9a-f]+)', line)
    if m and int(m.group(1), 16) in AUDIO:
        writes.append((int(m.group(1), 16), int(m.group(2), 16)))
start = next((i for i, (r, v) in enumerate(writes) if r == 0x0052 and v == 0x110), None)
if start is None:
    sys.exit('no DTO programming (reg 0x52 = 0x110) in the trace')
end = max(i for i, (r, v) in enumerate(writes) if r == 0x0052)   # past the packets-on write: DC's last DTO update
plan, last_index = [], None
for r, v in writes[start:end + 1]:
    if r == 0x384c:            # the endpoint index: Linux writes it twice before a read-modify-write; once is enough
        if v == last_index and plan and plan[-1][0] == 0x384c:
            continue
        last_index = v
    plan.append((r, v))
for r, v in plan:
    print(f'{r:#06x} {v:#010x}')
