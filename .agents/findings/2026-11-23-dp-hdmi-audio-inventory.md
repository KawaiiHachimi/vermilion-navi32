# DP / HDMI audio and HDMI output: inventory (thread 04, 2026-11-23)

Read-only on the XTX machine (macOS 26.7.1 25G241, EFI change 23, VermilionBoot b160), plus the Linux mode-set traces
already in `~/vermilion-captures` (2026-11-20 and 2026-11-23, same card, same LG 27GS95QE on DP B / DIG1). Nothing
was written to the GPU. Register reads went through `phxctl regs` as root.

## 1. The PCI side

- The GPU's audio function is `1002:ab30` at `3:0:1` (class `0x040300`, HDA). macOS creates the nub
  `pci1002,ab30`; **no driver attaches**.
- Its codec, read from the DCN side (`AZALIA_F0_CODEC_ROOT_PARAMETER_VENDOR_AND_DEVICE_ID`, `0x38c6`): `1002:aa01`,
  the ATI/AMD HDMI codec every AMD GPU since R6xx uses. Function group in D3 (`0x38ce` = `0x633`), supported rates
  `0x00020070` (32 / 44.1 / 48 kHz, 16-bit), the values Linux's `dce_aud_hw_init` leaves (unverified against a Linux
  read: no Linux dump has these registers).

## 2. macOS: Apple's driver is there, without our ID

- AppleHDA is gone in Tahoe, but **`AppleGFXHDA.kext` 550.1 ships** (System KC): the driver real Macs use for DP / HDMI
  audio from the GPU. Its AMD personality (`AppleGFXHDAEGController`) matches `0xAAF8 0xAAF0 0xABF8 0xAB20 0xAAE0
  0xAB38 0xAB28` (vendor 1002), Navi 21's `ab28` among them, **not Navi 31's `ab30`**.
- What it needs from the graphics side (from its symbols and call sites, disassembled from the KC):
  - `locateAssociatedGraphicsController`: finds the GPU on the same bus / device (function 0) and records `GFX BDF`.
  - An `AppleDisplay` (matching notification) with `IODisplayEDID`; `edidIsValidForAudio` checks the EDID's audio
    support. We have one: AppleDisplay under PhxAMDFB, `IODisplayEDID` present (the LG's EDID advertises audio: Linux
    wrote its short audio descriptors into the codec, section 3).
  - Framebuffer notifications (`addFramebufferNotification`): it handles mode change, sleep / wake, power on / off,
    speed change, clamshell, capture, online change (40) and display dims change (50).
  - Exactly **one** `getAttributeForConnectionExt(0, 'enab')` and `setAttributeForConnectionExt(0, 'aud ', 1 / 0)`
    (audio on / off for the connection; from `AppleGFXHDAEngineOutputDP::init`, `changeDisplayPortOutputPathSet`,
    `addAdditionalDPAudioSource`). Nothing else is asked of the framebuffer.
  - Properties it reads: `hda-gfx`, `connector-type`, `AAPL,DisplayPipe`, `port-number`, `audio-codec-info`,
    `IOFBDependentID` / `Index`, `layout-id`, `built-in`. PhxAMDFB publishes `IOFBDependentID` / `Index` only (no
    `connector-type`, no `hda-gfx`).
- So the macOS side is: a personality for `ab30`, the properties, and PhxAMDFB answering `'aud '` by switching the
  display engine's audio path on and off.

## 3. The display engine side: Linux's sequence is already captured

Every `trace-modeset.txt` / `trace-dpms-on.txt` from `linux_display_capture.py` (ftrace `amdgpu_dc_wreg`) contains
DC's full audio programming for DIG1, at every mode Linux set (60 / 120 / 144 / 240 Hz, HDR, 1080p, 1280x1024, after
DPMS). Decoded with the DCN 3.2 offsets (segments `0x12 0xC0 0x34C0 0x9000 0x2403C00`):

| Order | Registers | What (Linux DC) |
|---|---|---|
| 1 | `AFMT1_AFMT_AUDIO_PACKET_CONTROL`, `DP1_DP_SEC_CNTL` (stream off), `DIG1_AFMT_CNTL` | audio off before the stream change |
| 2 | `AZF0ENDPOINT1_..._ENDPOINT_INDEX/DATA` index `0x54` | pin hot-plug / audio-enabled off |
| 3 | `DCCG_AUDIO_DTBCLK_DTO_PHASE/MODULO` = 0, `DCCG_AUDIO_DTO_SOURCE`, `DCCG_AUDIO_DTO1_MODULE` `0x006d5ac4` (DPREFCLK 716.666 MHz x 10), `DCCG_AUDIO_DTO1_PHASE` `0x0003a980` (24 MHz x 10) | the DP audio clock (DTO1) |
| 4 | `AFMT1_AFMT_AUDIO_SRC_CONTROL` = 1, `AFMT1_AFMT_AUDIO_PACKET_CONTROL2`, `AFMT1_AFMT_MEM_PWR` = 0 | AFMT1 from Azalia stream source 1, its memory powered |
| 5 | endpoint 1, indexes `0x25`-`0x42` | `dce_aud_az_configure`: channel allocation, the short audio descriptors (`0x28`-`0x35`), latency, the sink info (manufacturer / product, port ID, the monitor name) = the ELD |
| 6 | `DP1_DP_SEC_FRAMING4`, `DP1_DP_SEC_CNTL` | secondary data packets on |
| 7 | endpoint 1 index `0x54`, `DIG1_AFMT_CNTL` = 1, `DP1_DP_SEC_AUD_N` `0x8000`, `DP1_DP_SEC_TIMESTAMP` = 1 (auto), `AFMT1_AFMT_AUDIO_PACKET_CONTROL`, `AFMT1_AFMT_INFOFRAME_CONTROL0`, `AFMT1_AFMT_60958_0`, `DP1_DP_SEC_CNTL` `0x00d01111` | audio on: pin present, audio sample / timestamp / info packets |

