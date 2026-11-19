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

## Reading the macOS disk from Linux (root, read-only)

Logs from macOS boots that never reached a desktop (2026-11-19, works). `apfs-fuse` is read-only. Build it once:

```sh
git clone --recursive https://github.com/sgan81/apfs-fuse.git && mkdir apfs-fuse/build && cd apfs-fuse/build
cmake -DUSE_FUSE3=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_FLAGS="-include cstdint" .. && make -j8
```

Mount one volume at a time. On p3: `-v 0` Data, `1` Preboot, `2` Recovery, `3` System, `4` Update. Files are under
`<mnt>/root`:

```sh
sudo ./apfs-fuse -o ro,allow_other -v 0 /dev/nvme0n1p3 /mnt/mac   # Data volume
sudo tail -100 /mnt/mac/root/private/var/log/install.log           # installer phases, "Triggering reboot"
sudo ls /mnt/mac/root/Library/Logs/DiagnosticReports               # panic reports
sudo umount /mnt/mac
```

The OpenCore log of every boot (`/boot/opencore-*.txt`) shows the boot.efi path and kernel flags
(`grep -a 'LIMG:FP\|MBA:OUT\|EXITBS'`). Together these tell you which installer phase a boot was in.

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

## Build host (macOS VM via macbox) (2026-11-19, works)

The macbox VM runs macOS 26.7.3 (x86_64) with Xcode. Set up once (everything in `~/src` on the guest):

```sh
macbox up && ssh macbox
git clone -b ownback/explore-7900xtx-efi-passthrough https://github.com/ownback/vermilion.git ~/src/vermilion
cd ~/src/vermilion/deps && git clone --depth 1 https://github.com/acidanthera/MacKernelSDK.git \
  && git clone --depth 1 https://github.com/KhronosGroup/Vulkan-Headers.git
curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-host x86_64-apple-darwin
curl -LsSf https://astral.sh/uv/install.sh | sh                     # Mesa's meson needs Python >= 3.10; the system has 3.9
~/.local/bin/uv python install 3.12 && ~/.local/bin/uv venv -p 3.12 ~/src/venv
~/.local/bin/uv pip install -p ~/src/venv/bin/python meson ninja mako pyyaml markupsafe packaging cmake
# glslangValidator: the macOS release binary of KhronosGroup/glslang 16.6.0, unpacked to ~/src/tools/glslang
# Mesa at f5cb8ee0 (a shallow fetch of the one commit), then the patches with an ABSOLUTE path:
mkdir ~/src/mesa && cd ~/src/mesa && git init -q && git remote add origin https://gitlab.freedesktop.org/mesa/mesa.git \
  && git fetch --depth 1 origin f5cb8ee032adabef599ae892ec4e42d796b84da9 && git checkout FETCH_HEAD
git -C ~/src/mesa am ~/src/vermilion/patches/mesa/*.patch          # `radv.sh apply` passes a relative path, which fails
                                                                   # under `git -C`
```

Build (about 10 minutes, most of it RADV):

```sh
cd ~/src/vermilion && export PATH=$HOME/src/tools/glslang/bin:$HOME/.cargo/bin:$HOME/src/venv/bin:$PATH
export MKSDK=$PWD/deps/MacKernelSDK MESA_DIR=$HOME/src/mesa
tools/firmware/fetch_firmware.sh
for s in kext accel tools metaltest plugin translator radv; do sh scripts/build/$s.sh; done
```

`spirv-val` (the translator runs it; Vermilion doesn't ship it): SPIRV-Tools v2025.4, `python3 utils/git-sync-deps`,
then `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64 -DSPIRV_SKIP_TESTS=ON
-DBUILD_SHARED_LIBS=OFF` and `ninja spirv-val`. Copy it to `build/spirv-val` and sign it with `codesign -fs -`.

Copy `build/` to the Linux checkout with `rsync -a macbox:src/vermilion/build/ build/`. Stop the VM afterwards
(`macbox down`): it uses 8 CPUs and 15 GB. The VM has no AMD GPU, so it only builds; all driver testing happens on
bare metal. The mock suites run there too (`bash tests/mock/run.sh`, `b15test.sh`, `espflags_test.sh`,
`recovery_install_test.sh`); the guest has passwordless sudo, so `recovery_install.sh` can also be run as root
against a fake Data volume.

## Installing Vermilion without the desktop (XTX machine, 2026-11-19)

From Linux (root):

```sh
sh tools/phxctl/esp_stage.sh /boot/vermilion ~/.cache/vermilion/fw/navi31   # payload + install.sh, MD5SUMS
efi/build.py build --stage vermilion && efi/build.py install --esp /boot --stage vermilion   # keeps EFI/OC.prev
```

Then power off, F8 > OpenCore > Reset NVRAM > OpenCore > the Recovery entry > Utilities > Terminal:

```sh
diskutil mount disk0s1; sh /Volumes/EFI/vermilion/install.sh
```

(If `disk0s1` is not the EFI partition, `diskutil list` shows which one is.) Then restart into Macintosh HD.

Flags for the next boot, from Linux, without macOS: `touch /boot/vermilion/flags/phxboot-desktop` (one desktop-arm
attempt), `touch /boot/vermilion/flags/phxboot-off` (kill switch), `touch /boot/vermilion/flags/phxboot-off.rm`
(remove it). The daemon's pre-hook takes them at the next boot and logs one `esp-flags:` line each in
`/var/log/phxboot.log`. Read that log from Linux with `apfs-fuse` (above). Roll the EFI back with
`mv /boot/EFI/OC /boot/EFI/OC.vermilion && mv /boot/EFI/OC.prev /boot/EFI/OC`.

## OpenCore EFI install *(unverified)*

The EFI partition is shared with Limine (`/boot` on Linux). Steps:

1. Copy the EFI to `/boot/EFI/OC`. Never touch `EFI/BOOT/BOOTX64.EFI` (Limine's fallback) or `EFI/limine`.
2. Add a firmware boot entry, without changing the boot order:
   `sudo efibootmgr --create --disk /dev/nvme0n1 --part 1 --label "OpenCore" --loader '\EFI\OC\OpenCore.efi'`.
3. Pick OpenCore from the firmware boot menu (F8 on ASUS) until it is proven; only then consider a Limine entry or
   reordering.
