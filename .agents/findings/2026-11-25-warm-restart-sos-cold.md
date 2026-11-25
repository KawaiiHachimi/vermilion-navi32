<!-- SPDX-License-Identifier: MIT -->

# A warm macOS restart leaves the PSP sOS stopped (2026-11-25)

Seen on the XTX machine (macOS 26.7.1 25G241, EFI change 33, VermilionBoot b160).

**Question.** PR #22 makes `bringup_navi31.sh` refuse a GPU whose PSP sOS is running (C2PMSG_81 != 0). If a normal
macOS restart left the sOS running, that refusal would cost Metal after every restart (and on this machine's squat
boots, a second reboot and a dark boot).

**Method.** A one-shot, read-only `vermilion/run-once.sh` on the ESP ran in the daemon's pre-start hook, before any
bring-up, and read `phxctl regs 0x16091 0x1607a 0x16063`. Then `sudo shutdown -r now` from a running accelerated
desktop (uptime ~12 h; the previous boot's bring-up had started the sOS).

**Result** (`/var/log/phxboot.log`, boot of 2026-11-25 06:31:35):

```
run-once: sOS probe: 0x16091: 0x00000000
run-once: sOS probe: 0x1607a: 0x00000000
run-once: sOS probe: 0x16063: 0x80000000
```

C2PMSG_81 = 0: the sOS is not running after a warm restart, so the GPU is reset somewhere between macOS's restart
and the next boot (firmware PCI reset or the VBIOS POST; which one is not known). The boot then reached `SUCCESS at
06:34:00` (240 Hz, previous shutdown cause 5). One restart, so one sample: the 16 warm restarts of 2026-11-24 are
consistent with it but recorded nothing.

**Consequence.** #22's warm refusal does not fire on a macOS restart; it stays the guard for a Linux warm reboot and
for a GPU the bring-up script already ran on in the same boot. #22 can deploy as planned. From step (a) on, the
daemon logs `sOS check: C2PMSG_81=…` at every boot, which adds samples.
