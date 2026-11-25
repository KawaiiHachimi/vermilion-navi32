# Navi 32 (RX 7800 XT) install runbook

From a clean macOS install to the accelerated desktop, in the order that works. Every step names what it needs
and what proves it. `docs/NAVI32.md` holds the why; `docs/NAVI32-STATUS.md` holds what is and is not working.

Throughout: `G=~/gpu` is the desktop user's tool directory, `ASIC=navi32` for every build.

## 0. Prerequisites

| Need | Why | Check |
|---|---|---|
| OpenCore with `VermilionBoot.kext` in `EFI/OC/Kexts` | PhxBringup probes before IONDRV and squats the framebuffer | `ioreg -l \| grep phx-build` |
| boot-args `phxdebug=1 amfi=0x80 amfi_get_out_of_my_way=1 phxclaimforce=1` | debug selectors, and loading an unsigned kext from `/Library/Extensions` without the approval dialog | `nvram boot-args` |
| Xcode command line tools | building kext, tools, translator | `xcode-select -p` |
| Vulkan-Headers | the Metal plugin's build | `ls $VULKAN_HEADERS/vulkan/vulkan_core.h` |

## 1. Build and install the boot kext

```sh
cd <repo>
export MKSDK=<MacKernelSDK>
ASIC=navi32 sh scripts/build/kext.sh
```

Copy `build/VermilionBoot.kext` to `EFI/OC/Kexts/`, `chown -R root:wheel`, `chmod 755`, `sync`, reboot. Prove it:
`ioreg -l | grep phx-build` reports the build number, and after a squat boot `ioreg -rc PhxFBSquatter` is non-empty
until the desktop is up.

## 2. Build and install the accelerator (AuxKC, not the file!)

```sh
ASIC=navi32 sh scripts/build/accel.sh
sudo cp -R build/VermilionAccel.kext /Library/Extensions/
sudo kmutil install --volume-root / --update-all     # rebuilds the AuxKC
sudo reboot
```

**The file in `/Library/Extensions` is not what loads** - the Auxiliary Kernel Collection is. Check both UUIDs:

```sh
dwarfdump --uuid /Library/Extensions/VermilionAccel.kext/Contents/MacOS/VermilionAccel
kmutil showloaded | grep org.vermilion.accel        # must show that UUID
sysctl debug.phxgl                                   # exists only after the new build is loaded
```

`kmutil install` reports a failure for the boot and system collections (the sealed volume is read-only); the
Auxiliary collection is still rebuilt, and its timestamp changes.

## 3. Tools and firmware

```sh
sudo cp build/mesboot /usr/local/sbin/mesboot
sudo cp build/phxctl  /usr/local/sbin/phxctl
sudo cp build/libvulkan_radeon.dylib /Library/GPUBundles/nvmtl/libvulkan_radeon.dylib
cp -R tools/phxctl ~/gpu/phxctl && cp build/mesboot build/phxctl ~/gpu/phxctl/
# VCN firmware for this die: amdgpu/vcn_4_0_0.bin from linux-firmware
mkdir -p ~/gpu/fw/navi32 && cp vcn_4_0_0.bin ~/gpu/fw/navi32/
```

## 4. The Metal stack

```sh
VULKAN_HEADERS=<headers> sh scripts/build/plugin.sh         # NVMTLDriver
rustup target add x86_64-apple-darwin                       # on an arm64 build host
sh scripts/build/translator.sh                              # libnvmtl_translate.dylib
sudo mkdir -p /Library/GPUBundles/NVMTLDriver.bundle/Contents/MacOS
sudo cp build/NVMTLDriver build/libnvmtl_translate.dylib /Library/GPUBundles/NVMTLDriver.bundle/Contents/MacOS/
```

The bundle needs an `Info.plist` (`CFBundlePackageType BNDL`, executable `NVMTLDriver`, `NSPrincipalClass
NVMTLDevice`); a minimal hand-written one works. The AIR reader goes in `air-runtime/` **self-contained and
ad-hoc signed** (see step 5).

## 5. The AIR reader (WindowServer needs it)

```sh
sudo cp /opt/local/libexec/llvm-21/lib/libLLVM.dylib /tmp/airsrc/   # MacPorts llvm-21
sudo cp /opt/local/lib/libzstd.1.dylib /tmp/airsrc/
sudo sh tools/phxctl/air_runtime_install.sh /tmp/airsrc
```

Then, **for every dylib in `air-runtime/`**: copy its `/opt/local` dependencies in, rewrite them to
`@loader_path/<name>` with `install_name_tool`, and `codesign -fs -` each one. WindowServer runs with library
validation, so an unsigned file - or one that pulls an unsigned `/opt/local` library - is rejected and no shader
translates. Ten files is normal (`libLLVM`, `libffi`, `libedit`, `libz`, `libzstd`, `libxml2`, `libicu*`, `libiconv`,
`libncurses`).

`spirv-val` must be findable: `sudo ln -sf /opt/local/bin/spirv-val /usr/local/bin/spirv-val`.

## 6. Permissions and allow list

```sh
sudo mkdir -p /Library/GPUBundles/nvmtl/aircache /Library/GPUBundles/nvmtl/spvcache
sudo chown _windowserver:wheel /Library/GPUBundles/nvmtl/aircache /Library/GPUBundles/nvmtl/spvcache
sudo chmod 0755 /Library/GPUBundles/nvmtl/aircache /Library/GPUBundles/nvmtl/spvcache
printf '*\n' | sudo tee /Library/GPUBundles/nvmtl-allow.txt >/dev/null
sudo chmod 644 /Library/GPUBundles/nvmtl-allow.txt
```

