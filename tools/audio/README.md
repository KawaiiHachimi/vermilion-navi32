# tools/audio

Measurement tools for audio on macOS. No root, no driver, nothing written to the system.

| Tool | What |
|---|---|
| `audioprobe.m` | `audioprobe list`: every CoreAudio device with transport, streams, physical formats, sample rates, buffer size and range, device / safety / stream latency, data sources and jack state. `audioprobe run <id\|default\|definput> [seconds] [frames] [tone]`: runs an IOProc and reports the callback period (median, p1, p99, p99.9, max), sample-time discontinuities (dropped cycles), HAL overloads and the device clock's drift against the host clock. Output is silence unless `tone` (440 Hz, -30 dBFS). The buffer size is put back afterwards |
| `uacdesc.c` | `uacdesc <vid> <pid>`: decodes the USB Audio Class 1 / 2 descriptors of a USB audio device read-only: clock entities, input / output terminals with their type (speaker, headphones, S/PDIF, microphone, line), and every streaming interface's alternate settings and isochronous endpoint. Tells which CoreAudio device is which jack |

Build (Command Line Tools, x86_64):

```sh
clang -O2 -Wall -fobjc-arc -framework CoreAudio -framework Foundation audioprobe.m -o audioprobe
clang -O2 -Wall -framework IOKit -framework CoreFoundation uacdesc.c -o uacdesc
```

Notes:

- Opening an **input** device (`run definput`, or the id of an input) makes macOS ask for microphone permission
  for the terminal app. The tool records nothing, but say so before running it on someone's machine.
- `usbaudiod` logs which streams it accepts or rejects: `log show --last boot --predicate 'process == "usbaudiod"'`
  (use `/usr/bin/log` in zsh).
- Device UIDs are not printed: USB UIDs contain the port location.

Results on the XTX machine: [`.agents/findings/2026-11-22-audio-connectivity-inventory.md`](../../.agents/findings/2026-11-22-audio-connectivity-inventory.md).
