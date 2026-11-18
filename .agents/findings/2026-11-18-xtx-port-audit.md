# 2026-11-18: XT vs XTX audit

Method: read the source; query the live XTX under Linux (libdrm `AMDGPU_INFO_*` ioctls, sysfs, the VBIOS image through
`AMDGPU_INFO_VBIOS_IMAGE`); diff against Mesa `f5cb8ee0` and Linux amdgpu master. Nothing was run on macOS yet.
Done by a Sonnet 5.5 sub-agent; the two key claims (the profile diff and the aperture arithmetic) were re-checked by
hand. Confidence: H high, M medium, L low.

## XTX as measured (Linux, libdrm, renderD128)

```
asic 0x744c, chip_rev 0, ext_rev 0x1, pci_rev 0xc8, family 145 (GC 11.0.0)
gb_addr_cfg 0x545, 6 SE x 2 SA, cu_active 96, rb 24 (mask 0xffffff), tcc 24 (disabled 0)
vram 384-bit GDDR6 (type 9), 25753026560 B total (MC 0x80_0000_0000-0x85_FEFF_FFFF), BAR 32 GB
max sclk 2482 MHz, max mclk 1249 MHz, gl2c 6144 KB, mall 96 MiB, PCIe 4.0 x16
cu_bitmap SE0-3: 0xff 0xff (SA0, SA1)
VBIOS 113-31XFSHBS1.L04, 2024/01/10. Power cap 327 W (max 350, min 294)
```

## Verdicts

| Claim | Verdict | Evidence | Conf |
|---|---|---|---|
| Canned `navi31_xt` mixes XT and XTX values | **Confirmed, harmless.** It is upstream Mesa's `navi31` (an XTX dump) with 5 overrides: `pci_rev 0xcc`, `cu_active 84`, `cu_bitmap 0xfc..`, `vram_bit_width 320`, `tcc_disabled 0xc03`. Diff verified by hand | `patches/mesa/0016:21-150` vs Mesa `amdgpu_devices.c` `navi31` | H |
| Canned heap sizes matter | **Refuted.** With the kext attached, heaps come from `acd_info_memory()`: 256 MiB visible + 6 GiB "hi" VRAM, 2 GiB GTT, fixed in `phx_globals.cpp:82-86`. The canned `mem` block only feeds mocks | `0002:581-668`; `phx_client.cpp:365-396` | H |
| `RADV_DARWIN_CANNED=navi31` gives a correct XTX profile | Confirmed, except two exact-name checks | `0017:112` (`acd_vcn`), `0016:178-180` (marketing name falls through to "RX 9070 XT") | H |
| XT profile on an XTX is unsafe | **Refuted.** `max_good_cu_per_sa` = 8 for both. The XT bitmap under-provisions, never over-provisions. `tcc_disabled 0xc03` makes `tcc_rb_non_coherent` true, which means more L2 flushes and TC-compat HTILE restrictions, i.e. perf, not correctness | Mesa `ac_gpu_info.c:587-597,1226-1305`, `radv_cmd_buffer.c:7915`, `radv_image.c:1111,1123` | H |
| 20 GiB hardcoded somewhere | **Confirmed, latent.** `mesboot.c:131` `SYSTEM_APERTURE_HIGH = 0x00213fbf`. For the XTX it is `(0x85FE<<24 \| 0xFFFFFF)>>18 = 0x00217fbf` (verified). Pipe-1 plans write HUBP1 aperture `0x00213fff` (XTX `0x00217fff`). `PhxDmub.h:6` is only a comment | | H |
| VRAM size is hardcoded | **Refuted.** Read from `FB_LOCATION_BASE/TOP` | `phx_core.cpp:182-206` | H |
| UMC count / harvest matter to the kext | **Refuted.** No UMC constants; the kext never touches the shader-array config | grep | H |
| Firmware offsets are XT-specific | **Refuted.** They depend on the pinned blob revision. XT and XTX both use non-kicker files (Linux `amdgpu_is_kicker_fw`: only `744B/00` and `7551/C8`) | `bringup_navi31.sh`, `fw.lock` | H |
| Power table is XT's | **Refuted.** PMFW uses the card's own PPTable; Vermilion only sends UCLK/FCLK min/max | `phx_debug.cpp:55-77`, `bringup_navi31.sh:143` | H code / M origin |
| Connector table is XT-only | **Refuted.** `PhxVbios.h` run on the XTX VBIOS yields the same 4 paths as `PhxDdc.h boardConnectors()`. Which socket the LG uses is **not settled**. Linux `card1-DP-1/2/3` own `drm_dp_aux0/1/2` ("hw bus 0/1/2"), while the VBIOS paths list AUX3/AUX4/AUX2, so the two numberings differ. To settle it: `amdgpu_dm_dtn_log` or `link_settings` in debugfs (root) at 1440p, or the macOS boot log | | H table / L socket |

