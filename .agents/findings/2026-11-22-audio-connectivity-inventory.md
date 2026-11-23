# 2026-11-22: audio and connectivity inventory and first baselines (thread 04)

> **Correction (2026-11-23, thread 00, from thread 04's final report):** the summary table's "not tested with a peer"
> (AirDrop) and "no iCloud account signed in" rows are out of date: iCloud is signed in, Handoff and AirPlay iPhone ->
> Mac work, BLE advertising works, and AirDrop fails (Mac not visible; with `amtk_awdl_offchan=0` and the router on
> DFS channel 52 the Mac never transmits in AWDL). The `missingEndpoint` status quoted from MtkWireless 1.1.1's docs
> is what 1.1.2 claims to fix. Unverified, written as plausible: the clipping cause, "Universal Clipboard needs AWDL",
> the TCC cause of the input start failure, "the Ethernet driver works" (no link was ever tested).

Machine: the XTX machine, macOS 26.7.1 25G241, booted 20:27 (EFI b150 + USB map + CPU sensors + Bluetooth, boot-args
`-v keepsyms=1 debug=0x100 pci=0x8000000 amfi=0x80 ipc_control_port_options=0 phxdebug=1`), 1440p desktop, measured
21:40-22:15. All read-only except the AWDL A/B test (`ifconfig awdl0 down` / `up`, restored).

## Summary

| Area | State | Note |
|---|---|---|
| Onboard audio | **partly**: 2 of 6 streams published; the analog line-out / speaker and headphone outputs are missing | below; AppleALC does not apply (USB codec) |
| Wi-Fi MT7922 | works, but **router ping 100 ms average / 456 ms max, every ~1.05 s**: AWDL off-channel windows | cause proved by A/B/A below |
| Bluetooth | works (controller On, LE / LEA / GATT services listed) | AirPods paired (owner, earlier) |
| Ethernet I225-V | Apple DriverKit E1000 bound, 10 M-2.5 G media offered, **no link** | cable? (owner) |
| AirDrop | AWDL up, discovery mode Everyone; not tested with a peer | |
| Handoff / Universal Clipboard / Sidecar / Universal Control | **no iCloud account signed in**, so none can work yet | thread 05 (iServices) first |
| DP / HDMI audio | not implemented in the stack (GPU side) | thread 00 |

## Audio: ASUS USB audio `0b05:1a5c` (Realtek ALC4080, USB Audio Class 2)

The board has no HDA codec: the ALC4080 is a USB device on chipset port 11 (USB map). So **AppleALC and layout IDs
do not apply**. macOS 26 drives it with `usbaudiod` (Apple's user-space USB audio driver, `AUAPlugin`; no kext).

Descriptors (`tools/audio/uacdesc 0x0b05 0x1a5c`, read-only):

| Interface | Terminal | Clock | Formats offered | macOS |
|---|---|---|---|---|
| 1 (in) | analog connector (`0x0601`) | 1 | 2 ch, 16 / 24-bit | **published**: device "Realtek USB2.0 Audio", default input |
| 2 (in) | line connector (`0x0603`) | 3 | 2 ch, 16 / 24-bit | removed |
| 3 (in) | microphone (`0x0201`) | 4 | 2 ch, 16 / 24-bit | removed |
| 4 (out) | speaker (`0x0301`), 7.1 | 5 | 2 / 4 / 6 / 8 ch, 16 / 24 / 32-bit | **removed** |
| 5 (out) | headphones (`0x0302`) | 6 | 2 ch, 16 / 24 / 32-bit (21 alternate settings) | **removed** |
| 6 (out) | S/PDIF interface (`0x0605`) | 8 | 2 ch, 16 / 24-bit PCM + IEC61937 (format type 3) | **published**: default output |

- `usbaudiod` log at boot, for each of the four: `AUAStream publishFormats could not find a valid format`, then
  `Removing input/output stream`. The six CoreAudio devices all exist (all named "Realtek USB2.0 Audio"), but four
  have no streams. The default output (device 85, 88.2 / 176.4 kHz pattern, no 32-bit) is the **optical S/PDIF**
  output, so sound from the analog jacks is not expected *(unverified by ear)*.
- Why the formats are rejected is open. Candidates *(all unverified)*: jack sensing (the ALC4080 may report no
  valid alternate setting for an unplugged jack at enumeration), the many duplicate alternate settings per format
  (one per rate family, which Linux handles with a rate-validation quirk), or the 32-bit alternates. The next test
  is cheap and needs no reboot: note what is plugged into which jack, restart `usbaudiod`, read its log again.
- Both published streams run **16-bit**, although 24-bit is offered (CoreAudio's choice). 24-bit can be selected
  in Audio MIDI Setup (owner's call; not changed).

Baseline (`audioprobe run 85 20 <frames>`, S/PDIF output, silence, 48 kHz):

| Buffer | Reported output latency | Callback period median / p99.9 / max | Overloads, sample-time gaps |
|---|---|---|---|
| 512 frames (10.67 ms) | 11.25 ms | 10666 / 10675 / 10681 us | 0, 0 |
| 128 frames (2.67 ms) | 3.25 ms | 2667 / 2673 / 2675 us | 0, 0 |
| 32 frames (0.67 ms) | 1.25 ms | 667 / 672 / 687 us | 0, 0 |

Device clock vs host clock: +24 ppm. `usbaudiod`: hardware timestamping off ("platform Intel"), polling the USB
frame counter; its timestamp error stays under 7 us. No dropout in 60 s at any buffer size. The input (device 76)
did not start (`AudioDeviceStart` returned `0x10004003`); most likely the microphone privacy permission
*(unverified)*, which macOS asks for when any input is opened. Not retried (owner: no microphone use).

## Wi-Fi: periodic latency stalls caused by AWDL

Router ping (`ping -i 0.1`, 300 s, AWDL up): **p50 47 ms, p90 308 ms, p99 418 ms, max 456 ms**, 0.1 % loss, only
36 % of replies under 5 ms. The replies come in bursts: the link is silent for ~400 ms (median gap 406 ms, max
555 ms), then 4 queued replies arrive at once, every **1.047 s** (median) ≈ 1024 TU. RSSI -56 to -61 dBm, MCS 9,
2 streams, 867 Mbit/s, CCA 0 %, channel 52 / 80 MHz all the time; no scans or roams in the log.

- AWDL stays in schedule state "Low Power", channel sequence `44 44 44 0 0 0 0 0 6 44 44 0 0 0 0 0`: 6 of 16 slots
  on channels 44 and 6, away from the router's channel 52. 16 slots of 64 TU = 1024 TU, the period seen.
- IO80211Family logs every second: `monitorAWDLState: mismatch of channel sequence with Firmware, force to program
  Low Power` and `setScheduleState ... force:YES`. The driver's sequence never matches what IO80211 wants.
- MtkWireless 1.1.1 docs (`AWDL-CHANNEL-TIMING.md`): associated off-channel windows are capped at **300 ms** and
  preceded by a power-save handshake with the AP, which fits a ~400 ms silence.

A/B/A (same session, nothing else changed):

| | AWDL up | AWDL down (`ifconfig awdl0 down`) |
|---|---|---|
| router ping avg / max (30 s, 10 Hz) | 101 / 456 ms, then 74 / 423 ms | 1.78 / 4.7 ms, 1.81 / 5.2 ms |
| `networkQuality -s` down / up | 64.3 / 32.1 Mbit/s | 101.0 / 47.9 Mbit/s |
| idle latency | 143 ms | 14 ms |

The internet line limits the AWDL-down figures; a LAN throughput test needs an `iperf3` peer *(none yet)*. This
explains the "slow every few minutes" note of 2026-11-19, at least partly: the stalls are continuous, and they
hurt most whenever traffic is interactive *(the 5-minute pattern itself is not reproduced)*.

Fix options (MtkWireless boot-args, `WIFI.md`), one per boot:

1. `amtk_awdl_offchan=0`: AWDL stays on the AP's channel while associated. Docs: "AirDrop to phones on other channels
   fails". **Recommended first**: keeps AWDL and should remove the stalls *(unverified)*.
2. `amtk_awdl=0`: no AWDL at all: no AirDrop, AirPlay to Mac, Sidecar or Universal Control over Wi-Fi.
3. No EFI change: keep `awdl0` down from a LaunchDaemon and bring it up when AirDrop is wanted (macOS may raise it
   again by itself).

## Ethernet: Intel I225-V (`8086:15f3` rev 3)

`DriverKit_AppleEthernetE1000` (Apple's dext) is bound under `DP50.EP00`, `en3`. It offers 10/100/1000/2500BASE-T
with flow control and EEE. `IOLinkStatus = 1` (valid, not active), media `none`. Nothing in its log beyond start-up.
So the driver works as far as can be seen without a link; the next step is a cable (owner). The `e1000=0` /
AppleIntelI210Ethernet fallback in TODO is only needed if a link fails.

## Bluetooth and Continuity

- Controller: `THIRD_PARTY_DONGLE`, USB, On, firmware `v9489 c6166`, services HFP AVRCP A2DP HID LEA AACP GATT.
- `sharingd`, `rapportd`, `bluetoothd` running. AWDL on, AirDrop discovery Everyone.
- No iCloud account is signed in, so Handoff, Universal Clipboard, Sidecar and Universal Control cannot work yet.
  They need thread 05 (SMBIOS / iServices) first. AirDrop needs no iCloud: test with a phone after the Wi-Fi fix.

## Tools added

- `tools/audio/audioprobe.m`: every CoreAudio device (transport, streams, formats, rates, buffer range, latency,
  safety offset, jacks), and an IOProc timing run (callback period distribution, sample-time gaps, HAL overloads).
- `tools/audio/uacdesc.c`: decodes a USB audio device's UAC1 / UAC2 descriptors read-only (terminals, clocks,
  alternate settings, endpoints), for the onboard codec and any USB DAC / audio interface.

## Boot 22:23 with `amtk_awdl_offchan=0` (EFI change 4): Wi-Fi fixed

- `nvram boot-args` carries `amtk_awdl_offchan=0`. Router ping 10 Hz, 60 s, AWDL **up**: avg 1.73 ms, max 4.19 ms,
  0 % loss (was 101 / 456 ms). `networkQuality -s`: 99.6 / 50.4 Mbit/s, idle latency 14.9 ms (line-limited, the same
  as with `awdl0` down). AWDL schedule state "Data", sequence still lists 44 / 6, but the radio stays on channel 52;
  the per-second "mismatch of channel sequence" lines are gone (0 in 2 min). RSSI -57, 867 Mbit/s.
- Bluetooth On, all kexts loaded, the GPU daemon `done`, 1440p Metal desktop: unchanged.
- AirDrop with a phone: not tested yet (the documented cost of the boot-arg).

## Audio after plugging the speaker into the rear line-out (boot 22:23)

- **Jack sensing confirmed:** with a cable in the rear line-out at boot, `usbaudiod` publishes the speaker stream
  (interface 4, terminal `spkr`, device 74), and it became the default output. Headphones, line-in and mic (nothing
  plugged) are still rejected. So the ALC4080 offers a stream only for a jack that is plugged in when `usbaudiod`
  enumerates it. Whether a hot-plug after boot is picked up is *(unverified)*.
- **Volume:** the macOS slider drives the codec's feature unit 29 directly (hardware gain, -65.25 to 0 dB; no
  software gain, no boost). The slider's steps, from `usbaudiod`'s log: 100 % = 0 dB, 94 % = -2.25, 88 % = -4.5,
  81 % = -6.75, 75 % = -9, 69 % = -11.25, 62 % = -13.5, 56 % = -16.5, 50 % = -18.75, 44 % = -21.75, 38 % = -25.5,
  31 % = -28.5 dB.
- **Clipping (owner, 22:35):** an active Bluetooth speaker pair on aux, speaker volume low, macOS at 100 %, distorts
  on kicks and 808s. At 100 % the codec sends full scale (0 dB); ASUS drives the rear outputs with a headphone amp
  (Savitech SV3H712, impedance sensing), so the level is likely far above what a portable speaker's aux input takes,
  and its input stage clips before its own volume control *(unverified: no level measurement)*. Fix is gain
  staging, no driver change: macOS around 50-60 %, speaker volume up.

## Correction (22:45): iCloud is signed in

The owner signed in to iCloud earlier; `MobileMeAccounts` has an account at 22:45 (the 21:40 read was empty). So
Handoff / Universal Clipboard / Sidecar / Universal Control are testable now, and iCloud is not why AirDrop fails.
AirDrop (owner, 22:44): the Mac does not appear on the iPhone. sharingd starts its AirDrop Bonjour server; bluetoothd
gets advertising requests. Suspects: `amtk_awdl_offchan=0` (AWDL kept on channel 52) and MtkWireless's own AirDrop
state (its `BLE-ADVERTISING.md`: advertising verified upstream, transfers still fail with `missingEndpoint`).

## Continuity tests (owner, 22:50, boot 22:23 with `amtk_awdl_offchan=0`)

| Feature | Result |
|---|---|
| Handoff (Safari iPhone -> Mac) | **works** |
| AirPlay iPhone screen -> Mac | **works** (over the router) |
| Universal Clipboard (iPhone -> Mac) | fails |
| AirDrop | fails (Mac not visible) |
| Continuity Camera | not tested yet |

Handoff needs only BLE advertising and iCloud, so Bluetooth LE advertising works. The failures are the features that
need a direct peer-to-peer Wi-Fi (AWDL) link *(unverified mechanism)*. Owner's decision 2026-11-22: keep
`amtk_awdl_offchan=0` (Wi-Fi latency over AirDrop / Universal Clipboard).

## Continuity Camera over USB (owner, 22:53): camera listed, black picture

The iPhone appears as a camera (Photo Booth) over a USB cable; the iPhone's USB network links come up (`AppleUSBNCMData`,
link-local only). ContinuityCaptureAgent starts the stream, then `no frames after timeout`, `createBlackSampleBuffer`
and `start stream failure ... Code=-536870186` (`0xE00002D6`, kIOReturnTimeout). So no video frame ever arrives;
decoding never starts (VCN being off is not the cause here). rapportd logs TCP connects to the phone failing with
`EADDRNOTAVAIL` on one of the USB links. Whether the media path needs AWDL even when wired, i.e. whether
`amtk_awdl_offchan=0` causes this too, is *(unverified)*.
