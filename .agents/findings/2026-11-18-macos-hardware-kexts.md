# 2026-11-18: kexts for the XTX machine's other hardware

Method: web and GitHub search (`gh api`), project READMEs. Nothing was tested yet. Release dates and versions are as of
2026-11-18.

| Hardware | ID | Candidate | Notes |
|---|---|---|---|
| Wi-Fi MediaTek MT7922 / RZ616 | `14c3:0616` | **[Arxusion/AirportMtkWlan](https://github.com/Arxusion/AirportMtkWlan)**, v2.1.6 (2026-11-14), GPL-3.0 | Based on OpenBSD `mwx(4)`. Native AirPort/Skywalk on Tahoe and Sequoia 15.2+, with the Wi-Fi menu, WPA2/3, 802.11ac/ax and S3. Lists `14c3:0616` explicitly. Needs `DisableIoMapper`, and no SSDT that hides the card |
| | | [qiqco/MT7922-macOS-Driver](https://github.com/qiqco/MT7922-macOS-Driver) 1.3 | Wi-Fi + BT; its macOS 26 line is the active one |
| | | [GeekAudio/MtkWireless](https://github.com/GeekAudio/MtkWireless) | Native menu, Tahoe only, no releases |
| | | [GeekAudio/mediatek-sunrise-hackintosh](https://github.com/GeekAudio/mediatek-sunrise-hackintosh) | Apple's own MediaTek driver route on macOS 26; says the MT7922 Wi-Fi firmware is missing in 26.7.1 |
| | | SimpleMtkWlan forks | Appear as Ethernet plus a menu-bar client; for older macOS |
| Bluetooth MT7922 (USB) | `0e8d:0616` | AirportMtkWlan's BT companion (`AirportMtkBtPatcher` + `AirportMtkBtFw`), or [SimpleMtkBtFw](https://github.com/iussch/SimpleMtkBtFw) | macOS 15+ has a built-in MediaTek BT stack behind a bluetoothd whitelist, which these patch. A user had to add `0e8d:0616` to SimpleMtkBtFw by hand ([nullmoth issue #336](https://github.com/nullmoth/nvidia-macos-driver/issues/336)). Use one loader only. The patch matches bluetoothd byte patterns, so a macOS update can break it |
| Ethernet Intel I225-V | `8086:15f3` | Apple's built-in driver | Usually works. Some setups needed a boot-arg or SSDT ([AMD-OSX thread](https://forum.amd-osx.com/posts/42792)). I found no report confirming Tahoe |
| CPU Ryzen 9 7900X3D (Zen 4) | | [AMD_Vanilla](https://github.com/AMD-OSX/AMD_Vanilla) patches with core count `0x0C`, plus the Zen 4 IOPCIFamily patch | Examples: [luccaroli/Tahoe-on-Ryzen](https://github.com/luccaroli/Tahoe-on-Ryzen) (OC 1.0.8), [luchina-gabriel/BASE-EFI](https://github.com/luchina-gabriel/BASE-EFI-AMD-RYZEN-THREADRIPPER-PUBLIC) |
| Board ASUS B650E-I | | none found | The closest is [ryanilano/hackintosh-asus-b650ef-amd](https://github.com/ryanilano/hackintosh-asus-b650ef-amd) (B650E-F), which needed a DSDT fix for ASUS firmware |
| USB audio | `0b05:1a5c` | none needed | standard USB audio |
| OpenCore | | [1.0.8](https://github.com/acidanthera/OpenCorePkg) (2026-09-27) | |

Not found: any other macOS driver for Navi 31. Dortania's GPU guide still lists Navi 3x as unsupported. Vermilion is the
only route.

## Notes for the EFI

- AirportMtkWlan's installer edits `config.plist` itself. Prefer the manual install, so the config stays reviewable.
- Vermilion needs `amfi=0x80` + AMFIPass and SIP `0x0A03`. Check that the Wi-Fi/BT kexts don't conflict with that
  (they shouldn't, because OpenCore injects kexts without signature checks *(unverified for these kexts)*).