The allow list is checked **per process**: a process not listed gets "not allow-listed: no device". A one-line
file containing `*` is the simple form.

## 7. The boot daemon

```sh
cp -p tools/phxctl/navi31_up.sh ~/gpu/phxctl/navi31_up.sh.xtx        # keep the fork's script
cp -p tools/phxctl/navi32_daemon_up.sh ~/gpu/phxctl/navi31_up.sh      # what the daemon calls
cp -p tools/phxctl/navi32_up.sh tools/phxctl/vcn_up.sh ~/gpu/phxctl/
cp -p tools/phxctl/phxboot-navi31.sh ~/gpu/phxboot-navi31.sh
# Idle-power helpers run by default from ~/gpu (sdma_idle -> gfx_cg -> idle_dpm)
cp -p tools/phxctl/sdma_idle.sh tools/phxctl/gfx_cg.sh tools/phxctl/idle_dpm.sh \
      tools/phxctl/uclk.sh tools/phxctl/dal.sh tools/phxctl/dalwm.sh \
      tools/phxctl/smumetrics.py ~/gpu/
mkdir -p ~/gpu/vcnup && cp -R tools/phxctl/vcn_up.sh tools/phxctl/vcn ~/gpu/vcnup/
# the LaunchDaemon
printf '%s\n' '<plist with Label local.phxboot, /bin/bash ~/gpu/phxboot-navi31.sh, PHX_USER=<user>>' \
  | sudo tee /Library/LaunchDaemons/local.phxboot.plist
sudo plutil -lint /Library/LaunchDaemons/local.phxboot.plist
```

Flags (create the ones you want in `~/gpu`):

| Flag | Effect |
|---|---|
| `phxboot-auto` | re-arm `phxfb-squat` after a successful boot (needed for the desktop to survive reboots) |
| `phxboot-desktop` | force a desktop arm even without a squat |
| `phxboot-vcn` | run `vcn_up.sh` after the daemon's `done` line |
| `phxboot-gl` | `debug.phxgl=1` after `done` - OpenGL over Metal |
| `phxboot-autologin` | user name the daemon auto-logs in before its WindowServer restart |

## 8. Automatic console login (this machine has no usable keyboard console)

```sh
# /etc/kcpassword: the login password XORed with the key below, padded to 12 bytes, mode 600 root:wheel
python3 -c "import binascii;open('/tmp/kc','wb').write(binascii.unhexlify('<hex>'))"
sudo cp /tmp/kc /etc/kcpassword && sudo chmod 600 /etc/kcpassword && sudo chown 0:0 /etc/kcpassword
cp /etc/kcpassword ~/gpu/kcpassword.bin    # phxlogin.sh restores it
# phxlogin.sh + its LaunchDaemon
sudo cp tools/phxctl/phxlogin.sh /usr/local/sbin/phxlogin.sh
```

**Do not use `sysadminctl -autologin set`**: it failed here with `SACSetAutoLoginPassword error:22` and left
`/etc/kcpassword` unreadable, which silently stops auto-login while the daemon still reports SUCCESS.

## 9. First boot with the desktop

```sh
pgrep -f phxboot-navi31        # must be empty before touching flags
rm -f ~/gpu/phxboot-failed     # a previous failure blocks arming
touch ~/gpu/phxboot-desktop ~/gpu/phxboot-auto ~/gpu/phxboot-vcn ~/gpu/phxboot-gl
printf '%s\n' cool > ~/gpu/phxboot-autologin
sudo nvram phxfb-squat=1 && sudo reboot
```

Proof, in `/var/log/phxboot.log`:

```
squat boot (PhxFBSquatter holds IOFramebuffer, no IONDRV console): 1
desktop-arm: 1
PhxAMDFB on panel after 1s / Metal device ready after 1s / AGDC up after 1s
restarting WindowServer once (Metal ready, AGDC up)
SUCCESS at <time>: WindowServer pid <n>, frames=<n> -- accelerated desktop survived
phxfb-squat re-armed for the next boot
vcn: up, ring adopted, MMHUB VM on (mmvm 1 bad 0x0)
gl: OpenGL over Metal on
```

## 10. Verifying the stack by hand

```sh
~/gpu/metaltest            # 1 device, COMPUTE RESULT 4096/4096 correct
~/gpu/metalbench compute 3 64 128 128
VKBENCH_DRIVER=/Library/GPUBundles/nvmtl/libvulkan_radeon.dylib ~/gpu/vkbench compute 3 64 128 128
~/gpu/vkwsi /Library/GPUBundles/nvmtl/libvulkan_radeon.dylib   # WSI OK
sudo launchctl asuser $(id -u cool) sudo -u cool ~/gpu/apiprobe  # GL 4.1 / OpenCL on the GPU (needs a session)
```

OpenGL and OpenCL cannot be checked over SSH: `CGLChoosePixelFormat` answers "invalid CoreGraphics connection"
outside the console user's session, and `launchctl asuser` alone is not enough.

> Updating the accel: copy the kext into `/Library/Extensions`, run the `kmutil install` above, then **reboot** - on this
> machine the AuxKC is rebuilt during that reboot (its timestamp moves to the shutdown time) and the new UUID loads;
> `kmutil install` alone leaves the old AuxKC in place.
