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

## The bigger work: a 1440p monitor

Upstream's display code is built around two 1080p monitors. On one 2560x1440 DisplayPort monitor:

- **60 Hz should work as it is.** The GOP lights 1440p60, Vermilion keeps the mode, and PhxAMDFB takes its geometry
  from the firmware framebuffer.
- **Everything that changes the mode refuses cleanly** (zero writes). That covers the pipe-0 and pipe-2 engines,
  the DP stream fix and the `b15`/`wm144`/`hubp_dlg`/`p0prefetch` scripts. They only accept OTG totals of upstream's
  1080p modes.
- **GPU present can't be used at 1440p:** the scan slots are 8 MiB and a 1440p frame is 14 MiB. WindowServer falls
  back to a CPU copy (speed unverified).
- **No DP link retraining.** 1440p at 144 Hz needs HBR2 x4, and 240 Hz needs HBR3 x4 or DSC. Vermilion can only
  re-time the link the GOP trained.
- **The EDID is read up to 256 bytes;** the LG's is 384. The 120/144 Hz modes are in the first 256 bytes; DisplayID
  data in block 2 is lost.
- **The GOP's DMCUB firmware version is hard-gated** (`0x07008742`). If the XFX VBIOS ships another version, every
  DMUB-driven feature refuses. Unknown until the first macOS boot logs it.

What 1440p high refresh needs, in order:

1. Log the GOP state on the first macOS boot: DMCUB version (SCRATCH1), DP link rate and lane count, OTG totals,
   DPCD.
2. Capture on Linux, same card and monitor, as root: `amdgpu_dm_dtn_log` and the DCN registers per mode (OTG, HUBP
   DLG/TTU, DCHUBBUB watermarks, DET, clocks, DIG/DP), DPCD from `/dev/drm_dp_aux*`, and an ftrace of the mode
   changes.
3. Code:
   - 16 MiB scan slots;
   - EDID segment pointer (384 bytes);
   - mode matching by the EDID's own timings instead of two fixed 1080p entries;
   - DP retraining on the GOP's connector;
   - then 1440p plans for 120/144/165/240 Hz and their watermarks.

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
