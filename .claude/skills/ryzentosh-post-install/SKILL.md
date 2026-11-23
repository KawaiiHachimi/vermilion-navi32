---
name: ryzentosh-post-install
description: Finish a macOS Tahoe 26.7.1 install on an AMD Ryzen + Radeon RX 7900 XT/XTX PC from inside macOS. It installs the GPU driver (Vermilion boot daemon, Metal plugin, kexts, VermilionTrust), then runs the checks and the validation. Use after /ryzentosh-pre-install, once macOS boots from OpenCore. DRAFT skeleton (thread 00); the steps are filled in by the series threads.
---

<!-- SPDX-License-Identifier: MIT -->

# ryzentosh post-install (macOS side) — draft

**Status: a skeleton.** Thread 00 wrote it on 2026-11-23. Each section names the series thread
([ROADMAP.md](../../../ROADMAP.md)) that fills it in, and it moves to `ownback/ryzentosh` in thread 10. Today the GPU
install is the manual expert path in [docs/INSTALL.md](../../../docs/INSTALL.md) and the runbooks. This skill only
points at it and never improvises it.

## Hard guardrails (read before doing anything)

1. **Hardware safety.** Never send an SMU, PSP or DAL message, or write a GPU register, that Linux amdgpu doesn't
   send on this ASIC. Never write the VBIOS. Keep the bring-up watchdog and the boot daemon's fallbacks (kill
   switch, pending mark, GOP revert). When coming from Linux, the user always powered off fully.
   ([CLAUDE.md](../../../CLAUDE.md) rule 1.)
2. **Never repartition, and never write the EFI partition without the user's explicit OK for that exact step.**
   Mount it read-only to look (`diskutil mount readOnly disk0s1`). Write to it only through `efi/build.py install`,
   which keeps `EFI/OC.prev`.
3. **One EFI change per boot,** with the rollback written down for the user first: `sudo diskutil mount disk0s1`,
   then in `/Volumes/<ESP>/EFI`: `mv OC OC.bad && mv OC.prev OC`; if macOS doesn't boot, the same from Linux.
4. **Never print serials:** no `config.plist` `PlatformInfo`, no `~/.config/vermilion/smbios-*.json`, no `nvram -p`
   output unfiltered, no `ioreg` serial or MAC lines, no EDID dumps with serials. Don't upload `build/efi/`.
5. **Tell the user, once, at the start:** everything this agent reads goes to the model provider.
6. **No macOS update** (not even 26.7.x) until thread 09's VermilionTrust layout check exists (ROADMAP rule 12). No
   Reset NVRAM in the OpenCore picker (it deletes `phxfb-squat`).
7. Scripts do the risky work; this skill only runs them.

## 1. Before signing in to the Apple Account — to be filled by thread 10

`en0` must be the built-in Ethernet (FairPlay and iServices take the identity from it):
`networksetup -listallhardwareports`. If `en0` is an iPhone / iPad USB interface, stop. The fix script is TODO
(thread 10; the manual fix with backups is in the old thread 05's finding,
`.agents/findings/2026-11-22-apple-services-security-inventory.md`). Unplug iPhones and iPads during the install and
the first boot.

## 2. Build the driver on this Mac — partly verified; packaging by thread 06

Verified on the reference machine ([runbooks](../../../.agents/runbooks.md), "Building on the Mac itself"):
`scripts/build/kext.sh`, `accel.sh`, `plugin.sh`, `translator.sh`, `tools.sh`, `metaltest.sh`. They need
MacKernelSDK and Vulkan-Headers. RADV needs a Mesa checkout at `f5cb8ee0` plus `patches/mesa`. TODO (thread 06):
GitHub Releases, so a user doesn't build at all.

## 3. Install the GPU stack — to be filled by thread 06

Today it is manual, [docs/INSTALL.md](../../../docs/INSTALL.md):
- VermilionAccel into the AuxKC, with the user's **Allow** click in Privacy & Security;
- the Metal plugin into `/Library/GPUBundles`, plus `air_runtime_install.sh`;
- the boot daemon in `~/gpu` with its LaunchDaemon (`tools/phxctl/daemon_install.sh`) and the firmware
  (`tools/firmware/fetch_firmware.sh`);