One write in the same window, `0x52` (`0x110`, then `0x10`, around the DTO writes), decodes to an AZ stream register in
the header; who writes it and why is not known yet (open).

The replay plans drop all of this on purpose (`p0odm_from_trace.py`: `AFMT|^AZ|AUDIO|SEC_AUD|SEC_TIMESTAMP`), but
keep `DP_SEC_CNTL`. So on macOS now (read 2026-11-23): `DP1_DP_SEC_CNTL` = `0x00d01111` (the audio packet enables on,
from the plan) while `DIG1_AFMT_CNTL` = `0x100`, `AFMT1_AFMT_MEM_PWR` = `0x130` (powered down), `AFMT_AUDIO_SRC_CONTROL`
= 0, `DCCG_AUDIO_DTO_SOURCE` = `0x30`, DTO1 phase 0: no audio clock, no audio data.

The values in step 5 come from the EDID (Linux's `fill_audio_info` / `dce_aud_az_configure`), so they are computed
from the live EDID, not copied (CLAUDE.md rule 2). The captured values serve as the test vector for this LG. The
sink info's port-ID words (`SINK_INFO2/3` = `0x5558859e`, `0x0d989449`) are constants in Linux DC
(`dc_stream.c`, used when the DM gives no container ID, as amdgpu_dm doesn't); the rest of the sink info is the EDID's
manufacturer / product ID and the monitor name.

## 4. HDMI output

- Upstream drives HDMI only as the second monitor (pipe 1, DIG3 / PHY D, "M7": `m7_boot.sh`, `PhxPipe2.h`), with plans
  captured from leolivm's Acer 1080p at 60 / 144 / 165 Hz, including SCDC.
- The XTX's VBIOS has the same four connector paths (XTX.md). Nothing for HDMI has run on this machine; an HDMI monitor
  here needs its own Linux capture, like the LG's DP plans did (unverified).

## 5. Open questions

1. Does AppleGFXHDA drive the `ab30` controller with only an ID match (it drives Navi 21's `ab28` on real Macs)?
2. Does it need the ELD in the codec (step 5) or only the pin presence (step 7)? Doing what Linux does covers both.
3. `0x52` (section 3).
4. HDMI: is there an HDMI display to test with?

## 6. AppleGFXHDA on ab30, step by step (2026-11-24, XTX machine, EFI changes 24-27)

What each step showed, with VermilionAudio (Lilu plugin, `src/vermilion-audio/`) and `tools/phxctl/audio_match.sh`:

1. **User space can't add the personality.** `IOCatalogueSendData` from root returns 0, but the kernel logs
   `IOCatalogueSendData(pid N, phxcatalog): Not entitled` and adds nothing. VermilionAudio 1.1.0 adds and removes
   it in the kernel (`gIOCatalogue->addDrivers` / `removeDrivers`) on a write to `debug.phxaudio_match`.
2. **The probe's controller table** (12 entries of 0x2c bytes at `__TEXT.__const`) lacks ab30: entry ab20 rewritten
   to ab30 (1.0.0). Then AppleGFXHDAEGController attaches: `status-mask` 1 (codec 0 on the link), `vendorcodecID`
   0x1002aa01, `GFX BDF` = the GPU (3:0:0).
3. **The codec factories** (`AppleGFXHDAFunctionGroupFactory::createAppleHDAFunctionGroup`,
   `AppleGFXHDAWidgetFactory::createAppleHDAWidget`) pick classes by the controller's device ID; for ab20-ab38 a bit
   mask 0x01000101 (ab20, ab28, ab38). Without ab30: "Sound assertion in AppleGFXHDAFunctionGroupFactory at line
   94" and the driver stops. 1.2.0 sets bit 16 in both masks (ab30 gets ab28's ATI_Tahiti / Widget_1002AAA0). Then
   AppleGFXHDADriver starts (`HDMIDPAudioCapabilities` = Yes).
4. **The display notification is lost.** `locateAssociatedGraphicsController` registers a matching notification on
   AppleDisplay and stores the notifier at controller + 0x1c48 after `addMatchingNotification` returns; for an
   AppleDisplay that exists already, IOKit calls the handler inside `addMatchingNotification`, the handler compares
   the notifier with the still-empty field and returns. 1.3.0 re-delivers it through `_gfxMatchedHandler` once the
   field is set: the handler then accepts the LG's EDID (`edidIsValidForAudio`: 256 bytes, CEA extension rev 3,
   basic audio) and sets `audio-selector` = `dpXX` on PhxAMDFB.
5. **No audio engine yet.** The framebuffer event handler acts on events 1, 2, 3, 4, 7, 8, 9, 10, 20, 40 and first
   asks `getAttributeForConnectionExt(0, 'enab')` (PhxAMDFB answers 1). A display sleep / wake (`pmset
   displaysleepnow`, `caffeinate -u`) delivers events (`IOFBEvent` on the controller), still no IOAudioEngine. Next
   suspects: the codec's pin sense / ELD (the display engine side, section 3, not programmed on macOS) and
   PhxAMDFB not answering `'aud '`.

6. **The framebuffer properties.** The event path reads `connector-type`, `av-signal-type` (0x10 DP, 0x08 HDMI; 0-2
   assert), `AAPL,DisplayPipe` and `audio-codec-info` from the framebuffer: byte 0 codec address, byte 1 function group
   node, byte 2 pin node, bytes 1 and 2 non-zero or "line 3170 / 3171" (`supportsSinkWithCodecInfo` hands all three
   to the path set). VermilionAudio 1.4.0 / 1.5.0 set them on PhxAMDFB (codec info 0x050100 = pin 5 for codec
   endpoint 1, Linux's endpoint for DIG1).
7. **The engine.** 2026-11-24 02:17, EFI change 29: `audio_match.sh on`, the DC replay (90 writes), a display sleep /
   wake: `AppleGFXHDAEngineOutputDP` starts, `startDMAEngine`, CoreAudio lists "LG ULTRAGEAR+" (transport
   DisplayPort, 8 channels, 32-192 kHz, 16-24 bit). One assertion "AppleGFXHDAEngineOutputDP at line 262" (open;
   likely `setAttributeForConnectionExt(0, 'aud ')`, which PhxAMDFB doesn't answer).
8. **Sound.** 2026-11-24 07:53 (after a 07:46 restart, the same three steps by hand, device 99): `audioprobe run 99 5
   512 tone`, 440 Hz at -30 dBFS, 5 s, speakers on the LG's headphone jack: the owner heard it. 469 callbacks in
   4.9 s, period 10.667 ms median (p99.9 10.683 ms), 0 late cycles, 0 sample-time discontinuities, 0 overloads, the
   device clock 47998.8 Hz (-24 ppm vs the host). So pin 5 (0x050100) is the right pin for DIG1 on this machine, and
   the line-262 assertion does not stop the sound.

Assertions seen every time, not fatal: AppleGFXHDAController line 4775 (`setRequireMaxBusStall`, no max-bus-stall
property), AppleGFXHDADriver 4838 + controller 5107 (`setCodecProperties` before the controller's power-on: its
first verb fails).

## 7. Automatic at boot (2026-11-24, XTX machine, EFI changes 30-33)

- `tools/phxctl/phxaudio` computes Linux DC's audio sequence for DIG1 from the EDID (`phxaudio_plan.h`); from the LG's
  EDID in IOKit it equals the traced plan, 90/90 writes. On a fresh boot (18:08) it programmed the display engine and
  the end state matched Linux's; `0x52` read 0 before (Linux's writes leave it at 0x10).
- With the display engine programmed first, the attach alone starts no engine (18:08, 10 s). Delivering
  `kIOFBNotifyDidPowerOn` (8), or 7 then 8, on PhxAMDFB reached AppleGFXHDA's handler and started nothing (18:24).
- A ktrace of `AppleGFXHDAController::framebufferEventHandler` (kdebug subclass 0x0524, debug-ids 0x5248811 / 12, arg4
  high word = the event) during a display sleep / wake: 90 and 92 (WindowServer's defer), 1 and 2 (mode will / did
  change, the mode WindowServer sets again on wake), 91 and 93. No 7 / 8: PhxAMDFB calls `handleEvent` off
  IOFramebuffer's power thread, and IOFramebuffer then drops the event.
- VermilionAudio 1.7.2 delivers 1 then 2 (info 0) on head 0's PhxAMDFB through
  `IOFramebuffer::deliverFramebufferNotification`, inside the controller's work-loop gate. 18:32: engine up 2 s later,
  "LG ULTRAGEAR+" listed, a test stream clean (0 late cycles, 0 discontinuities). Nothing visible on the screen.
- 1.7.0 linked IOGraphicsFamily and did not load: it lives in the System KC, and OpenCore injects into the boot KC.
  1.7.1 resolves the symbol through Lilu when IOGraphicsFamily loads.
- A refresh-rate switch through p0odm (240 -> 144 -> 240, 17:54) left the display engine's audio state as Linux's;
  whether sound continues across it is an owner test.

