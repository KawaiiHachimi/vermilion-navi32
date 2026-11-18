# Runbooks

Step-by-step procedures for the XTX machine. Anything not done yet is marked *(unverified)*. Replace that marker
with the date once a procedure has worked.

## Booting between Linux and macOS

- **Linux -> macOS: shut down fully, then power on** and pick OpenCore. Never use restart: Vermilion's bring-up needs
  a cold GPU (the SMU firmware may be loaded only once per power cycle).
- **macOS -> Linux:** wait until `/var/log/phxboot.log` says `done`, then shut down or restart normally
  ([INSTALL.md 8.3](../docs/INSTALL.md)).
- **Bad boot:** boot-arg `-phxbringupoff` (or `-phxacceloff`) for one boot, or `touch ~/gpu/phxboot-off`, or disable
  `VermilionBoot.kext` in OpenCore.
- **One EFI change per boot.** Keep the previous EFI folder (`EFI/OC.prev`) until the new one has booted several
  times.

## BIOS settings for macOS (ASUS B650E-I, BIOS 4005) *(unverified: menu names from ASUS AM5 boards)*

Press F2 or Del at power-on, then F7 for Advanced Mode.

| Setting | Value | Where |
|---|---|---|
| CSM | Disabled | Boot > CSM (Compatibility Support Module) > Launch CSM |
| Secure Boot | off (OS Type: Other OS) | Boot > Secure Boot (already off) |
| Above 4G Decoding | Enabled | Advanced > PCI Subsystem Settings |
| Re-Size BAR Support | Enabled (OpenCore gives macOS 256 MiB BARs) | Advanced > PCI Subsystem Settings |
| Primary video device | the dGPU (PCIe), so its GOP lights the monitor | Advanced > NB Configuration (name varies) |
| Fast Boot | Disabled (reliable F8 menu and USB at boot) | Boot > Boot Configuration |
| SVM, IOMMU | leave on (Linux VFIO later; macOS ignores AMD-Vi) | Advanced > AMD CBS |

## Booting Linux

Start Linux the normal way (Limine is first in the firmware boot order), **not** from OpenCore's picker. Linux started
by OpenCore gets OpenCore's Mac identity and ACPI tables, and `efibootmgr` then shows OpenCore's private (empty) boot
list instead of the real one.

## First boot of OpenCore

1. Power off fully, then power on and press **F8**: the firmware boot menu. Pick **OpenCore** (Limine stays the
   default, so a normal boot still starts Linux).
2. In the OpenCore picker, pick the installer volume (**Install macOS Tahoe**).
3. Verbose boot (`-v`): if it stops, take a photo of the last lines. The OpenCore log is on the EFI partition
   (`/boot/opencore-*.txt` under Linux).
4. In the installer: Disk Utility > View > Show All Devices. Erase **only the `MACOS` volume** (78 GiB, exFAT) as
   APFS, name it `Macintosh HD`, then install onto it.

## Reading the XTX from Linux (no root)

[`tools/capture/gpuinfo.c`](../tools/capture/gpuinfo.c) (libdrm `AMDGPU_INFO_DEV_INFO`, `VRAM_GTT`, firmware
versions; 2026-11-18, works):

```sh
cc -Wall -I/usr/include/libdrm -o /tmp/gpuinfo tools/capture/gpuinfo.c -ldrm_amdgpu && /tmp/gpuinfo /dev/dri/renderD128
```

The dGPU is `renderD128` / `card1` (PCI `03:00.0`); the iGPU is `renderD129` / `card0` (`0e:00.0`). Check with
`readlink -f /sys/class/drm/renderD*/device`, because the numbering can change.

## Register captures from Linux (root) *(unverified)*

Read-only, while amdgpu drives the monitor in the mode being captured:

```sh
D=/sys/kernel/debug/dri/0000:03:00.0                          # dGPU (or dri/<minor>, check `ls`)
sudo cat $D/amdgpu_dm_dtn_log                                 # DCN state: watermarks, DLG/TTU, pipes
# one dword at a time: byte offset = dword index * 4
sudo dd if=$D/amdgpu_regs bs=4 skip=$((0x4FEA)) count=1 2>/dev/null | xxd
sudo dd if=/dev/drm_dp_aux2 bs=1 count=16 2>/dev/null | xxd   # DPCD of DP-3 (card1-DP-3 owns drm_dp_aux2)
```

Linux connectors on the XTX (sysfs, 2026-11-18): DP-1 = `drm_dp_aux0`, DP-2 = `drm_dp_aux1`, DP-3 (the monitor) =
`drm_dp_aux2`. Which VBIOS path / DIG each of these is (DP A/B/E) is not settled yet, because Linux's AUX bus
numbers differ from the VBIOS's.

Never write to `amdgpu_regs`. Captures go to a local directory outside git, unless they contain no serials and are
turned into plans.

## Build host (macOS VM via macbox) *(unverified)*

```sh
macbox up                 # starts the VM (Docker, QEMU); macbox status shows it
macbox ssh                # shell on the guest
```

The guest needs the Command Line Tools, MacKernelSDK (`deps/MacKernelSDK`), Vulkan-Headers, Rust with
`x86_64-apple-darwin`, and for RADV: meson, ninja, glslang, python3 with mako, pyyaml, markupsafe, packaging, plus a
Mesa checkout at `f5cb8ee0`. Then follow [BUILD.md](../docs/BUILD.md). Stop the VM afterwards (`macbox down`): it uses
8 CPUs and 15 GB.

The VM has no AMD GPU, so it builds only. All driver testing happens on bare metal.

## OpenCore EFI install *(unverified)*

The EFI partition is shared with Limine (`/boot` on Linux). Steps:

1. Copy the EFI to `/boot/EFI/OC`. Never touch `EFI/BOOT/BOOTX64.EFI` (Limine's fallback) or `EFI/limine`.
2. Add a firmware boot entry, without changing the boot order:
   `sudo efibootmgr --create --disk /dev/nvme0n1 --part 1 --label "OpenCore" --loader '\EFI\OC\OpenCore.efi'`.
3. Pick OpenCore from the firmware boot menu (F8 on ASUS) until it is proven; only then consider a Limine entry or
   reordering.
