# 2026-11-22: Bluetooth on the XTX machine (MediaTek MT7922, USB `0489:e0e2`)

> **Note (2026-11-23, thread 00):** "the black boot was unrelated to Bluetooth" rests on one bad and one good boot; the
> logs point to DP link training (thread 01). MtkWirelessBT's feature list (BLE advertising partial, mSBC) is the
> author's claim; only A2DP with AirPods and BLE advertising (Handoff) were seen here.

Since the USB map (same day) the controller enumerates on chipset port 16 as `Wireless_Device` `0489:e0e2`, with
no driver. System Information still shows Apple's placeholder controller (`BCM_4350C2`, PCIe, firmware v0, Off).

## Choice: MtkWirelessBT 1.1.1 (Dino-VN/MtkWireless) + BlueToolFixup 2.7.2

- It is in the MtkWireless 1.1.1 package we already pin for Wi-Fi (`MtkWirelessWifi` works on this machine): same
  author, same release, so Wi-Fi and Bluetooth of the one chip come from one project. GPL.
- Its `BLUETOOTH.md` names macOS 26.7.1 **25G241**, x86_64 (this machine's build) as tested. Firmware load,
  pairing, A2DP, BLE scanning: working per the author; BLE advertising / AirDrop: partial *(all unverified here)*.
- It matches `0489:e0e2` explicitly (personality `MtkWirelessBT_MT7922A_0489_e0e2`), plus a generic MediaTek
  (`0e8d`, any product) USB personality. It is USB-only: nothing touches PCI or the GPU.
- Needs Lilu (present), BlueToolFixup (acidanthera BrcmPatchRAM 2.7.2, pinned) and two NVRAM variables in the Apple
  GUID, in both `NVRAM > Add` and `NVRAM > Delete`: `bluetoothInternalControllerInfo` = 14 zero bytes,
  `bluetoothExternalDongleFailed` = `00` (without them bluetoothd assumes Apple's Broadcom module and aborts).
  `efi/build.py` gained `nvram_add` per stage for this (the build without it is byte-identical to before).
- The Lilu half is invasive: it patches pages of `bluetoothd` (as BlueToolFixup does), hooks exec of `bluetoothd`,
  its code-signing page validation and the APFS `vnop_verify` entry of its vnode, so that patched pages don't break
  the sealed system volume's hash checks. `-mtkbtoff` turns all of that off (the firmware loader still runs).
  Other boot-args: `-mtkbtdbg` (debug log), `-mtkbtnolefix`, `-mtkbtnomsbc`, `-mtkbtnoshutdownoff`.
- Alternatives not taken: AirportMtkWlan's Bluetooth companion (2.2.x panicked and was pulled; not in the 2.1.6
  release), SimpleMtkBtFw (firmware only, older, would need the USB ID added by hand). Only one loader may run.
- Project state (2026-11-22): 3 open issues, none about Bluetooth or panics; v1.1.1 is the newest release.

## Checking after the boot

```sh
system_profiler SPBluetoothDataType            # Chipset: THIRD_PARTY_DONGLE, State: On
ioreg -l -w0 -r -n IOResources | grep -A12 '"MtkWirelessBT"'   # RustSelfTestFailed = 0
ioreg -l -w0 | grep -A30 MtkBtLog              # the loader's log on the USB device
```

## Notes

- With `DisableVariableWrite` macOS can't write NVRAM; OpenCore sets both variables before macOS starts, as it does
  `boot-args`. They stay in the firmware NVRAM after a rollback to an EFI without them (inert without the kexts).
- Sleep stays off (Vermilion), so the author's open sleep/wake item doesn't apply.

## Boots (XTX machine)

- **20:19:** MtkWirelessBT and BlueToolFixup loaded; `system_profiler`: `THIRD_PARTY_DONGLE`, State On, firmware
  `v9489 c6166`. The screen stayed black: at login macOS requested 1440p60 HDR first and the switch failed
  (`link training failed`), the display engine fell back to 144 Hz and refused further switches until reboot. The
  GPU daemon still reported `SUCCESS`. Unrelated to Bluetooth as far as the logs show (a DP link problem).
- **20:27** (restart): 1440p240 HDR `DONE`, daemon `SUCCESS`, Bluetooth On again, Wi-Fi and sensors fine.
- 2026-11-22 ~20:35: the owner paired AirPods; they connect and play sound.
