# Reference machines

Vermilion is tuned to real machines: register plans, watermarks and display timings were measured on them. A value in
the code is only known to be right for the machine it was measured on. When you report a result, say which machine it
was on.

## Upstream reference (leolivm)

From upstream's [README](../README.md) and [INSTALL.md](INSTALL.md):

| | |
|---|---|
| GPU | AMD Radeon RX 7900 XT, PCI `1002:744c` rev `0xCC`, 84 CU, 320-bit, 20 GB |
| Monitors | AOC 24G2W1G4 (DisplayPort, 1080p 144 Hz) and Acer KG241Q S (HDMI, 1080p 165 Hz) |
| macOS | Tahoe 26.7.1, SMBIOS MacPro7,1 |
| Status | Accelerated desktop on both monitors at 144 / 165 Hz; no sleep, no GPU audio |

## This fork's target: the XTX machine

The machine [ownback/vermilion](https://github.com/ownback/vermilion) is being made to work on. Measured from Linux
(amdgpu, libdrm, sysfs) on 2026-11-18.

| Part | Details | macOS status |
|---|---|---|
| Board | ASUS ROG STRIX B650E-I GAMING WIFI (AM5, Mini-ITX), BIOS 4005 | |
| CPU | AMD Ryzen 9 7900X3D, 12 cores / 24 threads (Zen 4) | AMD_Vanilla patches, core count `0x0C` |
| RAM | 48 GB DDR5 | |
| dGPU | XFX Speedster MERC 310 RX 7900 XTX, PCI `1002:744c` rev **`0xC8`**, subsystem `1eae:7901`, VBIOS `113-31XFSHBS1.L04` | Vermilion (being ported, see [XTX.md](XTX.md)) |
| dGPU details | 96 CU (none disabled), 6 SE x 2 SA, 24 RB, 24 GL2 channels, 384-bit GDDR6, 24 GiB (24560 MiB), 96 MB MALL, max shader clock 2482 MHz, `GB_ADDR_CONFIG 0x545`, PCIe 4.0 x16, Resizable BAR on (32 GB BAR under Linux) | |
| dGPU connectors | DP-1, DP-2, DP-3, HDMI-A-1 (Linux names). The VBIOS connector table equals the reference XT's: DP A/DIG0, DP E/DIG4, DP B/DIG1, HDMI D/DIG3 | |
| iGPU | Radeon 610M (Raphael, `1002:164e`) | no macOS driver; Linux uses it in the VFIO setup |
| Monitor | LG UltraGear+ 27", 2560x1440, 60/120/144 Hz DTDs, range 48-240 Hz, HDR metadata, 384-byte EDID (2 extensions). Connected to both the dGPU (DisplayPort, DP-3) and the iGPU | 60 Hz expected at first; high refresh needs new plans |
| Ethernet | Intel I225-V (`8086:15f3`) | Apple's built-in driver (to verify on Tahoe) |
| Wi-Fi | MediaTek MT7922 / AMD RZ616 (`14c3:0616`, subsystem `105b:e0cd`), behind the chipset switch (`GPP7.UP00.DP58`) | community kext (AirportMtkWlan 2.1.6, being tested) |
| Bluetooth | MediaTek MT7922 over USB (`0489:e0e2`, Foxconn ID) | community firmware loader (to verify) |
| Audio | ASUS USB audio (`0b05:1a5c`, Realtek USB codec) | standard USB audio, expected to work without a driver |
| Storage | One PCIe 5.0 NVMe (Phison E26). macOS gets a 100 GiB partition next to Linux | NVMe native |
| Firmware | UEFI, Secure Boot off, IOMMU on (the dGPU and its HDMI audio function each in their own IOMMU group), SVM on | |
| Other OS | Linux (Arch-based) with Limine on the shared EFI partition | |

### Rules this machine adds

- **The GPU is shared with Linux.** Before booting macOS after Linux, power off fully; never use restart
  ([ARCHITECTURE.md](ARCHITECTURE.md#what-happens-at-every-boot)).
- **Bare-metal macOS needs the dGPU as the boot display** (its GOP must light the monitor). The later VFIO setup makes
  the iGPU the host's display instead. That BIOS setting differs between the two phases.
- **One monitor, at 1440p,** is the target. Upstream's two-monitor and 1080p flags (`phxboot-m7*`, `phxboot-aoc*`,
  `phxboot-b15*`) don't apply.
