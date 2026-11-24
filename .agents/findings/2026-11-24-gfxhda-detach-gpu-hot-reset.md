# Detaching AppleGFXHDA from ab30 hot-resets the whole GPU at the next attach (2026-11-24)

Thread 04, XTX machine (RX 7900 XTX rev 0xC8, macOS 26.7.1 25G241), EFI change 29 (VermilionAudio 1.5.0).

## What happened

At 07:53 the DP audio path worked (a test tone on the LG's jack). At 07:55:14 I ran `audio_match.sh off`
(`debug.phxaudio_match=0`: the personality removed, `terminate()` on the AppleGFXHDA controller), then
`audio_match.sh on` 3 s later. The point was to see whether the engine comes up without the display sleep / wake when
the display engine is already programmed. From the unified log (`/usr/bin/log show`):

| Time | What |
|---|---|
| 07:55:14.863 | AppleGFXHDADriver / Engine / Controller terminate, IOAudioEngine stopped (normal) |
| 07:55:14.864 | `IOPCIFamily [detachFromChild()] Marking HDAU as needing hardware reset` |
| 07:55:18.317 | at the re-attach: `[getResources()] Performing hot reset on nub HDAU`, `HDAU::reset(0x1, 0x0)`, `VGA::saveDeviceState`, `kConfigOpPaused(shadowed) at 3:0:0 (0x1002:0x744c)` and `3:0:1 (0x1002:0xab30)` |
| 07:55:18.318 | WindowServer: `radv-darwin: gem va map failed ... -> -12`, `vkAllocateMemory(image) -> -2`, "ws gpu present: plane lost" |
| 07:55:29 | daemon `idledpm`: mode switch with `otg0_htotal=0x00000000` (OTG0 no longer programmed) |
| 07:55:31 | `idledpm`: DAL `SetHardMinByFreq` to the SMU: "no response" |
| 07:55:30-33 | WindowServer crashes: a stack overflow, SkyLight `RenderEncoder` / `SetScissor` recursing because `-[NVMTLCommandQueue commandBuffer]` fails |
| 07:56:11 | WindowServer userspace watchdog timeout; the machine stays unusable until a power-off (restarted 17:27, cold; bring-up OK) |

## Why

IOPCIFamily (apple-oss-distributions/IOPCIFamily, `IOPCIDevice.cpp`, `IOPCIDevice::detachFromChild`): when a driver
leaves an `IOPCIDevice` that is not itself going away (not an unplug), whose driver did not crash, and the device doesn't
override `shouldSkipReset()`, the nub is marked `hardwareResetNeeded`. The next match resets it: "function level reset
if supported, else fall back to hot reset". The GPU's audio function 1002:ab30 has no FLR, so the fallback is a hot
reset of the link, and functions 0 (VGA) and 1 (HDAU) sit behind the same link: the whole GPU is reset under a running
macOS, with our kexts and the SMU state left behind.

The same applies to any driver detached from the GPU's function 0 and matched again (unverified: not tried, and not to
be tried).

## What changed

- VermilionAudio 1.6.0: the attach is one-way, once per boot. The personality is removed from the catalogue right after
  the attach (`removeDrivers(..., false)` leaves the attached driver alone), so nothing can match ab30 a second time;
  the controller is never terminated; `debug.phxaudio_match` accepts one write of 1, later writes return EBUSY.
- `tools/phxctl/audio_match.sh`: no `off` (it prints why and exits 2); a second `on` in the same boot does nothing.
  Mock test `tests/mock/audio_match_test.sh`.
- Rule for this machine: never terminate or unload AppleGFXHDA (or any driver on the GPU's PCI functions) in a running
  boot. To undo the audio attach, restart.

Open for thread 05 (sleep and wake): whether IOPCIFamily resets the functions on system sleep / wake by another path
(unverified).
