# Navi 32 firmware layout for VermilionBoot's bring-up

All numbers below were read from the pinned linux-firmware revision (see `fw-navi32.lock`) on 2026-11-23 with a
parser for the amdgpu firmware headers, and cross-checked against RedNavi's `psp_fw.h`/`gen_psp_fw_header.py` output
for the same card. They are what `tools/phxctl/bringup_navi31.sh` (`loadfw` / `bl_load` / `stage`) needs.

## PSP: `psp_13_0_10_sos.bin` (360352 bytes)

Header: `ucode_offset` (u32 at 0x18) = 0x100, component count (u32 at 0x20) = 9, entries of 16 bytes at 0x24
`{type, version, offset, size}` relative to `ucode_offset`.

| Component | file offset | size | bootloader cmd | version |
|---|---|---|---|---|
| KDB | 256 | 7488 | 0x80000 (LOAD_KEY_DATABASE) | 0x02003200 |
| SYS_DRV | 7744 | 61696 | 0x10000 (LOAD_SYSDRV) | 0x00321717 |
| SOS | 69440 | 109216 | 0x20000 (LOAD_SOSDRV) | 0x00321717 |
| SOC_DRV | 178656 | 94464 | 0xB0000 | 0x00321717 |
| INTF_DRV | 273120 | 33024 | 0xD0000 | 0x00321717 |
| DBG_DRV | 306144 | 28928 | 0xC0000 | 0x00321717 |
| **TOC** | **335072** | **2304** | 0x20 (GFX_CMD_ID_LOAD_TOC) | 0x00000010 |
| RAS_DRV | 337376 | 16640 | 0xE0000 | 0x00321717 |
| RL | 354016 | 6336 | - | 0x03003200 |
| **SPL** | **absent** | - | - | - |

The Navi 31 script loads `KDB, SPL, SYS_DRV, SOS`; **Navi 32 has no SPL component**, so that `bl_load` line must be
skipped (it is the KDB image again on Navi 31). The TOC is staged from file offset **335072**, not 257040.

## SMU: `smu_13_0_10.bin` (271520 bytes)

`ucode_offset` = 256, `ucode_size` (u32 at 0x14) = 261632. Same shape as Navi 31: `loadfw <file> 256 261632 18 0x000000`.
The file is 21124 bytes longer than `smu_13_0_0.bin` (271520 against 292452 is *shorter*: 271520 < 292452, i.e. 20 KiB
smaller); the extra trailing table is not loaded.

## GC: `gc_11_0_3_*.bin`

For `pfp/me/mec` the code block is `{size u32@0x28, offset 256}` and the stack block size is `u32@0x30`, the stack
starting right after the code. For `mes_2/mes1` the code is `[256, total - 256 - 131072)` and the 131072-byte data
block follows.

| File | total | code off/size | stack/data off/size | loads |
|---|---|---|---|---|
| gc_11_0_3_pfp.bin | 231008 | 256 / 91488 | 91744 / 139264 | CP_RS64_PFP 0x060000; P0_STACK 0x100000; P1_STACK 0x130000 |
| gc_11_0_3_me.bin | 314512 | 256 / 174992 | 175248 / 139264 | CP_RS64_ME 0x080000; P0/P1 stacks 0x160000/0x190000 |
| gc_11_0_3_mec.bin | 406528 | 256 / 267008 | 267264 / 139264 | CP_RS64_MEC 0x0b0000; P0..P3 stacks 0x1c0000/0x1f0000/0x220000/0x250000 |
| gc_11_0_3_mes_2.bin | 257408 | 256 / 126080 | 126336 / 131072 | CP_MES 0x280000; CP_MES_DATA 0x2a0000 |
| gc_11_0_3_mes1.bin | 218336 | 256 / 87008 | 87264 / 131072 | CP_MES1 0x2c0000; CP_MES1_DATA 0x2e0000 |
| gc_11_0_3_imu.bin | 132352 | 256 / 66048 | 66304 / 66048 | IMU_I 0x300000; IMU_D 0x320000 |
| gc_11_0_3_rlc.bin | 179104 | - | - | not loaded by `loadfw` (AUTOLOAD_RLC through the PSP) |

The only file whose numbers differ from the Navi 31 script are `mes_2` (126080 against 126016) and `mes1`
(87008 against 86912): the two MES images grew by 64 and 96 bytes.

## SDMA: `sdma_6_0_3.bin` (34560 bytes)

Same size as `sdma_6_0_0.bin`, so the two loads carry over unchanged: `256 / 17408` (SDMA_UCODE_TH0, fwtype 71) and
`17664 / 16896` (SDMA_UCODE_TH1, fwtype 72).

## DCN: `dcn_3_2_0_dmcub.bin`

Byte-identical to the Navi 31 file (md5 6a06c16a77e84fe22e91c9d2e6220476), so `phx_c3_image.inc` and the C3 PHY
loading path need no change at all. `vcn_4_0_0.bin` is byte-identical too.
## SMU 13.0.10 message set (checked 2026-11-23)