## Display at 1440p (the real work)

- The GOP lights 2560x1440 at 59.95 Hz (DTD1, 241.5 MHz, totals 2720x1481). PhxAMDFB takes that geometry and
  `singleMode()` matches it, so **1440p60 should come up unchanged**.
- These refuse any mode except upstream's two 1080p OTG totals (`0x897/0x464`, `0x807/0x44b`). They refuse cleanly,
  with zero writes:
  - `PhxPipe2.h:125,232-236`
  - `PhxPipe0.h:610-624,674-683`
  - `PhxDpStream.h:455`
  - `b15_boot.sh:210,240,403,630`
  - `hubp_dlg.sh:71`
  - `p0prefetch.sh:81`
- The pipe-0 engine also requires an HBR x4 link at 148.5 MHz `DP_VID_M` (`PhxPipe0.h:178,678-683`). 1440p144 needs
  HBR2 x4 (14.25 Gbit/s). Native training exists only for DP A/E, and only up to HBR2 (`PhxDpLink.h:39-43,110-129`).
- Scan slots are 8 MiB at VRAM 24/32/40 MiB, but 1440p needs 14.06 MiB, so GPU present is impossible
  (`PhxNativeABI.h:268-271`, `phx_scan.cpp:130-131,193-195`).
- The EDID read stops at 256 bytes; the LG's is 384. Block 2 needs the E-DDC segment pointer
  (`PhxDdc.h:371-397`, `PhxEdid.h:42-48`).
- The GOP DMCUB version is gated to `0x07008742` in 5 places (`PhxDmub.h:260,351,429`, `PhxDpRun.h:152`,
  `PhxPipe2.h:1107,1254`). The value on the XFX VBIOS is unknown.
- Watermarks, DLG/TTU, DET and DAL values were all captured on the 320-bit XT at 1080p, so they must be re-captured.

## Risks

1. **Warm boot after Linux** (M). The bring-up assumes sOS isn't running and the SMU isn't loaded
   (`bringup_navi31.sh:56-77`). Upstream already saw a hang after a warm reboot (`INSTALL.md:555-565`). Power off
   fully.
2. **Hang-capable operations:**
   - loading SMU firmware twice;
   - DAL `SetAlwaysWaitDmcubResp 0x14` (already off the allow-list);
   - `debug.phxdmub_restart`;
   - any wrong debug register write (`PHXN_SEL_DBG_WRITE_REG` = arbitrary MMIO for root with `phxdebug=1`).
3. **Thermals.** Without `gfx_cg.sh` and `sdma_idle.sh` the idle card sits at about 250 W and a 110 °C hotspot. Use
   the flags from the first boot.
4. **Reboot watchdog.** `shutdown -r +3` stays armed for the whole bring-up. If that `shutdown` is killed, it leaves
   `/etc/nologin` behind.

## Where upstream stopped

- **Sleep/wake:** nothing. The only power hook is the IH ring's sleep/wake interest (`phx_ih_irq.cpp:62`).
- **DP/HDMI audio:** no code; `PhxDpLink.h:17-19` skips audio, SDP and VSC on purpose.
- **More than two monitors:** `kMaxHeads = 2`. The multiport work (b112-b122) reached a cloned DIG on another
  connector and a DP watcher, but no independent third head.
- **Video:** `PhxVA.bundle` is a placeholder.
- **Platform:** `amfi=0x80` costs FairPlay and TCC prompts; FileVault is untested.

## Open questions (need a test)

- **Linux, root, read-only:**
  - DCN registers at 1440p 60/144/240;
  - `amdgpu_dm_dtn_log`;
  - DPCD (`/dev/drm_dp_aux2`);
  - GFXHUB aperture and MMHUB `FB_LOCATION` readback;
  - which DIG/HPD each DP socket uses.
- **macOS, first cold boot, logging only:**
  - DMCUB version;
  - the GOP's DP link rate and lanes;
  - OTG0 totals (expect `0xA9F/0x5C8`);
  - C2PMSG_81 = 0 on a cold boot;
  - `phx-fb-size`, BAR0 length under OpenCore;
  - whether CoreDisplay accepts the 256-byte EDID;
  - CPU-copy speed of 14 MiB frames.
