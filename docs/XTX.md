# RX 7900 XTX support

Status of running Vermilion on an RX 7900 XTX (Navi 31, PCI `1002:744c`, revision `0xC8`). Upstream was built and
tested only on an RX 7900 XT (revision `0xCC`). This page lists what is the same, what differs and what needs
doing. The audit behind it: [`.agents/findings/2026-11-18-xtx-port-audit.md`](../.agents/findings/2026-11-18-xtx-port-audit.md).

**Status (2026-11-18): audited from source and from Linux on the XTX; not yet run on macOS.**

## Same as the XT (no change needed)

The XTX is the same die with nothing disabled. These were checked against the XTX under Linux:

- IP versions (GC 11.0.0, PSP/SMU 13.0.0, DCN 3.2, VCN 4.0.0) and IP-discovery register bases.
- `GB_ADDR_CONFIG = 0x545` (asserted by `vcn_up.sh`).
- Firmware: the same non-kicker files. Linux picks kicker firmware only for other device/revision pairs.
- PSP TMR size `0x1300000` and VRAM base `0x80_0000_0000`.
- VRAM size: the kext reads it from the memory controller, so 24560 MiB is picked up automatically.
- Connector table: the XTX VBIOS has the same four paths as the hardcoded table in `PhxDdc.h`.
- Power limits and fan curves come from the card's own firmware. Vermilion sends no power-limit or fan messages.
- The kext matches `0x744c1002` with no revision check. Since build 125 it names a `0xC8` card "Radeon RX 7900 XTX".

## Differences that need code changes

| # | What | Where | Fix | Impact if left |
|---|---|---|---|---|
| 1 | RADV canned profile is the XT's: 84 CU, 320-bit, 4 of 24 cache channels off | `patches/mesa/0016` | Add `navi31_xtx` (= upstream Mesa's `navi31` entry, which already describes an XTX) and choose the profile by PCI revision. A quick test needs no rebuild: `RADV_DARWIN_CANNED=navi31` | Works, but extra cache flushes (`tcc_rb_non_coherent`), the GPU reports 84 cores |
| 2 | Two shims compare the profile name to `"navi31_xt"` exactly | `patches/mesa/0017` (`acd_vcn`), `0016` (marketing name) | Match by family / prefix | With another profile: no video decode, wrong name ("RX 9070 XT") |
| 3 | GFXHUB `SYSTEM_APERTURE_HIGH` hardcoded for 20 GiB (`0x00213fbf`) | `tools/phxctl/mesboot.c:131` | `0x00217fbf` for 24 GiB, better: derive from `FB_LOCATION_TOP` | Latent: nothing uses VRAM above 20 GiB today |
| 4 | HUBP1 aperture in pipe-1 plans (`0x00213fff`) | `phx_s2_plan_*.inc` | `0x00217fff`, or derive | Latent; pipe 1 is the second-monitor path |
| 5 | Metal core count 84 | `src/metal-plugin/nvmtl_corecount.c` | follow the profile | Cosmetic |
| 6 | Docs say revision `0xCC` only | `README.md`, `docs/INSTALL.md`, `bringup_navi31.sh` | Name both | Cosmetic |

## A 1440p monitor (LG ULTRAGEAR+, 2560x1440 at up to 240 Hz)

Upstream's display code is built around two 1080p monitors. On the XTX machine, with one 2560x1440 DisplayPort
monitor, this works from System Settings > Displays (verified on the XTX machine, 2026-11-20):

- **Every rate at 2560x1440: 60, 120, 144 and 240 Hz**, in any order. 240 Hz runs HBR3 with DSC and ODM 2:1, as Linux
  drives it. VermilionBoot's replay engine (`PhxP0Odm.h`, b135+) replays Linux's own mode sets, captured on the same card
  and monitor (`tools/capture/linux_display_capture.py`, `tools/capture/p0odm_from_trace.py`). Every switch goes through
  Linux's 144 Hz and retrains the link at the lowest rate that fits.
- **Every other mode the monitor lists:** 1920x1080 at 60 and 120 Hz, 1280x1024, 1024x768, 800x600 and 640x480. The
  desktop resizes (PhxAccel reports each mode's own geometry with Linux's 256-pixel pitch, b138).
- **The login screen comes up at the saved rate** (b140).
- **Adaptive Sync:** System Settings offers "Variable (48-240 Hz)" next to the fixed rates (PhxAccel publishes the
  timing range with Apple's VRR flag and a variable twin of the 240 Hz mode). Picking it turns on OTG0's V_TOTAL range
  the way Linux's DCN 3.2 code does on a firmware without fw-assisted mclk switching, which the GOP's DMCUB build is
  (b141-b144; `debug.phxvrr`, `debug.phxvrr_stat`). It works only at 2560x1440: the per-flip trigger that ends a frame
  comes from GPU present, and the other sizes are drawn by the CPU copy. `~/gpu/phxboot-novrr` turns the offer off.
- **GPU present** draws 2560x1440 at every rate (b126 slots, b143 under ODM 2:1). It runs only at the size it started at
  (Metal plugin): the other sizes are drawn by the CPU copy. A size change gives the plane back first (b139).

Not done yet: HDR (the Linux traces are in capture v3), and Variable at the other sizes.

What macOS needs for Adaptive Sync, and where each value comes from:
[findings/2026-11-20-adaptive-sync-research.md](../.agents/findings/2026-11-20-adaptive-sync-research.md).

**NVRAM:** `DisableVariableWrite` stays on (the installer hangs on this board without it), so macOS can't clear
`phxfb-squat`. A failed accelerated boot used to reboot in a loop; since 2026-11-20 the boot daemon notices that the
squat survived its reboot and brings up a safe desktop instead (`~/gpu/phxboot-nvstuck`). Reset NVRAM in the OpenCore
picker clears the squat ([findings/2026-11-20-b138-freeze-and-squat-loop.md](../.agents/findings/2026-11-20-b138-freeze-and-squat-loop.md)).

## First boot on the XTX

Use upstream's guide ([INSTALL.md](INSTALL.md)) with these differences:

- Flags: only `phxboot-sdma`, `phxboot-cg`, `phxboot-uclk` (`456`). No `phxboot-m7*`, `phxboot-aoc*`, `phxboot-b15*`.
- Power off fully when coming from Linux.
- First boot without desktop arm (apps get Metal, the GOP keeps the screen), then the squat boot.
- Watch GPU temperature and power for the first minutes. Without clock gating the idle card runs hot.
- On the XTX machine the desktop without a driver is too slow to type in, so the files go in from Recovery:
  `tools/phxctl/esp_stage.sh` (Linux) puts them on the EFI partition and `tools/phxctl/recovery_install.sh` installs
  them; later flags come through `<EFI>/vermilion/flags/` (`tools/phxctl/esp_flags.sh`). Steps:
  [`.agents/runbooks.md`](../.agents/runbooks.md).