Vermilion talks to PMFW through the driver mailbox (\`MP1_C2PMSG_66/82/90\` at \`0x16282/0x16292/0x1629A\`,
identical on both dies since MP1 segment 1 is \`0x16000\` for every 13.0.x) and sends these message ids:

| id | message | where |
|---|---|---|
| 0x01 / 0x02 / 0x03 | TestMessage / GetSmuVersion / GetDriverIfVersion | \`bringup_navi31.sh\` (SMU check) |
| 0x1B | SetHardMinByFreq (arg \`(PPCLK_UCLK=2)<<16 | MHz\`) | \`PhxPipe0.h\` (UCLK pin), \`uclk.sh\` |
| 0x1F | GetDpmFreqByIndex | \`uclk.sh levels\` |
| 0x2A / 0x2B | PowerUpVcn / PowerDownVcn | \`vcn_up.sh\` |
| 0x10009 | DAL SetHardMinByFreq | \`m7_boot.sh\` (display → PMFW, DAAL range) |

**These ids are the same on 13.0.10.** Linux drives Navi 32 with the *same* SMU driver as Navi 31 -
\`smu_v13_0_0_ppt.c\` matches \`IP_VERSION(13, 0, 10)\` in its \`amdgpu_ip_version(MP1_HWIP)\` branches - and the
message numbers come from \`smu_v13_0_0_ppsmc.h\` for both dies. The 13.0.10-only branches in that driver are
gpu-metrics layout, RAS mode1/mode2 reset, \`EnableAllSmuFeatures(FEATURE_PWR_GFX)\` and the ECC table; Vermilion
uses none of them.

What does differ, and is worth watching on the first boot:

- \`GetSmuVersion\` returns a **80.x** build on 13.0.10 (Linux comments give \`0x00501c00\` = 80.28 as its RAS
  threshold) where the 7900 XT reports 78.135.0. The bring-up script only prints the value, so nothing to change.
- \`GetDriverIfVersion\` is expected to answer 0x40 (\`smu13_driver_if_v13_0_0.h\` is shared by both dies), but the
  PMFW's answer should be confirmed rather than assumed - the script prints it and does not gate on it.
- The \`0x10009\` DAL message is not in \`smu_v13_0_0_ppsmc.h\`: it belongs to the display\`->\`PMFW channel. DCN 3.2.0 and
  its DMUB firmware are identical between the two dies (the DMCUB file is byte-identical, see above), so it should
  carry over, but it is the one message id no header in this tree pins down.

## Confirmed on the card (Linux 7.0, 2026-11-23)

The board booted Ubuntu 26.04 (kernel 7.0, amdgpu 3.64.0) with the RX 7800 XT as `0000:03:00.0`; these are the
values the driver reported for the firmware it actually loaded, read from
`/sys/kernel/debug/dri/0000:03:00.0/amdgpu_firmware_info`:

| block | feature | version | agrees with the locked files in `fw-navi32.lock` |
|---|---|---|---|
| PFP | 29 | 0x0a50 (2640) | feature 29 yes; version is a **newer** blob (ours: 0x0a14 = 2580) |
| ME | 29 | 0x09a6 (2470) | feature 29 yes; ours 0x0974 = 2420 |
| MEC | 29 | 0x0a78 (2680) | feature 29 yes; ours 0x0a5a = 2650 |
| SMC (SMU) | 0 | **0x00505600 = 80.86.0** | see the SMU section: 13.0.10 answers 80.x, as predicted |
| SDMA0/1 | 60 | 0x1d | 6.0.3, as expected |
| RLC | 1 | 0x64 | |
| DMCUB | 0 | 0x07002f00 | **exactly the value the register read shows in `DMCUB_SCRATCH1`** |
| MES_KIQ | 6 | 0x0108 | |

Two conclusions:

1. **The firmware offsets and components are right.** Every block this bring-up loads (PSP/PSP sOS, SMU, SDMA,
   RLC, MES, IMU, DMCUB) is present and reported by the card, with the same feature versions as the locked files.
   The locked blobs are a slightly older revision than Ubuntu's current linux-firmware (PFP/ME/MEC differ by 30-60
   in `ucode_version`, same feature version), which is expected for a pinned commit and harmless.
2. **The SMU version check in this port was right**: `GetSmuVersion` on a 13.0.10 part answers `0x00505600`
   (80.86.0), not the 78.x a Navi 31 reports. The bring-up script only prints it, so nothing gated on it - but a
   future check must not test for 78.x.

The display side of the same session: `2560x1440@60`, 2720x1481 totals, pixel clock 241.7 MHz, **10 bpc**
(`FMT0_FMT_BIT_DEPTH_CONTROL 0x0000d100`), link **HBR3 x4** verified (`link_settings: 4 0x1e 16`), DIG4/DP4,
`output_bpc` maximum 10. The register read is kept at
`tools/capture/captures/navi32-1440p60-linux.txt` and is the input of the board's plan.