- the daemon flags.

Don't run these steps from this skill until thread 06 scripts them. Point the user to the guide instead.

## 4. VermilionTrust and the EFI — verified on the reference machine

After any plugin, translator or RADV install ([runbooks](../../../.agents/runbooks.md), "Deploying a plugin,
translator or RADV build"):

```sh
PHXTRUST_KEEP=1 sh scripts/build/trust.sh      # cdhashes of the installed code -> build/VermilionTrust.kext
python3 efi/build.py build                     # the profile's default stage (vermilion-trust)
sudo diskutil mount disk0s1                    # then diff -rq the live EFI/OC against build/efi/<machine>-<stage>/EFI/OC
sudo python3 efi/build.py install --esp "/Volumes/<ESP>"    # guardrail 2 and 3: ask first; keeps EFI/OC.prev
```

## 5. Checks after each boot — verified tools

- `bash tools/validation/bootcheck.sh` (read-only): the shutdown cause, new crash / stall / panic reports, the
  daemon's result, sleep settings, the display mode. Add `sudo` for SMART.
- `sysctl debug.phxtrust_stat`: `mode=enforce`, `denied_unlisted=0`.
- `python3 tools/usbmap/usbwatch.py --ports`: the mapped USB ports.
- `python3 tools/sensors/cpusensors.py`: CPU temperature and package power (needs SMCProcessorAMD and `smcread`).
- The display engines (thread 01; VermilionBoot b151+; the `DARK` path has not happened on the XTX machine:
  unverified): bootcheck prints the replay
  engine's last result and `failed=` latch. The daemon itself reports `DARK` instead of `SUCCESS` when an engine
  latched, saves `/var/log/phxdark-<time>.txt`, and restarts once (`/var/db/phxboot.darkrestart` records it). After a
  `DARK` line, show the user that file's first lines and the last `last_fail_train:` line of
  `sysctl debug.phxp0odm_stat`, and ask before any further restart. Never write `debug.phxp0odm` (root) to "fix" it:
  every value runs register writes.

## 5b. Performance and power checks — thread 02, verified on the reference machine

- RADV picked the right profile: `/usr/bin/log show --last 30m --predicate 'eventMessage CONTAINS "radv-darwin: native"'`
  ends in `profile navi31` on an XTX (PCI revision `0xc8`) and `profile navi31_xt` on an XT (`0xcc`); `system_profiler
  SPDisplaysDataType` names the card (needs patch 0018 / VermilionTrust 1.0.4 or later).
- Benchmarks, light (seconds each; ask the user first, they load the GPU): `scripts/build/metalbench.sh` and
  `scripts/build/vkbench.sh` build them; `vkbench compute 3`, `vkbench submit 3 200 512 512`, `vkbench bandwidth 3`.
  Compare with PROJECT_KNOWLEDGE's performance record (Linux and macOS columns). Expected on macOS today: shader speed
  as Linux, buffer bandwidth ~45 GB/s (every buffer is system memory; Linux 823).
- Power: `SMU_CLOCKS=1 sudo -E python3 ~/gpu/smumetrics.py 4 1` (socket / board W, UCLK, FCLK, DCEFCLK). The reference
  machine idles at ~51 W socket at 1440p240 HDR with UCLK pinned at 456 (the daemon's `phxboot-uclk` flag); Linux 35 W.
- CPU: `python3 tools/sensors/cpusensors.py`; ~40 W idle package and a 5.1-5.2 GHz single-thread ceiling are what
  Linux shows too on this board with the BIOS at Auto. No CPU power-management kext.

## 6. System settings — to be filled by threads 05 and 09

Today, on the reference machine:
- `sudo pmset -a sleep 0 displaysleep 0 hibernatemode 0 disablesleep 1`, because there is no GPU resume (thread 05);
- `ConfigDataInstall = 1` and `xprotect update`, so XProtect / Gatekeeper data updates as on a real Mac;
- macOS updates stay off.

TODO: a script with backups and an undo for each.

## 7. Validation — to be filled by thread 11

The owner's checklist: [.agents/OWNER-TESTS.md](../../../.agents/OWNER-TESTS.md). Stress, soak and regression
runs against PROJECT_KNOWLEDGE's baselines: TODO (thread 11).
