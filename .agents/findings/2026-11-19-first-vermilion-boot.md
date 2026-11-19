# 2026-11-19: first Vermilion boot on the XTX: the bring-up works, the accelerator kext waits for approval

Installed from Recovery (`recovery_install.sh` with `VERMILION_DATA="/Volumes/Macintosh HD - Data"`: its own
search found 3 candidates, fix pending), then one boot of Macintosh HD with the `vermilion` EFI stage, flags `sdma`,
`cg`, `uclk=456`, no desktop arm, nobody logged in. `/var/log/phxboot.log` (read from Linux with apfs-fuse):

```
squat boot ...: 0 (decided by IOFramebuffer after 0s)
desktop-arm: 0
Extension with identifiers org.vermilion.accel not approved to load. Please approve using System Settings.
== bring-up + claim ==
gfx hqd after map: active=1 base=0x800a1700 cntl=0x20a0088a dbctl=0x40000458 rptr=0
gfx: scratch1=0xdeadbeef (want deadbeef) testbuf=0x6f7a6f7a (want 6f7a6f7a) rptr=0 rptr_wb=968 grbm=0x0000302c cp_stat=0x00000000
adopt gfx queue -> kr 0x0
navi31 up: compute + gfx queues ready
sdma: sdma idle: RLC_RLCS_SDMA_INT_STAT 0x00000300 (0x00000003 = both idle)
cg: ...
uclk: smu msg 0x1b arg 0x000201c8 -> kr 0x00000000 resp 0x01 (OK) out 0x000001c8
claim -> kr 0x0
NMXAccel did not start (apps-only this boot)
```

**The whole bring-up runs on the XTX (rev 0xC8) unchanged**: PSP/SMU/RLC, MES, KIQ, the gfx queue passes its
scratch and test-buffer checks, both SDMA engines idle, clock gating on, the SMU accepts the UCLK pin (456 MHz),
claim OK. Nothing in the bring-up needed an XTX change.

## Why VermilionAccel did not load

From the unified log (kernelmanagerd, kernelmanager_helper, syspolicyd):

- `kmutil load -p` asks for a new AuxKC with the kext: `auxkc build requires reboot, calling out to syspolicyd`.
- syspolicyd: `Kernel Extension ALLOWED: Kext ((null), org.vermilion.accel)` (SIP `0x0A03` includes
  `CSR_ALLOW_UNAPPROVED_KEXTS`; `KextPolicy` has `allowed=1`), `Triggered kext upgrade`, then
  **`Holding work until alerts available`**: the AuxKC rebuild waits for a user session that can show the
  "restart required" alert. Nobody was logged in, so kmutil returned error 27 ("not approved").
- The kext can't go through OpenCore instead: it links against `IOAcceleratorFamily2`,
  `AppleGraphicsDeviceControl` and `IOSurface`, which are in the System KC, not the Boot KC OpenCore injects into.

Next: one boot with a login, so syspolicyd can finish the AuxKC rebuild (then shut down). Check from Linux that
`/Library/KernelCollections/AuxiliaryKernelExtensions.kc` changed and contains `org.vermilion.accel` before the
next boot.

## Second and third boot (2026-11-19): VermilionAccel loads, NMXAccel up, Metal for apps

- Boot with a login (18:47 Pacific): syspolicyd rebuilt the AuxKC (`/Library/KernelCollections/AuxiliaryKernelExtensions.kc`
  03:48 local, 1.29 MB, contains `org.vermilion.accel`); that boot still logged "not approved" (the new AuxKC applies
  from the next boot). The bring-up ran again unchanged.
- Next boot (18:49, 49 s after `SHUTDOWN_TIME`, probably a restart, so the GPU may have been warm; unverified):
  bring-up OK, `NMXAccel up`, `no desktop arm: GOP + Metal for apps`, `done 18:50:08`, 5 s after the daemon started.
  No crash reports, no `phxhang-*` dump. The owner reports the desktop is faster (apps get Metal; WindowServer still
  composites on the CPU). A `shutdown_stall` report at 19:01 lists only slow userspace daemons (softwareupdated, akd).
- The hook `esp_flags.sh` was hardened (retries for listing and mounting while diskarbitrationd starts; a non-path
  `Mount Point` counts as unmounted), 7/7 in its test. Run as root on macbox against its real EFI partition: an
  unmounted partition has no `Mount Point` line, gets mounted, the flags are taken and deleted, the partition is
  unmounted (0.9 s). The hook copies every `phxboot-*` file, including `phxboot-pre.sh` itself, so the new hook was
  staged that way, together with `phxboot-desktop` for the first desktop-arm boot.

Security note: the EFI partition is a write path into `~/gpu`, which root runs at boot. That is no new exposure
on this machine (writing the EFI partition already needs root, and `~/gpu` is user-writable by design,
INSTALL.md 6), but it belongs in the docs before the hook is offered upstream.
