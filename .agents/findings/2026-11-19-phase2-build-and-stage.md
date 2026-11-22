# 2026-11-19: Phase 2 built on macbox and staged for a Recovery install

## Build

Every component built on the macbox VM (macOS 26.7.3, Xcode clang 21, x86_64) from this branch at `da8750c`, with
the steps now in [runbooks.md](../runbooks.md):

| Output | Notes |
|---|---|
| `VermilionBoot.kext` | build `PHX_KEXT_BUILD` unchanged (no kext source change); MacKernelSDK `3f75008` |
| `VermilionAccel.kext` | ad-hoc signed; link gate skipped (no export lists) |
| `NVMTLDriver` | Vulkan-Headers `b26454f` |
| `libnvmtl_translate.dylib` | Rust 1.100.0 |
| `libvulkan_radeon.dylib` | Mesa `f5cb8ee0` + the 17 patches; meson 1.12.1 under Python 3.12 |
| `phxctl`, `mesboot`, `metaltest` | |
| `spirv-val` | SPIRV-Tools v2025.4, static, ad-hoc signed |

Snags, all build-host setup, none in the code: the system Python is 3.9 and Mesa needs 3.10 or newer (uv's 3.12);
`radv.sh apply` passes a relative patch path that `git -C` resolves against the Mesa checkout (apply with an
absolute path); no Homebrew, so glslang comes from its release binary. The firmware matches `fw.lock`.

## Things Vermilion does not ship, and what the payload does about them

- **`NVMTLDriver.bundle` skeleton.** NullMoth's driver package (nullmoth/nvidia-macos-driver v3.0,
  `nullmoth-nvidia-3.0.tar.gz`, SHA-256 matches its `SHA256SUMS.txt`) has an `Info.plist` with seven plain keys
  (executable, identifier `com.nullmoth.NVMTLDriver`, name, type `BNDL`, versions 0.1). The accelerator finds the
  bundle by path (`MetalPluginName = ../../../Library/GPUBundles/NVMTLDriver`). The installer writes the same keys
  (unverified on hardware: INSTALL.md 5.2 says a hand-written `Info.plist` was never tested). The rest of NullMoth's
  bundle is NVIDIA-specific (SASS caches, NVENC/NVDEC dylibs) and is not used.
- **Bundle signature.** As on the reference machine, the executable is signed on its own, not the bundle as a whole;
  `codesign -v` on it says "code has no resources but signature indicates they must be present". The reference
  machine ran its own executable inside NullMoth's sealed bundle, which breaks the seal the same way, so this is
  expected to be accepted with `amfi=0x80` (unverified).
- **`spirv-val`:** built (above), installed to `/usr/local/bin`, one of the paths the translator searches.
- **`air-opt`:** not installed (optional fallback, INSTALL.md 5.2).

## The install path without a desktop

- `tools/phxctl/recovery_install.sh` (Recovery's Terminal, or `sudo` on the live system) installs INSTALL.md
  sections 4 to 7. It finds the Data volume and its user from the local directory, keeps every replaced file in
  `~/vermilion-rollback/<time>/`, sets the first-boot flags `sdma`, `cg`, `uclk=456` (docs/XTX.md), no desktop arm,
  and turns off Software Update's automatic download and macOS install. Recovery's BaseSystem.dmg (read with
  apfs-fuse) has every tool it uses (PlistBuddy, plutil, diskutil, stat, sed, cp, chown) and has
  `/System/Installation` but no local users, so the script can't mistake Recovery for the target.
- Checked on macbox: `tests/mock/recovery_install_test.sh` 6/6 (no root), and the real payload installed as root into
  a fake Data volume. Owners came out root:wheel (bundle, kext, tools, LaunchDaemon 644), `_windowserver` (shader
  caches) and the user (`~/gpu`), the signatures verified, and the firmware is byte-identical.
- The boot daemon's pre-start hook and `esp_flags.sh`: `tests/mock/espflags_test.sh` 4/4; the daemon suite
  `run.sh` 169/169 (3 new `prehook_*` scenarios); `b15test.sh` 159/159.
- Staged on the EFI partition `/boot/vermilion` (60 files, 44 MB, `md5sum -c` clean after the copy). The `vermilion`
  EFI stage is installed on `/boot/EFI/OC` (`EFI/OC.prev` = the `install` stage that installed macOS). Its config
  differs from `install` only in `Kernel > Add` (+ AMFIPass, VermilionBoot), the boot-args (+ `amfi=0x80
  ipc_control_port_options=0 phxdebug=1`) and `csr-active-config` `0x0A03`; `ocvalidate` is clean.

## Open questions for the first Vermilion boot (unverified, all logged by the daemon)

- Does the bring-up (PSP, SMU, RLC, MES, KIQ, queues) come up on the XTX as on the XT? Nothing in the scripts checks
  the PCI revision, and the firmware set is the same.
- Is `phxfb-boot`, which the daemon sets for a desktop-arm boot, readable in the same boot with
  `DisableVariableWrite` on? It matters only for the second (desktop-arm) boot.
- The GOP's DMCUB version (docs/XTX.md): only the DMUB-driven features need it, not the first boot.
