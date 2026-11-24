# Navi 32 (RX 7800 XT / RX 7700 XT) port

This branch adapts Vermilion's Navi 31 driver to **Navi 32** — the RX 7800 XT (`1002:747e`, 16 GiB) and the
RX 7700 XT (same device id, 12 GiB). Everything below was derived from a real RX 7800 XT (RedNavi's probes of
that board) and from Linux's own AMDGPU sources; the parts marked *needs the card* have not run yet.

## What is different from Navi 31

| Block | Navi 31 (reference) | Navi 32 | Where it is handled |
|---|---|---|---|
| GC | 11.0.0 | **11.0.3** | `phxSelectAsic()` sets `gGcVmShift` (+0x10) and `gGcL2Shift` (+0x04) |
| MP0/MP1 (PSP/SMU) | 13.0.0 | **13.0.10** | firmware files; the message set is unchanged (see NAVI32-FIRMWARE.md) |
| SDMA | 6.0.0 | **6.0.3** | firmware file |
| DCN / MMHUB / NBIF / HDP / VCN | 3.2.0 / 3.0.0 / 4.3.0 / 6.0.0 / 4.0.0 | the same | nothing to do |
| `GB_ADDR_CONFIG` | 0x545 | **0x444** | `phxSelectAsic()`, reported to RADV |
| Monitor path | OTG0 + DIG1 + DP1 | **OTG0 + DIG4 + DP4**, HPD4 | `PHX_PIPE_DIG=4`, own plan |

The GC memory hub moved two register groups between 11.0.0 and 11.0.3 (the VM context/aperture block by +0x10
dwords, the L2/fault block by +0x04); Vermilion touches those in `phx_vm.cpp`, `phx_vcn_mmhub.cpp` and
`phx_wait.cpp` only.

## Build

```sh
# 1. firmware for this die (md5-checked against tools/firmware/fw-navi32.lock)
tools/firmware/fetch_firmware.sh --lock fw-navi32.lock ~/gpu/fw/navi32     # or copy the files in your own way

# 2. the boot kext for Navi 32: DIG4 plan + ~/gpu/fw/navi32
MKSDK=/path/to/MacKernelSDK ASIC=navi32 scripts/build/kext.sh

# 3. the accelerator kext (unchanged by the port so far)
MKSDK=/path/to/MacKernelSDK scripts/build/accel.sh
```

`ASIC=navi31` (the default) still builds the reference driver byte for byte. For Navi 32 the build sets
`PHX_PIPE_DIG=4`, `PHX_PIPE_HPD=4` and picks `phx_p0_plan_1080p60n32.inc`.

## The display plan

`src/vermilion-boot/phx_p0_plan_1080p60n32.inc` is generated from a Linux register capture of this exact board:

```sh
python3 tools/capture/navi32_dump_to_regs.py <dcn_pipe dump> --out regs.txt
python3 tools/capture/p0plan_from_regs.py --template src/vermilion-boot/phx_p0_plan_1080p60.inc \
    --regs regs.txt --name 1080p60n32 --hz 60 --pclk 148500 --depth 8 --dig-shift 0x300 --fill-from-template \
    --source 'Linux amdgpu on the RX 7800 XT, DIG4/DP4, 1080p60' > src/vermilion-boot/phx_p0_plan_1080p60n32.inc
python3 tools/capture/navi32_plan_check.py src/vermilion-boot/phx_p0_plan_1080p60n32.inc regs.txt --abs
```

**The board's real plan is `phx_p0_plan_1440p60n32.inc`**, generated from a live Linux read of the card
(`tools/capture/captures/navi32-1440p60-linux.txt`, 61 registers, 2026-11-23): 2560x1440@60, 2720x1481 totals,
pixel clock 241.7 MHz, 10 bpc, DIG4/DP4. Its check is **29 entries matching, 0 mismatch, todo 0** - no value is
borrowed from the reference machine any more. The file is reproduced byte for byte from the archived capture by
the command above (with `--depth 10 --pclk 241700 --derive-scaler-blank`).

`phx_p0_plan_1080p60n32.inc` (from RedNavi's older 1080p60 dump) still carries 13 HUBPREQ0 values from the
reference machine, flagged TODO-CAPTURE and counted in its `info.todo`; it is only used if a 1080p mode is
planned on this board.

How the two were captured:

* `modetest -M amdgpu` cannot pick a card, and the greeter blanks after ~35 s, so the read is taken right after
  waking the display (`chvt 2; chvt 1`) and the register node is `/sys/kernel/debug/dri/0000:03:00.0/amdgpu_regs`
  (the box has two AMD GPUs: the Navi 32 at 03:00.0 and a Granite Ridge iGPU at 0c:00.0).
* The DTO register on Linux holds *half* the pixel clock (two pixels per cycle); the plans use the firmware's
  convention, so the pixel clock is passed as `--pclk 241700` and the generator writes 241,700,000.

## The plans, one per captured mode

Every plan below was generated from a live Linux register read of **that same mode on this same monitor**
(`tools/capture/captures/`), so no value is borrowed from the reference machine and each check reports
**29 entries matching, 0 mismatch, todo 0**:

| plan | mode | pixel clock | totals |
|---|---|---|---|
| `phx_p0_plan_1080p60n32.inc` | 1920x1080@60 | 148.500 MHz | 2200x1125 |
| `phx_p0_plan_1440p60n32.inc` | 2560x1440@60 | 241.700 MHz | 2720x1481 |
| `phx_p0_plan_1440p100n32.inc` | 2560x1440@100 | 402.840 MHz | 2720x1481 |
| `phx_p0_plan_1440p120n32.inc` | 2560x1440@120 | 497.760 MHz | 2720x1525 |
| `phx_p0_plan_1440p144n32.inc` | 2560x1440@144 | 597.290 MHz | 2720x1525 |
| `phx_p0_plan_1440p165n32.inc` | 2560x1440@165 | 657.500 MHz | 2720x1465 |

All six are 10 bpc (the monitor's `output_bpc` maximum, and what Linux programs at every rate). The boot kext's
target list now names 100 and 165 in addition to 60/120/144 (`phxPipe0SetMode`, the `debug.phxpipe0_mode`
selector and the `setDisplayMode` validation), and `p0Pick` takes the requested rate straight from the plan
family - a rate the running family has no plan for still falls back to its 60 Hz plan, which keeps the 1080p
behaviour identical (120 there was never a plan).

### How a mode is captured

`modetest` sets a mode and exits, and the kernel restores the fbdev/preferred mode when DRM master is dropped - the
mode is gone before a register read can be taken (observed: 2560x1440@100 reverts to 60 within a second).
`tools/capture/navi32_kms_hold.py` is a ~200-line ctypes DRM client that creates a dumb scanout buffer, sets one of
the monitor's modes and **keeps DRM master** while it sleeps, so the read can be taken. The box also has an APU, so
the sequence first stops the greeter, unbinds the iGPU (`echo 0000:0c:00.0 > /sys/bus/pci/drivers/amdgpu/unbind`)
so `/dev/dri/card1` is the only amdgpu card, and unbinds fbcon so nothing restores a mode behind us:

```sh
sudo systemctl stop gdm
sudo sh -c 'echo 0000:0c:00.0 > /sys/bus/pci/drivers/amdgpu/unbind'
sudo sh -c 'echo 0 > /sys/class/vtconsole/vtcon1/bind'
for rate in 60 100 120 144 165; do
  sudo python3 tools/capture/navi32_kms_hold.py /dev/dri/card1 378 364 $rate 25 &
  sleep 4
  sudo python3 tools/capture/navi32_linux_capture.py --node /sys/kernel/debug/dri/0000:03:00.0/amdgpu_regs \
      --reglist tools/capture/navi32-dcn-reglist.txt --out regs-$rate.txt
  kill %1; wait
done
# restore: vtcon1 bind 1, amdgpu unbind -> bind, systemctl start gdm
```

The display-clock floors in the Plan structs (DAL `SetHardMinByFreq`) are the one value **not** read from the card:
Linux's SMU messages are not in a register dump, so they are the pixel clock rounded up to the next 10 MHz
(410/500/600/660 MHz), which can only be an over-request - the SMU sets a floor, and too high costs power while too
low would fail. `pp_dpm_fclk` confirms the fabric clock does step with the rate (601 MHz at 60 Hz, 1000 at 100/120,
1200 at 144/165).
## Which modes the accelerator offers for this board

`ASIC=navi32` also builds `VermilionAccel.kext` with `PHX_PIPE_DIG=4`, and the head-0 mode table then carries only
what this board has a plan for:

| id | mode | plan |
|---|---|---|
| 1 | 1920x1080@60 | `phx_p0_plan_1080p60n32.inc` (13 HUBPREQ0 values still from the reference machine) |
| 3 | 2560x1440@60 (P27QBA-RX base DTD: 2720x1481, 241.7 MHz) | `phx_p0_plan_1440p60n32.inc`, every value from this board |

The reference machine's rows (LG ULTRAGEAR+ 1440p 100/120/144/240, HDR and VRR twins, the AOC 1080p144) are left
out of the Navi 32 build on purpose: **an offered mode without a plan would scan out with the wrong timings**. They
come back per mode as this monitor gets its captures (its EDID has a 100 Hz DTD plus DisplayID timings; Linux
trains HBR3 x4 here, so 1440p100 and 1440p144 are realistic next targets).

The boot kext and the accelerator kext have to be built with the **same** `ASIC`, because the pipe index chooses
both the plan and the mode table. `tools/phxctl/esp_stage.sh` takes `ASIC` for the firmware side as well.
## First light on the card (not run yet)

1. Stage the payload with `ASIC=navi32 tools/phxctl/esp_stage.sh <dest>` (it takes the firmware from
   `~/gpu/fw/navi32` and checks `fw-navi32.lock`).
2. Install `VermilionBoot.kext` through OpenCore, keep SSH and a USB copy of the working EFI.
3. Walk the checkpoints in this order, each of which RedNavi already reached on this GPU:
   PSP sOS (`C2PMSG_81` non-zero, version `C2PMSG_58`) -> SMU answers (`smu 0x01 0x1234` returns 0x1235) ->
   RLC/MES up (`mes-step` 5..7, `gfx-queue` step 7) -> VM -> DP link training (tx/cr/eq ok) -> OTG0 scans
   `phx_p0_plan_1080p60n32` into DIG4.
4. `phxctl` selectors 9..14 publish the scanout; then install `VermilionAccel.kext` into the auxiliary kernel
   collection and run `metaltest`.

If the DP link trains but the screen stays black, the suspects in order are: the 13 template watermarks
(regenerate after a HUBP capture), the DIG4 front-end source select in the plan, and the `PHX_PIPE_HPD` value
for the retrain path.

## RADV (built and verified, 2026-11-23)

The full 20-patch series was applied to a clean Mesa checkout at `f5cb8ee0` and **built on macOS** (meson 1.12,
Apple clang 21, x86_64 cross-file from `scripts/build/radv.sh`): `libvulkan_radeon.dylib`, 20.7 MB, ad-hoc signed,
x86_64, and it carries the new device - `strings` shows `navi32_xt` and `AMD Radeon RX 7800 XT`.

```sh
git -C $MESA_DIR am patches/mesa/*.patch        # or: MESA_DIR=... scripts/build/radv.sh apply
MESA_DIR=... scripts/build/radv.sh               # configures once, then ninja + codesign
```

Only Python build deps are needed beyond Xcode and meson/ninja/glslangValidator (`mako pyyaml markupsafe
packaging`; a venv works if the system Python refuses installs).

## RADV

`patches/mesa/0020-radv-darwin-canned-navi32_xt-RX-7800-XT-measured-har.patch` adds the canned `navi32_xt`
device (every number from this board's `DRM_AMDGPU_INFO`) and picks it by PCI device id 0x747e; the GC version
it reports is 11.0.3. Apply it with `scripts/build/radv.sh apply` on a machine with the Mesa checkout.

## Files worth knowing

| File | What |
|---|---|
| `tools/phxctl/NAVI32-FIRMWARE.md` | the firmware set and the exact load offsets inside each file |
| `tools/firmware/fw-navi32.lock` | md5s of the 13 files, at the same pinned linux-firmware commit |
| `tools/capture/navi32_dump_to_regs.py` | DCN dump -> Vermilion's absolute register addresses |
| `tools/capture/p0plan_from_regs.py` | register read -> plan (with `--dig-shift` and `--fill-from-template`) |
| `tools/capture/navi32_plan_check.py` | plan -> capture consistency check, no hardware needed |
| `tools/capture/navi32_linux_capture.py`, `navi32-dcn-reglist.txt`, `navi32_linux_capture_ssh.sh` | the register read of a mode, taken on Linux (locally or over SSH) |
## What the port touched, one by one

| File | Change |
|---|---|
| `src/vermilion-boot/phx_globals.cpp` | `phxSelectAsic(deviceId)`: GC version, `GB_ADDR_CONFIG`, the two memory-hub shifts |
| `src/vermilion-boot/phx_core.cpp` | selects the ASIC at start, publishes `phx-asic`, names 0x747e from its VRAM |
| `src/vermilion-boot/phx_client.cpp` | RADV gets the selected `gb_addr_config` / `gc_version` |
| `src/vermilion-boot/phx_vm.cpp` | GC context registers through `gGcVmShift` |
| `src/vermilion-boot/phx_vcn_mmhub.cpp`, `phx_wait.cpp` | the same shift (+ the fault-status block's +0x04) |
| `src/vermilion-boot/PhxPipe0.h` | `PHX_PIPE_DIG` / `PHX_PIPE_OTG` / `PHX_PIPE_HPD`: the shell's DIG/DP registers, the allow tables and the pipe-1 OTG |
| `src/vermilion-boot/phx_pipe0.cpp` | includes the board's plan when `PHX_PIPE_DIG == 4` |
| `src/vermilion-boot/PhxDpbFix.h` | the retrain's registers follow `PHX_PIPE_DIG` / `PHX_PIPE_HPD` |
| `src/vermilion-boot/phx_p0_plan_1080p60n32.inc` | the generated plan (new file) |
| `src/vermilion-boot/Info.plist`, `src/vermilion-accel/Info.plist` | match `0x747e1002` |
| `src/vermilion-boot/build.sh`, `scripts/build/kext.sh` | `PHX_PIPE_*` plumbing, `ASIC=navi32` |
| `tools/firmware/fw-navi32.lock`, `fetch_firmware.sh --lock` | the Navi 32 microcode set |
| `tools/phxctl/bringup_navi31.sh`, `esp_stage.sh` | firmware files and offsets per ASIC; the SPL load is skipped on Navi 32 |
| `tools/capture/navi32_dump_to_regs.py`, `navi32_plan_check.py`, `p0plan_from_regs.py` (--dig-shift, --fill-from-template) | the display-plan toolchain |
| `patches/mesa/0020-...patch` | the canned `navi32_xt` RADV device |

**What already existed and is reused**: the DP link/stream/retrain machinery is DIG-parameterised
(`PhxDpLink.h regs(unsigned dig)`, with asserts for DIG0 and DIG4), and the accelerator's connector table already
carries the board's port as `DP-E` (`{hpd 4, dig 4, aux 0x24}` in `phxhp0.h`) with `PhxBringup` S2d feeding DIG0 /
DIG4 from OTG0. The port therefore did not have to invent a second display path — it had to give DIG4 a plan and
make the shell follow the index.

## Capturing the remaining values over SSH (Linux on the same machine)

The two things the plan is missing are both a *register read of the mode we want*, and that read can be taken from
Linux on the same box over SSH:

* the 13 HUBPREQ0 watermarks the first plan had to fill from the reference machine,
* the 1440p60 mode (the plan shipped so far is 1080p60, the mode the existing capture was taken at).

What the Linux side needs: amdgpu bound to the card, a mounted debugfs (`/sys/kernel/debug`), `python3`, and the
monitor **already at the mode to capture** (the plan is steady state). Root is needed to read the debugfs node.

```sh
# on this Mac: copies the reader and the register list over, runs them there, brings the read back
tools/capture/navi32_linux_capture_ssh.sh user@linux-box navi32-regs-1440p60.txt

# then, locally: the plan from that read, with no template values at all
python3 tools/capture/p0plan_from_regs.py --template src/vermilion-boot/phx_p0_plan_1080p60.inc \
    --regs navi32-regs-1440p60.txt --name 1440p60n32 --hz 60 --pclk 296500 --depth 8 --dig-shift 0x300 \
    --source 'Linux amdgpu on the RX 7800 XT, DIG4/DP4, 1440p60' > src/vermilion-boot/phx_p0_plan_1440p60n32.inc
python3 tools/capture/navi32_plan_check.py src/vermilion-boot/phx_p0_plan_1440p60n32.inc navi32-regs-1440p60.txt --abs
```

The pipeline was exercised end to end with a synthetic node: 61 registers read, plan generated with **todo 0** (no
template values), check 31 of 31 matching. The register list is `tools/capture/navi32-dcn-reglist.txt` (61
registers: the plan's own, the 13 HUBPREQ0 watermarks, and the pipe fingerprint; registers with read side effects
are excluded).

If the Linux session is a desktop with a compositor, switch the monitor to the target mode there first (Hyprland
`hyprctl keyword monitor`, KDE display settings, or `modetest` on a bare VT) — the capture only reads.
## First light on hardware, 2026-11-23 (RX 7800 XT, macOS 26.7.3)

The card ran real bring-up on the target machine. **What works today:**

| checkpoint | evidence |
|---|---|
| ASIC selection | `ioreg` property `phx-asic = navi32`, `phx-fb-size = 17179869184` (16 GiB), `phx-bar0-len = 268435456` |
| PSP sOS | `C2PMSG_81 = 0x02166e00` (non-zero, a fresh value each boot), `C2PMSG_58 = 0x00321717` |
| PSP KM ring | `psp create -> kr 0`, `C2PMSG_64 = 0x80c20000`, ring MC `0x8003000000` |
| TOC | `raw cmd 0x20 -> status 0` |
| SMU firmware | `smu_13_0_10.bin[0x100:+261632] fwtype 18 -> status 0` |
| SMU mailbox | TestMessage `0x1234 -> 0x1235` OK, **GetSmuVersion `0x00505500` (80.85.0)**, GetDriverIfVersion **`0x40`** |
| GC / SDMA / MES / IMU / RLC firmware | 24 `loadfw` calls, every one `kr 0 status 0` (sdma_6_0_3, gc_11_0_3 pfp/me/mec/stacks, mes_2, mes1, imu, and the seven RLC slices) |
| VM / GART | `mesboot gart mc -> ack17=0x1 ctx0=0xfffe01` |
| MES | `mesboot mes -> CP_MES_CNTL=0xc000000` |
| accel kext staged | `kmutil load -z -p` bypasses the approval prompt and puts `org.vermilion.accel` in the AuxKC |

**Where it stops:** `EnableAllSmuFeatures` (`0x06`) gets no response (`kr 0xe00002d5`), after
`SetDriverDramAddrHigh/Low`, `SetToolsDramAddr*`, `TransferTableSmu2Dram` and `RunDcBtc` (`0x36`) all answered OK.
Linux gives the SMU its driver/tools table in **system memory** (a GTT buffer); the script hands it VRAM
(`MC 0x80_09000000`), which is the first thing to change. The KIQ ring is consumed (`rptr 0 -> 13`) but the
`WRITE_DATA` marker does not land in `SCRATCH_REG0` (`0xcafedead` stays), so that immediate's destination may sit
at the shifted GC 11.0.3 address - the next thing to check once the SMU enables its features.

### Queues run: the KIQ and the MES scheduler ring consume packets (2026-11-24)

After a power cycle (a warm reboot leaves a wedged CP behind, and a GRBM soft reset clears the hang but
holds the GC block in reset, so only a real power cycle gives a usable GPU), the chain is:

```
boot                GRBM_STATUS 0x00000000
bringup             GRBM_STATUS 0x0000302c   (Linux's idle value)
mesfull + gart      GRBM_STATUS 0x0000302c, context 0 CNTL 0x01fffe01, page table 0x0a000001
KIQ doorbell        CP_HQD_PQ_RPTR 13/13    (the whole packet is consumed)
                    GRBM_STATUS still 0x0000302c, GCVM fault registers 0
scheduler ring      CP_HQD_ACTIVE 1, PQ_BASE 0xff0a1100, RPTR 0 -> 128 -> 256 per MES API frame
```

That is the first time any queue on this board has been consumed. The three address spaces had to be
separated first, and each one was wrong in its own way:

| what | space | port had |
|---|---|---|
| context page-table base, PDE, PTE | VRAM offset (PA), bit0 valid, bit1 system=0 | MC (0x8000000000+) |
| HQD MQD base, MAP_QUEUES MQD field, `*_gpu_mc_ptr`, ADD_QUEUE `mqd_addr` | MC | VA |
| CP_HQD_PQ_BASE, rptr/wptr report and poll addresses | GPU VA | (correct) |

plus GCVM_CONTEXT0_CNTL: context 0 is amdgpu's system domain, so PAGE_TABLE_DEPTH = 0 with the flat
GART table, not the 0x01fffe07 (depth 3) that vmids 1..15 use. Walking the flat table as four levels
faulted on the ring's own VA and wedged the CP, which is what everything before looked like.

Still open: the MES reads its API frames but never completes them. `SET_HW_RESOURCES` and
`QUERY_SCHED_STATUS` are consumed (rptr advances) and neither `api_status` nor the
`api_completion_fence` is ever written, so `mes_submit` reports TIMEOUT and the CP wedges afterwards.
The frames themselves now match the firmware: 64 dwords (`API_FRAME_SIZE_IN_DWORDS`), header
`type:4 | opcode:8 | dwsize:8` from bit 12, type 1, opcode 0 (MES_SCH_API_SET_HW_RSRC).

Next candidates, in order: (a) compare the submission with
`mes_v11_0_submit_pkt_and_poll_completion` line by line - it waits for ring space with
`amdgpu_fence_wait_polling` before allocating and uses `ring->fence_drv.gpu_addr` for the completion
fence; (b) check whether the MES needs `CP_MES_MSCRATCH_LO/HI` (the event-log pointer amdgpu sets
when `amdgpu_mes_log_enable` is on); (c) try a MEC compute HQD programmed by hand with the MES halted,
to see whether any engine executes without the firmware in the path.
### Where the port stands after the register work (2026-11-23)

Working and reproducible on every boot: PSP (all 24 firmware loads `status 0`, full bootloader chain
ending in sOS), `SETUP_TMR status 0`, `EnableAllSmuFeatures` answered, RLC booted
(`RLC_RLCS_BOOTLOAD_STATUS 0xc000001f`), MES firmware loaded and running (its scratch pattern
`0xcafebabe/0xdeadbeef` is present and the per-pipe MES words change over time), and the accelerator
kext loads (`org.vermilion.accel`, `NMXAccel` matched) with `phxclaimforce=1`.

The GCVM state now matches a live Linux read of the same card register for register: the MES block
(30+ registers, per-pipe where the hardware keeps two copies), the GCVM_L2 block at its +0x04
addresses, the invalidation ranges at +0x10, the context window (`0x7fff00000000` + 512 MiB in
*page numbers*), a real 4-level page table, and the HQD/MQD with the MQD at its MC address.

Still open: **no engine executes a single packet**. `CP_HQD_PQ_RPTR` stays 0,
`CP_HQD_PQ_RPTR_REPORT` stays 0, `CP_STAT` stays 0 and `SCRATCH_REG0` keeps the CPU's marker, on all
three MEs, with the ring at either VA base, page tables in VRAM or system memory, both PTE encodings,
and the doorbell rung at 0x60, 0x180 and two other offsets, 32-bit and 64-bit stores (BAR2 is mapped,
`phx-bar2-len` = 2 MiB). Writing `CP_HQD_PQ_WPTR` directly does not latch.

The two candidate explanations left: either the doorbell write never reaches the aperture despite the
mapping, or the MES firmware expects its KIQ ring to be registered in a way the driver's register
sequence does not capture (Linux's own first KIQ packet is `MAP_QUEUES`, and `mesboot schedkiq` fails
at exactly that point: "KIQ did not consume MAP_QUEUES").

Everything measured on both sides is recorded in `docs/captures/linux-gc-reference-navi32.txt` and
`docs/captures/linux-mes-reference-navi32.txt`, and `tools/capture/mes_dump.py` re-takes the Linux
dump in one command.
### Engine writes never land (open, 2026-11-23)

With the SMU and RLC healthy, everything that needs the GPU to *write* still does nothing, while the
CPU side is provably fine:

| test | result |
|---|---|
| KIQ ring | consumed, `CP_HQD_PQ_RPTR` 13/13, HQD registers read back exactly what was programmed |
| KIQ `WRITE_DATA` -> SCRATCH_REG0 | never arrives (0x0C040 keeps the CPU's `0xCAFEDEAD`; 0x0D2A0 and 0x3B48 checked too) |
| SDMA `WRITE_LINEAR` | target unchanged (`0x11111111`) |
| SDMA `CONSTANT_FILL` | readback `0x00000000 / 0xffffffff`, no fill |
| SDMA copy of the live desktop (`fbcomp`) | copy is `0xffffffff`, nothing landed |
| GCVM fault status | nothing recorded, and the fault-default page holds no marker either |

Ruled out: the scratch register itself (CPU write/readback works), the HQD programming (all registers read
back as written), and a wrong GC base for the ordinary CP registers (CP_ME_CNTL, CP_MEC_RS64_CNTL and the
doorbell range all accept and return writes).

Two concrete defects were found and fixed on the way (`5745ecd`): mesboot built its page tables as
`(0 + offset)`, mapping VA to physical 0 instead of VRAM; it now takes the base from `FB_PA`. Testing that
base at 0, at the BAR0 physical (`0x1080000000`) and at the VRAM MC base (`0x8000000000`) changes the KIQ
read pointer (13 vs 16385) but never makes a write land, so the page-table base is necessary but not
sufficient.

Measured on this die: `GCVM_CONTEXT0_CNTL` exists at **both** 0x28E8 and 0x28F8. 0x28E8 masks bit 24 away on
readback; 0x28F8 keeps it and holds the value Linux reports (`0x1fffe01`), so the port's **+0x10 shift for the
11.0.3 context block is right** and mesboot is still programming the unshifted one. Programming the shifted
context by hand (base `0x108A000000`, start 0, end `0xffffffff/0xf`, CNTL `0x1fffe07`) did not help either.

Next: take a Linux read of the same card (the project's usual method) - `CP_HQD_*` for the MES KIQ ring,
`GCVM_CONTEXT0_*`, `GCVM_L2_*` and the ring's page-table base - and diff it against what mesboot writes.
That distinguishes "the queue is not really running" from "the address map drops writes" with one dump.
### The TMR was the blocker (2026-11-23, fixed)

`SETUP_TMR` was built from hardcoded constants (96 MiB into VRAM, 19 MiB). On this board it answered
**status 0xffff0006**, and while the TMR is wrong PMFW ignores `EnableAllSmuFeatures` (0x06): the message times out,
leaves the mailbox latched Busy, the RLC never reaches BOOTLOAD_COMPLETE and every later stage (MES scheduler,
`ADD_QUEUE`, `mesboot claim`) fails on top of it.

The two numbers come from the card itself:

* **size**: the `LOAD_TOC` response carries it in its fifth dword. RedNavi read `0xa700000` from that same
  response on this same board; the script now reads the response out of VRAM (ring + 16 KiB + 0x360) instead of
  assuming anything.
* **placement**: Linux's dmesg on this board reserves it with `reserve 0xa700000 from 0x83e0000000 for PSP TMR`, so
  the VRAM offset is `0x3e0000000`. The six arguments are RedNavi's verified layout
  (`buffer_addr_lo/hi, size, flags=2, system_phy_addr_lo/hi`), with the GPU address = the kext's
  `phx-fb-mc-base` + offset and the system physical address = `phx-fb-aper-phys` + the same offset.

Result on the card, first run after the change:

```
LOAD_TOC response (status, session, fw_lo, fw_hi, tmr_size): 00000000 00000000 00000000 00000000 0a700000
SETUP_TMR args: mc 0xe0000000:0x00000083 (0x83e0000000) size 0xa700000 flags 0x2 pa 0x60000000:0x00000014 (0x1460000000)
raw cmd 0x5 -> kr 0x00000000  status 0x00000000  63000 us
smu msg 0x06 arg 0x00000000 -> kr 0x00000000 resp 0x01 (OK)
RLC booted after the enable: BOOTLOAD_STATUS 0xc000001f
smu msg 0x0d -> 0x0003ebbc / msg 0x0c -> 0x71ffffff   (running features, RedNavi's values)
```

So: **SETUP_TMR status 0, EnableAllSmuFeatures answered, RLC booted, and the running-feature mask is exactly the
one RedNavi measured**. The whole bring-up script now runs to `rc=0`.

The next wall is one step further out: the KIQ consumes its ring (`rptr 13/13`) but the `WRITE_DATA` marker never
reaches `SCRATCH_REG0` (the register at SEG1(0x2040) = 0x0C040 holds the `0xCAFEDEAD` the CPU wrote, and a CPU
write/readback works, so the register itself is fine), and `mes SET_HW_RESOURCES` / `QUERY_SCHED_STATUS` time out.
That is the GC 11.0.3 queue-descriptor question, next on the list.
### Three hardware facts that changed the port

1. **The Navi 32 sOS needs the whole bootloader chain.** KDB, SYS_DRV, **SOC_DRV, INTF_DRV, DBG_DRV, RAS_DRV**, and
   the SOS **last**. With only KDB/SYS_DRV/SOS the bootloader reports the sOS version in `C2PMSG_58` and
   `C2PMSG_81` stays 0 - the sOS never runs. (RedNavi's chain on this same card loaded all seven and saw
   `C2PMSG_81 != 0`; that is what pointed at the missing components.) `bringup_navi31.sh` now loads them for
   Navi 32 and treats `C2PMSG_81` as the only liveness signal.
2. **The RLC slices move with the blob.** `gc_11_0_3_rlc.bin` is a `rlc_firmware_header_v2_3`; its GPM/SRM/IRAM/
   DRAM/RLCP/RLCV offsets are 25344/27904/58272/124320/157600/166304 with sizes 2560/30368/66048/33280/8704/12800,
   and they tile the 179104-byte file exactly. `bringup_navi31.sh` reads them per ASIC now (`RLC_*` variables).
3. **A warm reboot does not reset the PSP.** After rebooting, `C2PMSG_81`/`C2PMSG_64` still showed the running sOS
   and the old ring, so a clean cold-start run needs a **power cycle**, not a restart.

`kmutil install --update-all` cannot rebuild the boot/system collections on this machine (no matching KDK, sealed
system volume), but `kmutil load -z -p <kext>` skips the approval check and stages the kext into
`/Library/KernelCollections/AuxiliaryKernelExtensions.kc` - no GUI click needed. The accelerator only binds after
the boot kext sets `nmxgpu-claimed` (during VM setup), so it stays idle until the bring-up reaches that point.
## Open, needs the card

- The 13 HUBPREQ0 watermarks in the plan (TODO-CAPTURE).
- A 1440p60 capture for this board (the current plan is 1080p60, the mode the capture was taken at).
- Everything in the first-light checklist above.
- Applying `patches/mesa/*` in sequence and building RADV (needs the Mesa checkout and its build deps).

#### MES API frame layout A/B (2026-11-24)

The vendored `mes_v11_api_def.h` uses `MES_MAX_HWIP_SEGMENT = 8` for the `gc_base` / `mmhub_base` /
`osssys_base` arrays while Linux 6.6 carries 6, which moves `api_status` six dwords. Both layouts were
built and tried against the live firmware: both consume the frame (rptr 0 -> 128), neither produces an
`api_status` or a completion fence, and both wedge the CP afterwards. So the frame field offsets are not
what the MES is rejecting; the port keeps the 8-segment header, matching the kernel version that
supports this card's firmware.

#### MES firmware to pipe mapping is correct (2026-11-24)

Checked the one thing that could have left the scheduler pipe running the KIQ firmware, or the other way
round. `amdgpu_mes_sw_init` picks the file by pipe:

```c
snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_mes%s.bin", ucode_prefix,
         pipe == AMDGPU_MES_SCHED_PIPE ? "_2" : "1");
```

so SCHED -> `gc_11_0_3_mes_2.bin`, KIQ -> `gc_11_0_3_mes1.bin`, and `psp_gfx_if.h` numbers them
`GFX_FW_TYPE_CP_MES = 33` (with `MES_STACK = 34`) and `GFX_FW_TYPE_CP_MES_KIQ = 81` (with
`MES_KIQ_STACK = 82`). The board's script loads exactly that: mes_2 at fwtype 33/34 and mes1 at
81/82. No swap, so the pipe/firmware pairing is ruled out.

The pc start the port writes (0xfc001400 / 0xfc000800, matching the live Linux registers) is also
right, so the remaining question is why a scheduler pipe that is active, has its firmware at the right
instruction address and consumes frames never executes a command.

### Restarted on the latest reference (2026-11-24)

Both reference projects were pulled: upstream is unchanged at 9a91901, XTX went 2f4dc68 -> e953e2a
(thread 03: VCN decode as EFI change 23, Safari WebGL/WebGPU, OpenGL and OpenCL on the GPU, Vulkan window
surface; thread 02 wrap-up with the tiled drawable as EFI change 22; PhxGpuTime, b157).

The whole navi32 diff was re-applied onto e953e2a as branch `navi32-v2` (the old branch stays as
`navi32`). One conflict, in mesboot's system-aperture block, resolved by keeping XTX's new derivation from
GCMC_VM_FB_LOCATION_BASE/TOP and carrying the GC 11.0.3 +0x10 shift through both the read and the write,
with this board's 0x00200000-0x0020ffff as the fallback instead of the XT's 20 GiB.

Both kexts and mesboot build clean on the new base, and every navi32 diagnostic survived the rebase
(mesfull, mesreg, kiqsched, KIQ_ATLINUX, MES_FENCE_MC, GART_VA_BASE, CTX0_CNTL, HIER_PT, phx-bar2-len).

Worth re-testing on this base, because the reference works on navi31 with the *minimal* MES setup
(`mes_start`: reset, per-pipe program counter, ACTIVE) while the port has been adding the full Linux
block before every queue test. Many of those registers (0x0c80d, 0x0c812, 0x0c826, 0x0c843, 0x0c89f and
friends) are live firmware state, not driver-written setup, so pre-writing them may be what stops the
scheduler pipe from executing. The next experiment is therefore bringup (minimal MES) -> gart -> kiqsched
with no mesfull at all, then the same with mesfull, on the new base.

#### The rebased baseline on the box, and the minimal-MES A/B (2026-11-24)

Installed the rebased kext and mesboot on the target (it came back on a new address, 192.168.10.82:122)
and ran the A/B the previous commit proposed:

```
after bringup (mes_start only):
  0x0c800 = 0x00000800   0x0c801 = 0   0x0c802 = 0
  0x0c807 = 0x40000000   <- CP_MES_CNTL = MES_HALT (bit 30)
kiqsched without mesfull:  rptr stays 0 (frames not consumed), CP wedges
```

So the minimal path does not work on this board for a different reason than "mesfull is harmful": the
bringup script leaves the MES **halted** and its program counter unset, and `mesfull` is what raises the
reset bits, loads the per-pipe program counter and writes ACTIVE. The proposed A/B is therefore answered:
mesfull is required, and the firmware does consume frames once it runs - it just never completes them.

Recording the state for the next round: with the rebased baseline the port behaves exactly as before the
rebase (bringup rc=0, GRBM 0x0000302c after bringup, gart fine, MES consumes the API frames, no
api_status and no completion fence, CP wedges afterwards). Nothing regressed in the rebase.

#### The MES consumes the KIQ packets exactly and creates nothing (2026-11-24)

Ran the HQD scan the previous commit added, around a real MAP_QUEUES:

```
before:  me3/pipe1 active=1  pq_base=0x7fff0a1000  rptr=13  (the KIQ the port programmed)
schedkiq sends MAP_QUEUES (7 dwords) on the KIQ
after:   me3/pipe1 active=1  pq_base=0x7fff0a1000  rptr=20  (13 + 7: the packet was consumed)
         no other ME/pipe anywhere reads as active
```

So the KIQ takes the packet byte-for-byte, and the MES creates no scheduler queue in any slot. The
earlier "the MES did not create the HQD" reading was therefore not an artefact of watching the wrong
slot - the queue really is never created.

Everything about the firmware side has now been checked against the kernel sources and the card, and all
of it is correct:

| item | value | source |
|---|---|---|
| file per pipe | SCHED -> `gc_11_0_3_mes_2.bin`, KIQ -> `gc_11_0_3_mes1.bin` | amdgpu_mes_sw_init |
| PSP fw types | 33/34 (CP_MES + stack), 81/82 (CP_MES_KIQ + stack) | amdgpu_psp ucode map, IP 13.0.10 |
| program counter | 0xfc001400 / 0xfc000800 | live Linux registers |
| MES block | 30+ registers incl. 0x0c82e/0x0c82f scratch | live Linux registers |
| queue/vmid masks | gfx 0x02, compute 0x0c, sdma 0xfc, vmid 0x0000ff00 | this card's own MES log |
| API frame | 64 dwords, type 1, opcode 0, dwsize 0x40 | mes_api_def.h |
| frame pointers | `*_gpu_mc_ptr` and MAP_QUEUES' MQD in MC, wptr/PQ_BASE in VA | driver sources |
| fence addresses | tried both VA and MC | A/B on the card |

The firmware consumes frames and even writes 0x00010000 into the `g_sch_ctx_gpu_mc_ptr` slot, but never
writes `api_status` or the completion fence, and wedges the CP when a frame targets a queue that does not
exist. What is left cannot be settled from the outside: it needs either the MES firmware ABI for this
build or a register/WB capture from a working GC 11.0.3 part.

#### The scheduler ring's queue parameters live in the MQD, and ours disagreed with Linux (2026-11-24)

Booted Ubuntu on the same box (the boot switching now works, see the amd-hackintosh repo's
docs/BOOT-SWITCHING.md) and dumped the live scheduler ring Linux runs:

```
me3/pipe0 (sched) active=1 mqd=0x83feb2d000 pq=0x7fff006ac000 rptr=2432
                 dbctl=0x60000058 pqctl=0xd830800c vmid=0
me3/pipe1 (KIQ)   active=1 mqd=0x83feb2c000 pq=0x7fff00800000 rptr=1088
                 dbctl=0x00000060 pqctl=0xd8308011
```

with the matching MQDs in VRAM (scheduler: w132 0x0be05500, w143 0x60000058, w145 0xd830800c;
KIQ: w132 0x0bec5500, w143 0x00000060, w145 0xd8308011).

Two defects followed from that:

1. The port built the scheduler ring's PQ_CONTROL by hand and landed on QUEUE_SIZE 0x11, i.e. it told the
   firmware the ring was 1 MiB while the ring is 4 KiB. It now uses Linux's 0xd830800c over a 32 KiB ring
   (SCH_RING_DW 8192, with WB/MQD/EOP moved to 0xA118000/0xA119000/0xA11A000 to make room), and Linux's
   doorbell control 0x60000058.
2. Those values also have to be in the **MQD**, not only in the HQD registers: the MES reads the queue
   parameters from the MQD, so the register read-back looked correct while the firmware kept seeing the
   old values. sched_init now patches MQD words 132/143/145 after mqd_build.

Result after both fixes: the HQD reads `pqctl=0xd830000c dbctl=0x60000058`, the MQD reads
`w145=0xd830800c`, frames are still consumed (rptr 0 -> 128 -> 256) and the completion fence and
api_status are still never written, with the CP wedging on the second submit.

One more difference to try next: after bringup this port leaves **CP_ME_CNTL = 0x01000000** while Linux
runs with 0 (gfx_v11_0_cp_gfx_enable clears the ME/PFP/CE halt bits).

#### The MES sequence now matches the driver end to end, and still does not complete (2026-11-24)

Checked the driver's MES initialisation against the port's and closed the last two gaps found:

1. `mes_v11_0_hw_init` sends **SET_HW_RSRC_1** (opcode 19) after SET_HW_RESOURCES whenever
   `sched_version >= 0x52`, with `enable_mes_info_ctx = 1` and a cleaner-shader fence address. The port
   never sent it; mesboot now does, right after SET_HW_RESOURCES.
2. Before that, the scheduler MQD's words 132/143/145 had to be the ones the live Linux ring uses
   (`0x0be05500` / `0x60000058` / `0xd830800c`) - the MES reads the MQD, not the HQD registers.

With both in place and a 32 KiB scheduler ring, the port's MES state is register-for-register and
word-for-word identical to the Linux reference:

```
HQD me3/pipe0: active=1 pq_base=0x7fff0a110000 pqctl=0xd830800c dbctl=0x60000058 (0xe0000058 read back,
                                                                          the doorbell-hit bit)
MQD w132/w138/w143/w145/w149 = 0x0be05500 / 0 / 0x60000058 / 0xd830800c / 0x00300000
CP_ME_CNTL = 0, CP_PQ_STATUS = 0x2, RLC_CP_SCHEDULERS = 0x30e8, CP_MES_CNTL = 0x0c000000
```

Three frames go out (SET_HW_RESOURCES, SET_HW_RSRC_1, QUERY_SCHED_STATUS) and all three are consumed -
rptr 0 -> 128 -> 256 -> 384 - with GRBM_STATUS staying at the idle 0x0000302c, but neither `api_status`
nor the completion fence is ever written, and the MES still creates no queue in any ME/pipe.

The only MES-side write ever observed is `0x00010000` appearing in the slot that `g_sch_ctx_gpu_mc_ptr`
points at, which says the firmware does start processing the frame and then stops.

What is left cannot be resolved from the outside: every driver-visible value now matches a working system
on the same silicon, so the remaining difference is inside the MES firmware's own state (its ABI version
for this build, or something the PSP was supposed to leave in place). Settling it needs either the MES
firmware specification for gc_11_0_3 or a register/WB capture from another working GC 11.0.3 part.

### The GPU executes PM4: the KIQ and a MEC compute queue both run (2026-11-24)

Two encodings and one register collision were all that stood between the port and real GPU execution.

**1. The WRITE_DATA control word differs per ring type.** From `gfx_v11_0_ring_emit_wreg`:

```c
case AMDGPU_RING_TYPE_GFX:   cmd = WRITE_DATA_ENGINE_SEL(1) | WR_CONFIRM; break;
case AMDGPU_RING_TYPE_KIQ:   cmd = (1 << 16);                       break;  /* no inc addr */
default:                     cmd = WR_CONFIRM;                      break;  /* compute */
```

so the KIQ needs `0x00010000` and a compute queue `0x00100000`. The port had been sending
`0x00100100` (an earlier "fix" that set DST_SEL, which was never the problem - DST_SEL 0 *is* a register
write). Measured on the card, once the page tables were right:

| queue | WRITE_DATA cmd | result |
|---|---|---|
| me3/pipe1 (KIQ) | 0x00010000 | `SCRATCH_REG0` 0xCAFEDEAD -> **0xDEADBEEF**, rptr 5/5, GRBM idle |
| me1/pipe0 (compute) | 0x00100000 | `SCRATCH_REG0` -> **0xDEADBEEF**, rptr 5/5, GRBM idle |
| me1/pipe0 | 0x00010000 | nothing lands, CP wedges |
| me2/pipe0 | 0x00100000 | queue reads inactive: that ME does not exist here |

**Indirect buffers work too**: `ibtest` puts an IB in VRAM and submits `INDIRECT_BUFFER` (size | VALID |
VMID 0) pointing at its GART-window VA; `SCRATCH_REG0` becomes **0xBEEF5678** with rptr 4. So packet
fetch, IB fetch and register write-back all work when the address is one the port mapped itself.

**2. A register collision broke the accelerator's client VM.** mesboot wrote the GART window to
`SEG0(0x1713)` / `SEG0(0x1733)`, which are 0x2973 / 0x2993. In the GC 11.0.3 shifted layout those are
**vmid 8's page-table base and window start** - and vmid 8 is the first VMID the accelerator lends to
client contexts. Every client context therefore started life with a bogus page-table base (0x7FFF00000,
a GART page number) and an empty window, which is exactly why the kext's own submissions wedged while
the port's VMID-0 queues kept working. Those three unshifted writes are now gated behind `if (!vsh)`;
afterwards vmid 8 reads CNTL 0x01fffe07, PT 0x0A400001, window 0..0xFFFFFFFFF as the kext intends.

**3. The kext now adopts a queue.** `mesboot computeq ADOPT=1` programs the MEC compute queue directly
(MQD w132/w143/w145 = 0x0be05500 / 0x60000018 / 0xf030890a, from the live Linux compute queue), proves
it executes, then hands it to the kext: `adopt -> kr 0x0`. `debug.phxbo_vram_selftest` now gets past the
`gpuReady()` check it used to fail (it returns -2, a vramFill failure, instead of -1), so the kext sees a
live queue.

**Still open:** the kext's `submit_test` wedges the CP. The client context is correct now, but the
client's root table at VRAM 0xA400000 reads **empty** (`debug.phxpt_stat` reports "shadow on", pool chunks
1), so the IB's client VA has no translation. Next: why the client mappings do not reach VRAM - the
page-table shadow write-through, the chunk pool, or the map path itself.

### Fences work: the missing EOP registers (2026-11-24)

`ibtest3` isolated the last blocker: a RELEASE_MEM on its own wedged the CP and wrote no fence, while the
IB in front of it executed fine. The cause was in `hqd_program`, which programmed the MQD base, ACTIVE,
VMID, PQ base/rptr/report, wptr poll, doorbell and PQ control - everything `kiq_init_register` writes
**except** the EOP buffer and the persistent state:

```c
WREG32(regCP_HQD_EOP_BASE_ADDR,    mqd->cp_hqd_eop_base_addr_lo);
WREG32(regCP_HQD_EOP_BASE_ADDR_HI, mqd->cp_hqd_eop_base_addr_hi);
WREG32(regCP_HQD_EOP_CONTROL,      mqd->cp_hqd_eop_control);
WREG32(regCP_HQD_PERSISTENT_STATE, mqd->cp_hqd_persistent_state);
```

Without an EOP buffer, the RELEASE_MEM (which carries an EOP event) has nowhere to report and the CP
hangs. hqd_program now writes `CP_HQD_PERSISTENT_STATE` (0x1fad), `CP_HQD_EOP_BASE_ADDR`
(0x1fce/0x1fcf) and `CP_HQD_EOP_CONTROL` (0x1fd0: EOP_SIZE 8, process EOP and EOPIB) from the MQD it
was handed, and the same run gives:

```
ibtest3: fence=0x00000001/00000000 (want 1/0) rptr=8 active=1 -> FENCE WRITTEN
GRBM: 0x0000302c   (idle, no wedge)
```

So the port now has, on real hardware: PM4 execution on the KIQ and on a MEC compute queue, indirect
buffer fetch, and the RELEASE_MEM fence write-back that every submission ends with.

The kext's `submit_test` gets further as well - the fence retires (`waitseq pending 0 retired 1`) - but
its IB writes to a client BO at a client VA and the target still reads 0, so the remaining question is the
client mapping (VMID 8), not the queue.

### The accelerator's client VMID is the last wall (2026-11-24)

With fences working, the port's own paths all behave:

| test | result |
|---|---|
| PM4 on the KIQ (VMID 0) | SCRATCH_REG0 written, GRBM idle |
| PM4 on a MEC compute queue (VMID 0) | written, GRBM idle |
| IB in VRAM (GART window VA) | executed |
| IB in **system memory** (`ibsys`, PTE SYSTEM\|SNOOPED) | executed - **the GPU does read system memory** |
| RELEASE_MEM fence | `fence=1`, GRBM idle |

The kext's submission is the one that fails: `submit_reg` (an IB that only writes SCRATCH_REG0, so no
memory destination is involved) gives `waitseq pending 1 retired 0`, SCRATCH_REG0 unchanged, GRBM
0xa800302c and **no GCVM fault recorded**. The ring's own packets are in place:

```
0xA140000: c0033700 00100000 0000c040 00000000 deadbeef   <- the port's ring test (executes)
0xA140014: c0023f00 00000000 00000001 08800005           <- INDIRECT_BUFFER -> VA 0x100000000, VMID 8
0xA140024: c0064900 06603514 40000000 0a150000 00000080 00000001 00000000
```

and the client context is correct as far as can be read back: vmid 8 CNTL 0x01fffe07, page-table base
0x0A400001, window [0, 0xFFFFFFFFF pages], and the walk reaches a leaf PTE
(`0x0E8FDA5077` = system page 0xE8FDA5000 with VALID\|SYSTEM\|SNOOPED\|EXEC\|RD\|WR) for VA
0x100000000.

So the CP stops at exactly that IB fetch: everything in front of it (the port's own packets) ran, nothing
after it did, and the fault registers stay zero. The queue's `PQ_CONTROL` (0xf030890a) has
NO_UPDATE_RPTR set, so `CP_HQD_PQ_RPTR` reads 0 and cannot be used to see how far it got - the scratch
register and the fence are the only readbacks that matter.

Next candidates, in order: (a) the PDP/PD entry flags of a **VRAM-resident 4-level** table for a client
VMID - Linux allocates client page tables in system memory and marks the PDEs SYSTEM|SNOOPED, while the
kext's tables are in VRAM with VALID only; (b) the IB packet's VMID field against the queue's own VMID 0;
(c) making the client BOs VRAM-backed, which removes the 4-level walk from the IB path entirely
(the XTX's PhxVramPool already does this).

### The depth-3 (client) page-table walk does not translate at all (2026-11-24)

With fences working, the accelerator's own paths all execute. The client VMID does not, and the failure is
narrower than the accelerator: **the GCVM 4-level walk does not translate even for VMID 0**, which is the
port's own context.

Tested by pointing context 0 at a 4-level table the port builds itself (`HIER_PT=1`), with the same ring
and IB that the flat table executes:

| context 0 page table | CNTL | result |
|---|---|---|
| flat depth 0 (GART) | 0x01fffe01 | IB executes, SCRATCH_REG0 written |
| 4-level depth 3, table in VRAM | 0x01fffe07 | nothing fetched, no fault recorded |
| 4-level depth 3, table in system memory (`HIER_SYS`) | 0x01fffe07 | nothing fetched |
| depth 2 with the walk started at the PDP (`PT_ADD=0x1000`) | 0x01fffe05 | nothing fetched, invalidation ack 0x0 |
| depth 1 with the walk started at the PD (`PT_ADD=0x2000`) | 0x01fffe03 | nothing fetched, invalidation ack 0x0 |

The table itself is structurally correct - the accelerator's own client table walks cleanly from VRAM
(PML4E 0x0A401001 -> PDPE 0x0A402001 -> PDE 0x0A403001 -> PTE 0x0E8FDA5077 for VA 0x100000000) - and the
IB target does not matter: an IB that writes nothing but SCRATCH_REG0 fails identically with a GTT BO and
with a VRAM BO (`submit_vram`), so the PTE's system bit and its physical address are both ruled out.

Two further facts point at the GCVM configuration rather than the tables:

* the invalidation engine acknowledges at depth 3 but **not** at depth 1 or 2, so the context control
  register is only half understood;
* `GCVM_L2_CNTL` as the port addresses it (SEG0(0x15bc) + 0x10 = 0x282c) reads **0** - the GCVM L2 cache is
  never configured, while `mmhub_v3_0_2_init` sets ENABLE_L2_CACHE, ENABLE_DEFAULT_PAGE_OUT_TO_SYSTEM_MEMORY
  and CONTEXT1_IDENTITY_ACCESS_MODE before any context is enabled. Writing that offset does not read back
  what was written and it breaks the working flat configuration, so 0x282c is **not** the GCVM L2 block in
  the GC alias, and the experiment is now gated behind `L2INIT=1`.

`mesboot scan <lo> <hi> <value> [mask]` was added for exactly this: it reads a register range in one
process and prints the indices holding a value. It confirms context 0's page-table base is only at 0x2963
and its window start only at 0x2983, so the GCVM **context** block's +0x10 shift is right; the L2 block's
is not.

Next: boot Linux, scan its register space for the page-table address of a live depth-3 context, and take
the GCVM register layout from that instead of from a segment base carried over from an older generation.

### Reprogramming an active HQD wedges the CP - stop the queue first (2026-11-24)

The inconsistency that made every earlier experiment look flaky had one cause: the first HQD program after
bring-up works, a second program of the same HQD wedges the CP. Measured on one boot, same binary:

```
ibtest #1   computeq: dbctl=0x60000018   ibtest: scratch0=0xbeef5678  IB EXECUTES
ibtest #2   computeq: dbctl=0x20000018   ibtest: scratch0=0xcafedead  no IB execution, GRBM 0xa800302c
```

The doorbell control read back 0x20000018 instead of 0x60000018 on the second program, i.e. the queue was
still live and the write was consumed by it. `hqd_program` now calls `hqd_stop` first: write
`CP_HQD_DEQUEUE_REQUEST` (0x1fc1) = 2, poll `CP_HQD_ACTIVE` (0x1fab) bit 0 clear, force ACTIVE = 0 if it
does not, and clear the request. With that, three consecutive programs all execute:

```
ibtest #1   IB EXECUTES
ibtest #2   hqd_stop: me1/pipe0 active now 0   IB EXECUTES
ibtest #3   hqd_stop: me1/pipe0 active now 0   IB EXECUTES
GRBM: 0x0000302c
```

`QSTOP=0` restores the old behaviour for A/B. This also matters for the accelerator: a client submission
that re-programs the adopted queue while it is running wedges exactly like this.

### Where the client path stands after the queue-stop fix (2026-11-24)

With `hqd_stop` in place the queue machinery is reliable, and the accelerator's failure is now isolated to
one thing. Measured on one boot:

```
computeq: ring test scratch0=0xdeadbeef -> EXECUTES
computeq: adopt -> kr 0x0
submit  : waitseq pending 1 retired 0, SCRATCH_REG0 unchanged, GRBM 0xa800302c
ibtest  : scratch0=0xbeef5678 -> IB EXECUTES, GRBM back to 0x0000302c
```

So the kext's submission wedges the CP **recoverably**, and the only difference from the port's own
IB + RELEASE_MEM submission that works (`ibtest2`) is that the kext's IB is fetched through a **depth-3**
page table at VMID 8, while every working path uses the flat depth-0 table. Reprogramming the queue is no
longer a candidate: three consecutive programs execute now.

**The plan that follows from this**: give client VMIDs a flat depth-0 table, like context 0 already uses,
and keep client VAs low so the table stays small.

* depth 0 with the walk starting at the PTE: table size = kClientVaTop / 4096 * 8 bytes;
* a 256 MiB client window costs 512 KiB per context - with `kChunks * kChunkPages` = 4096 tables of 4 KiB
  (16 MiB, `PHX_VRAM_PT_SIZE` 0x4C00000) that is ~32 concurrent contexts, which is plenty for bring-up;
* `PHX_AMDGPU_GEM_VA` allocation must then start well below 0x100000000 (the ABI's test VA is 0x100000000,
  which would need an 8 MiB table on its own).

The alternative - finding the real GCVM L2 register layout and making depth 3 work - is still worth one
pass, because it is what Linux does and it is what the XTX's working stack uses.

### A client VMID translates with a flat table - depth 3 is the whole problem (2026-11-24)

Two experiments, same boot, same queue, differing only in which page table the fetch goes through:

```
ctx8: flat 512 MiB table at VRAM 0xA300000, CNTL=0x01fffe01 (depth 0), PT=0x0a300001,
      window 0..0x1ffff, invalidated for VMID 8
ib8 : queue at VMID 0 (ring in ctx 0's GART window), INDIRECT_BUFFER with VMID 8 -> VA 0xA200000
      -> scratch0=0x0d15ea5e, rptr 4, GRBM 0x0000302c   **CTX-8 TRANSLATES**
```

against the same context 8 with a **4-level** table (the accelerator's layout), where the fetch consumes
nothing and the CP wedges. So:

* the GCVM walk works fine for a **client** VMID (8) when the table is **flat, depth 0**;
* it does not translate at **depth 3**, which is exactly what the accelerator builds for its clients;
* and the rest of the client path was already correct - the IB's VMID field, the client window, the
  invalidation, the queue.

The port's own queue must still live at VMID 0 (`ctx8` with the queue at VMID 8 fetched nothing, so the CP
takes its ring from the submitting context); a queue at VMID 0 whose *IB* carries the client VMID is
exactly the shape the accelerator already uses, and it works.

**So the accelerator fix is: build client page tables flat (depth 0), like context 0's GART, and keep the
client VA inside a bounded window.** A 512 MiB window costs 1 MiB per context (16 of the 256 pool chunks);
1 GiB costs 2 MiB. The ABI's test VA 0x100000000 would need 8 MiB on its own, so either the window grows or
client VAs move low.

New mesboot subcommands: `ctx8` (client VMID with a flat table, queue also at that VMID) and `ib8` (client
VMID only on the IB, which is the configuration that works). `gLowVa` makes the port's own addresses plain
VRAM offsets so a small flat table can cover them.

## The accelerator submits to the GPU (2026-11-24)

`submit_test` now does what it was written to do, on real hardware, reproducibly:

```
bringup rc=0   mesfull   gart   computeq ADOPT=1 (ring test EXECUTES, clean handoff, adopt kr 0x0)
run 1: waitseq pending 0 retired 1  target = 0x5eed0001
run 2: waitseq pending 0 retired 2  target = 0x5eed0001
run 3: waitseq pending 0 retired 3  target = 0x5eed0001
GRBM: 0x0000302c
```

Three changes got there, in the order they were found:

**1. Flat (depth 0) client page tables.** The GCVM four-level walk does not translate on this die; the same
client VMID does translate a flat table (measured both ways, see the entry above). The accelerator now gives
each context one contiguous 2 MiB table indexed by `va>>12` - like context 0's GART - at
`PHX_VRAM_PT_OFF + PHX_VRAM_FLAT_OFF` (VRAM 0xD800000), eight of them, one per client VMID, taken when a
context is acquired and cleared and handed back when its work has retired. Client VAs must stay inside
`PHX_CLIENT_VA_SPAN` (1 GiB); `submit_test`'s IB and target moved from 0x100000000/0x100010000 to
0x10000000/0x10010000.

**2. A clean ring at adoption.** `mesboot cs mc ADOPT=1` ran its ring test on the queue and then handed it
over: the CP's rptr had advanced while the accelerator's wptr bookkeeping starts at 0, so the first client
submission overwrote the packets the CP had already consumed and then fetched from the middle of its own
sequence. That was the intermittent failure - one run out of three. The adopt path now stops the queue
(`hqd_stop`), clears the ring, re-arms it at rptr = wptr = 0 and *then* hands it over.

**3. VRAM-backed client BOs.** With a GTT BO the submission executed (the IB's register write landed) but the
fence never retired; with `PHX_AMDGPU_GEM_DOMAIN_VRAM` the whole path completes. `submit_test_vram` is the
same test with the domain switched. The ring's own BOs and the client's are both VRAM, which is what the
XTX's VRAM-backed BO pool already does.

What this means for the stack: the MES is no longer on the critical path. The accelerator reaches the GPU
through a queue at VMID 0 plus client VMIDs on its IBs, exactly as the XTX does, so RADV/NVMTL can be wired
to it while the MES work continues separately.

Next: raise the flat window and give the UMD a VA allocator that stays inside it (1 GiB is enough for a
bring-up, not for RADV's full heap), then run the XTX's RADV on top and see what it asks for first.

### RADV reaches the accelerator, and what stops it (2026-11-24)

Installed the prebuilt `build/libvulkan_radeon.dylib` at `/Library/GPUBundles/nvmtl/` and ran `tools/vkbench`
against it directly (`VKBENCH_DRIVER=...`). The native backend handshake works on the navi32 port:

```
radv-darwin: native kext: ABI 1.12, build 160, VMID 8, max 64 IBs/submit,
             GB_ADDR_CONFIG 0x00000444, VRAM 256 MiB visible + 6144 MiB hi,
             GTT cap 2048 MiB, profile navi32_xt
device: AMD Radeon RX 7800 XT (RADV NAVI32) (driver 26.2.99)
VK FAILED -2: vkCreateDevice(pd, &dci, NULL, &dev)
```

So RADV enumerates the device from this kext and fails in `vkCreateDevice` with
VK_ERROR_OUT_OF_DEVICE_MEMORY. The kext log says exactly where:

```
PhxBringup: GPU map FAILED: pid 794 vkbench op 1 flags 0xe va 0xffff800100000000 size 0x1000 -> 0xe00002bd
```

**RADV places its BOs at `0xffff800100000000`** - above bit 39 - and a flat depth-0 table can only index
`va >> 12` over the low 27 bits of that, so no flat window can ever cover it. Raising the window from 1 GiB
to 4 GiB (`PHX_VRAM_FLAT_OFF` 0x6000000, `PHX_FLAT_BYTES` 0x800000, six tables) changes nothing for RADV;
it stays useful only for the port's own tests.

**Therefore RADV needs the four-level walk**, which is what the XTX uses on Navi 31 (the kext's own comment
about "an upper-half IB (bit 47, where RADV puts them)" is a trace of that working).

### What the register probe did establish

The MMHUB VM block's base is **0x1A080**, derived from two constants the port already had:
`kMmCtx0BaseLo = 0x1A7AB` against Linux's `regMMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32 = 0x072B`, and
confirmed by `kMmL2Cntl5 = 0x1A71E` against `regMMVM_L2_CNTL5 = 0x069E`. Its live contents:

```
0x1A700 MMVM_L2_CNTL   0x00080602   ENABLE_L2_CACHE (bit 0) = 0, CONTEXT1_IDENTITY_ACCESS_MODE (bit 19) = 1
0x1A702 MMVM_L2_CNTL3  0x80100007   the reset default
0x1A718 MMVM_L2_CNTL4  0x000000c1   VMC_TAP_PDE_REQUEST_PHYSICAL and VMC_TAP_PTE_REQUEST_PHYSICAL = 1
0x1A71E MMVM_L2_CNTL5  0x00003fe0
0x1A740 MMVM_CONTEXT0_CNTL 0x01fffe00   ENABLE_CONTEXT (bit 0) = 0
0x028F8 GCVM_CONTEXT0_CNTL 0x01fffe01   ENABLE_CONTEXT = 1, written by the port
```

so the GC and MMHUB VM blocks are **separate instances** (their context-0 controls differ), and the GCVM L2
block is not where the port's alias arithmetic puts it: a scan of `0x1200..0x2C00` finds no register equal
to the distinctive `0x80100007` CNTL3 default, and `GCVM_L2_CNTL` at the derived `0x282C` reads 0.

Linux clears the two `VMC_TAP_*_REQUEST_PHYSICAL` bits (bit 6, bit 7) before enabling any context - the
MMHUB instance here has both set - which remains the best hypothesis for the depth-3 failure: with the tap
enabled the walker issues *virtual* requests for PDE/PTE fetches. Finding the GC instance's L2_CNTL4 is the
next experiment.

> **Hazard:** a blind `mesboot scan` over `0x1000..0x40000` hung the machine (unimplemented registers in the
> middle of that range). Keep scans inside the blocks the port already reads, and treat a wide scan as a
> cold-boot experiment.

### Depth 3: what has been eliminated, and what is left (2026-11-24)

The four-level walk does not translate in any configuration that has been built so far, and the walker
records no fault (`GCVM_L2_PROTECTION_FAULT_STATUS` at 0x28C4 stays 0 - CONTEXT0_CNTL 0x28F8 minus the MMHUB
delta 0x34). Measured, all with the queue at VMID 0 and only the IB asking for VMID 8, so the queue is not
a variable:

| context 8 page table | result |
|---|---|
| flat depth 0, VRAM | translates (control) |
| 4-level depth 3, VRAM, VALID only | no translation |
| 4-level depth 3, VRAM, + AMDGPU_PDE_PTE (bit 54) | no translation |
| 4-level depth 3, system memory, SYSTEM\|SNOOPED + bit 54 | no translation |
| depth 2 (root at the PDP) and depth 1 (root at the PD) | no translation, invalidation ack 0 |

Also eliminated: the context window (0..0x1FFFF pages), the invalidation (comprehensive: L2 PTEs, PDE0/1/2,
L1 PTEs, per-VMID; the engine acks), the table contents (dumped word for word: PML4E[0] 0x0040000005001001,
PDPE[0] 0x0040000005002001, PDE[0] 0x0040000005003001, PTE[0] 0x71), the MMHUB VMC tap bits (cleared at
0x1A718, no effect) and ENABLE_L2_CACHE (set at 0x1A700, no effect).

What amdgpu adds to a PDE, from the source: `amdgpu_vm_pte_update_flags` ORs `AMDGPU_PDE_PTE_FLAG(adev)` -
which is `AMDGPU_PDE_PTE`, **bit 54**, for anything below GC 12 - on every non-leaf level and then calls
`amdgpu_gmc_get_vm_pde`. The port was writing VALID only; it now writes bit 54 as well, which was the last
source-backed difference that could be applied blind.

**Next step is a capture, not another guess.** The plan: boot Linux, find where its GCVM context registers
really live in the `amdgpu_regs` index space (reads at the port's indices 0x28F8/0x2963 return 0, so the
debugfs index space differs from the port's absolute one), take the PAGE_TABLE_BASE of a live client
context, walk that table in `/sys/kernel/debug/dri/.../amdgpu_vram` and print every entry's flags, then
diff that against the table the port builds. That yields the missing bits or the missing global enable
instead of another hypothesis.

> The machine had to be power-cycled twice during this round: once after a blind register scan over
> `0x1000..0x40000` (now refused by `reg_scan`), and once after a `phxnext reboot` into Ubuntu that never
> came back up.

## The CP's IB fetch goes through the MMHUB instance - and four-level tables work (2026-11-24)

This is the answer to the depth-3 mystery, and it also explains why Linux's GC-segment context registers
read 0.

The GPU has **two instances** of the same VM block:

| instance | base | port's registers | Linux |
|---|---|---|---|
| GC segment | 0x1260 + reg | `GCVM_CONTEXT0_CNTL` at 0x28E8+0x10, PT at 0x2953+0x10 | **all 0** - never used |
| MMHUB | 0x1A080 | `kMmCtx0Cntl` 0x1A740, `kMmCtx0BaseLo` 0x1A7AB, start 0x1A7CB, end 0x1A7EB | **the only one Linux programs** |

Read live on the card: `0x1A740 = 0x01fffe01`, `0x1A700 = 0x00080e01`, `0x1A718 = 0x00000001` on Linux,
against all-zero GC-segment context registers. The port only ever programmed the GC-segment instance, and
the MMHUB mirror it already had was gated behind `debug.phxmmvm` (`gMmvmOn`, default 0).

Measured, with the GC instance's context 8 deliberately disabled (CNTL = 0, PT = 0) so only the MMHUB table
could translate, an IB at a VA the flat table had never touched:

```
ib8h: GC ctx8 disabled (CNTL=0x00000000 PT=0x00000000)
ib8h: 4-level table at VRAM 0x5000000 for ctx8, CNTL=0x01fffe07 PT=0x05000001   (MMHUB instance)
ib8h: scratch0=0x0badf00d -> 4-LEVEL CLIENT WALK WORKS
GRBM: 0x0000302c
```

**So the four-level walk was never broken: it was being programmed into the instance the CP does not use for
IB fetches.** (The GC-segment instance does translate a *flat* table - that is what made the earlier flat
experiments work and hid the mistake.)

Two methodological notes, both learned the hard way:

* an earlier "4-LEVEL CLIENT WALK WORKS" was a **false positive**: the flat run had already mapped the same
  VA, so a cached translation served the fetch. The real test must use a VA that was never mapped before
  (`ib8h` now uses 0xA280000, and with `MMHUB=1` it also disables the GC instance first);
* boot state matters: after a wedged CP, `CP_HQD_PQ_CONTROL` loses bit 15 (`0xf030890a` -> `0xf030090a`) and
  nothing executes until a cold boot. Every claim above is from a fresh boot.

The accelerator now builds normal 4-level client tables again and programs **both** instances, with the
MMHUB one done unconditionally. `submit_test_vram` still reaches `0x5eed0001` with a fence retirement on the
first try, so the 4-level path is at least as good as the flat one was - and it is the one RADV needs, since
RADV places BOs at 0xffff800100000000, above what a flat table can index.

### RADV on the 4-level path (same day)

With that in place RADV gets past `vkCreateDevice` - it creates two client contexts (VMID 8 and 9, both
4-level, `CNTL=0x01fffe07` on the MMHUB instance) - and then hangs in the compute loop with GRBM at
0xa800302c. So the next frontier is no longer memory translation but the content of RADV's submission: which
packet it sends that our queue cannot retire.

### RADV's first submission: translation is fine, the IB content is not (2026-11-24)

With the MMHUB instance in use, RADV gets past `vkCreateDevice`, binds context 8 (MMHUB
`CNTL=0x01fffe07`, `PT=0x0A400001`) and submits. The ring the accelerator builds shows exactly what it
sent - two IBs and a fence, three times over:

```
C0023F00 00064000 FFFF8001 08800038   INDIRECT_BUFFER -> 0xFFFF800100064000, 0x38 dwords, VALID|VMID 8
C0023F00 00001000 FFFF8001 08800058   INDIRECT_BUFFER -> 0xFFFF800100001000, 0x58 dwords, VALID|VMID 8
C0064900 06603514 40000000 0A150000 00000080 00000001   RELEASE_MEM, fence 1
```

and the page walk for that first IB is complete in VID 8's table:

```
PML4[256] @ 0xA400800 = 0x0A401001
PDP[4]    @ 0xA401020 = 0x0A402001
PD[0]     @ 0xA402000 = 0x0A403001
PT[100]   @ 0xA403320 = 0x0E8C0C2077   VALID|SYSTEM|SNOOPED|EXEC|RD|WR
```

So the IB address translates, the kext logged no failed mapping, and the CP still wedges (GRBM 0xa800302c).
The remaining suspect is **the content of RADV's IB**, which the port has never inspected. It lives in a GTT
BO (the PTE has SYSTEM set), so it cannot be read through the VRAM window - the accelerator needs a debug hook
that dumps the last IB's dwords, or the submission has to be made with VRAM BOs (`debug.phxbo_vram=1`) so the
IB is readable from the host.

That is the next experiment, and it is a much better place to be than translation: the device is created, the
contexts are bound, the tables translate, and a submission reaches the ring.

### RADV's IBs all translate; the wedge is in their content (2026-11-24)

`phxdebug=1` in the boot-args turns on the accelerator's existing IB logger (`PhxIB <seq>.<ib> +off: dwords`,
first 96 dwords of each of the first four submissions), so RADV's command buffers can finally be read
without VRAM BOs (`debug.phxbo_vram` still refuses: its self-test fails first).

RADV's submission is a chain of ordinary GFX11 packets - INDIRECT_BUFFER to a nested IB, RELEASE_MEM,
ACQUIRE_MEM, SET_UCONFIG_REG (GB_ADDR_CONFIG), SET_SH_REG, DMA_DATA, DISPATCH_DIRECT (0x15 with 8x8x1 and
0x2045), RELEASE_MEM - which is what a compute submission should look like.

Every address it names translates in context 8's table (root 0x0A400000):

```
PT[0x10]  (VA 0xFFFF800100001000, IB 1.1)     = 0x0E83D7077    mapped
PT[0x15]  (VA 0xFFFF800100015000, nested IB) = 0x0E9178F077   mapped
PT[0x64]  (VA 0xFFFF800100064000, IB 1.0)    = 0x0E91AE8077   mapped
```

so the earlier "unmapped page" note was a mistake in my own index arithmetic (I read PT[256] instead of
PT[100]); the mapping is complete and there is no fault. The wedge is therefore **in the content of RADV's
IB**, and the next step is to bisect it: replay the logged dwords from a VRAM IB on the port's own queue,
truncating until the offending packet is found. The nested IB's own dwords are not logged yet, so the logger
has to follow one level of INDIRECT_BUFFER first.

### RADV: the CP dies inside the nested IB (2026-11-24)

Two experiments narrow it down further.

**1. `PACKET3_CLEAR_STATE` is not legal on a MEC queue.** The clear-state preamble amdgpu always runs before
a queue is used was emitted as

```
C0004A00 20000000   PACKET3_PREAMBLE_CNTL, PREAMBLE_BEGIN_CLEAR_STATE
C0001200 00000000   PACKET3_CLEAR_STATE
C0004A00 30000000   PACKET3_PREAMBLE_CNTL, PREAMBLE_END_CLEAR_STATE
```

and it wedged the queue on its own (`preamble: clear-state fence=0`). So that was not RADV's problem, and the
preamble is now behind `PREAMBLE=1` (off by default).

**2. The CP consumes exactly one packet.** With `NO_UPDATE_RPTR` cleared (`RTUPDATE=1`, PQ_CONTROL
0xd030890a) the queue reports how far it got after RADV's submission hangs:

```
CP_HQD_PQ_RPTR (0x3213)  = 4        <- one 4-dword INDIRECT_BUFFER packet
PQ_CONTROL    (0x321A)  = 0xd030090a
ACTIVE        (0x320B)  = 1
GRBM_STATUS             = 0xa800302c
```

So the CP fetched the ring's first packet - the `INDIRECT_BUFFER` to 0xFFFF800100064000 - and died while
executing that IB. That IB's own first packet is an `INDIRECT_BUFFER` to **0xFFFF800100015000 (0xd8 dwords)**,
whose content has never been logged: the accelerator's logger only records the IBs the client hands it, not
the ones those IBs point at.

**Next step: follow one level of `INDIRECT_BUFFER` in the accelerator's IB logger.** It already has the BO
lookup (`maps[]` -> `Bo::kva`) it needs; it just has to scan the first IB's dwords for packet opcode 0x3F and
log that target range too. Everything else about RADV's submission is now known-good: the ring packets, the
translation of every address, the queue, and the fence that follows.

### The nested IB is not in the accelerator's map bookkeeping (2026-11-24)

The IB logger now follows one level of `INDIRECT_BUFFER` (`PhxIBn`) and reports when the target has no
mapping. Every one of RADV's submissions says the same thing:

```
PhxIBn 1.0 -> va ffff800100015000 dw 216 NO MAP ENTRY
PhxIBn 2.0 -> va ffff800100015000 dw 216 NO MAP ENTRY
PhxIBn 3.0 -> va ffff800100015000 dw 216 NO MAP ENTRY
```

The ring's first `INDIRECT_BUFFER` (0xFFFF800100064000) *is* mapped - the logger found its BO and read its
dwords - but the IB it points at (0xFFFF800100015000, 216 dwords) has no entry in the accelerator's `maps[]`.
That is the same address the CP dies on: with `RTUPDATE=1` the queue's `CP_HQD_PQ_RPTR` stops at 4, exactly
the one 4-dword IB packet, and nothing after it runs.

Two readings, and the next experiment separates them:

1. the VA was never mapped by this context at all, so the fetch cannot translate and the CP wedges silently
   (the behaviour already seen for unmapped fetches);
2. the PTE is there but the map bookkeeping lost the entry (an unmap/remap reuse), in which case the fetch
   works and the IB's *content* is what hangs.

The page walk that would settle it has to read the table through the accelerator, which only serves reads
inside the 256 MiB BAR0 window (`vram read error 0xe00002c2` above it) - and the table for this context sits
outside it, so the walk has to be done by the accelerator, not by `phxctl`.

Practical next step: log every GEMVA map and unmap with its VA and size (`mapComplain` already exists for
refusals) and diff that list against the IB addresses RADV submits. If RADV maps 0xFFFF800100015000 and the
accelerator does not record it, the map path has a bug; if RADV never maps it, the submission itself is
pointing at unmapped memory and the fix belongs on the client side.

### Found it: `SET_UCONFIG_REG` wedges a MEC compute queue (2026-11-24)

The nested IB's content is state programming - `SET_SH_REG` after `SET_SH_REG` - plus one packet that does not
belong on a compute queue:

```
PhxIBn 1.0 +10: ... c0047900 00000444 12355123 0001544d 80000020 ...
                                    ^ SET_UCONFIG_REG, register 0x444 (GB_ADDR_CONFIG)
```

Tested directly on the port's own queue with a five-dword IB holding exactly that packet:

```
uconfig: SET_UCONFIG_REG(0x444) on a MEC queue -> fence=0 WEDGED
GRBM: 0xa800302c
```

**So RADV's stream is being executed by a queue that cannot run it.** UConfig registers are graphics-level;
a MEC compute queue cannot take them. RADV emits them in its gfx path, and the accelerator only has the
compute queue ready (`gq[1].ready` is false), so `gpuSubmit` falls back and runs a gfx stream on the compute
queue. That single fact explains every symptom: the ring packet is fine, every address translates, the IB is
fetched - and the CP dies on the first uconfig write inside it.

The fix is therefore not in the client path at all: **the accelerator has to bring up its gfx queue** so that
gfx submissions (which is what RADV's first submissions are, even for a compute-only workload - it programs
GB_ADDR_CONFIG and the gfx state before the compute dispatch) land on a queue that understands them. That is
what the XTX has: `gq[1]` ready alongside `gq[0]`.

### The gfx queue lives in a different register block (2026-11-24)

RADV's gfx stream cannot run on the MEC compute queue (the `SET_UCONFIG_REG` result above), so the accelerator
needs its gfx queue. Programming ME0/pipe0 through the same registers the compute queue uses does nothing:

```
gfxq: active=0 pq_base=0x0 pqctl=0x0 dbctl=0x0      <- writes to CP_HQD_* at ME0/pipe0 do not stick
```

The reason is two separate queue interfaces:

| queue | registers | ring base |
|---|---|---|
| MEC compute | `CP_HQD_ACTIVE` 0x1FAB, `CP_HQD_PQ_BASE` 0x1FB1, `CP_HQD_PQ_CONTROL` 0x1FBA ... | in the register |
| gfx | `CP_GFX_HQD_ACTIVE` **0x1E80**, `CP_GFX_HQD_VMID` 0x1E81, `CP_GFX_HQD_QUANTUM` 0x1E85, `CP_GFX_HQD_BASE` **0x1E86** (the MQD in memory), `CP_GFX_HQD_RPTR_ADDR` 0x1E89, `CP_GFX_HQD_OFFSET` 0x1E8E, `CP_GFX_HQD_CNTL` **0x1E8F** | **inside the MQD** |

The port already reads the first two of these in its MES gfx test (`rd(SEG0(0x1e80))`, `rd(SEG0(0x1e86))`),
which is how the block was found. amdgpu's own defaults for the block are recorded in `gfx_v11_0.c`:

```
regCP_GFX_HQD_VMID_DEFAULT            0x00000000
regCP_GFX_HQD_QUEUE_PRIORITY_DEFAULT  0x00000000
regCP_GFX_HQD_QUANTUM_DEFAULT         0x00000a01
regCP_GFX_HQD_CNTL_DEFAULT            0x00a00000
```

with `CP_GFX_HQD_CNTL` carrying RB_BUFSZ (bits 5:0), RB_BLKSZ (bits 13:8), MIN_AVAILSZ (bits 21:20),
MIN_IB_AVAILSZ (bits 23:22), CACHE_POLICY (bits 25:24), RB_NO_UPDATE (bit 27) and RB_EXE (bit 28), and
`CP_GFX_HQD_OFFSET` carrying RB_OFFSET in bits 31:0 with DISABLE_RB_OFFSET at bit 31.

**So the gfx queue is programmed by writing the MQD (which holds the ring base) and then
`CP_GFX_HQD_BASE` = that MQD's MC address, VMID, QUANTUM, CNTL, RPTR_ADDR and ACTIVE = 1** - not by the
`CP_HQD_*` registers the compute queue uses. That is the next piece of work, and with it RADV's gfx stream
lands on a queue that can execute it.

### The gfx queue's MQD is a different structure (2026-11-24)

`struct v11_gfx_mqd` lives in `drivers/gpu/drm/amd/include/v11_structs.h` (not under `amdgpu/`, which is why it
was hard to find). Its field layout, with the word offsets that matter:

```
128 cp_mqd_base_addr        129 cp_mqd_base_addr_hi
130 cp_gfx_hqd_active       131 cp_gfx_hqd_vmid
134 cp_gfx_hqd_queue_priority   135 cp_gfx_hqd_quantum
136 cp_gfx_hqd_base (ring base >> 8)   137 cp_gfx_hqd_base_hi
138 cp_gfx_hqd_rptr          139 cp_gfx_hqd_rptr_addr   140 _hi
141 cp_rb_wptr_poll_addr_lo  142 _hi
143 cp_rb_doorbell_control   144 cp_gfx_hqd_offset
145 cp_gfx_hqd_cntl          149 cp_gfx_hqd_wptr        150 _hi
156 cp_gfx_hqd_mapped
```

Words 128-137 overlap the MEC MQD the port already builds, which is why the queue went ACTIVE at all, but
**word 145 is `CP_GFX_HQD_CNTL`, not the MEC `PQ_CONTROL`**: RB_BUFSZ in bits 5:0, RB_BLKSZ in bits 13:8,
RB_NON_PRIV at bit 15, on top of the `0x00a00000` default (amdgpu's `gfx_v11_0_gfx_mqd_init` computes
`rb_bufsz = order_base_2(queue_size / 4) - 1` and `RB_BLKSZ = rb_bufsz - 2`). For an 8 KiB ring that is
`0x00a0880a`. `mesboot me0q` now builds that MQD and programs the `CP_GFX_HQD_*` registers from it.

The queue comes up ACTIVE with the right CNTL and base, but still fetches nothing. What `gfx_v11_0.c` also
does that the port does not:

* `gfx_v11_0_set_doorbell_range` - `CP_MEC_DOORBELL_RANGE_LOWER/UPPER` (and the gfx equivalent) so the CP
  knows which doorbell range its queues may use;
* the MQD's `cp_gfx_hqd_mapped` (word 156);
* the ME/PFP/CE enable sequence around it (`CP_ME_CNTL` bits), and the RLC's gfx power state.

Those are the next three things to try, in that order, with the port's existing `mesboot me0q` as the harness.

### The gfx ring is a legacy CP_RB0 ring, not an HQD (2026-11-24)

A correction to the entry above: amdgpu's non-MES path for the gfx ring is
`gfx_v11_0_cp_gfx_resume`, which uses the **global `CP_RB0_*` registers**, not the HQD block:

```
CP_RB_WPTR_DELAY      0x0F61   = 0
CP_RB_VMID            0x1DF1   = 0
CP_RB0_CNTL           0x1DE1   = RB_BUFSZ | RB_BLKSZ<<8 | KMD_QUEUE
CP_RB0_WPTR / _HI     0x1DF4/5 = 0
CP_RB0_RPTR_ADDR/_HI  0x1DE3/4 = the writeback VA
CP_RB_WPTR_POLL_ADDR_LO/_HI  0x1E8B/0x1E8C
CP_RB0_BASE           0x1DE0   = ring VA >> 8
CP_RB0_BASE_HI        0x1E51
CP_RB_ACTIVE          0x1F40   = 1
CP_RB_DOORBELL_CONTROL 0x1E8D  = DOORBELL_EN | offset
CP_RB_DOORBELL_RANGE_LOWER/UPPER 0x1DFA/0x1DFB
```

with `rb_bufsz = order_base_2(ring_size / 8)`, `RB_BLKSZ = rb_bufsz - 2`, `KMD_QUEUE` set and
`CP_RB0_CNTL` written twice around a 1 ms delay. `mesboot me0rb` programs exactly that.

Result: `CP_RB_ACTIVE=1`, `CP_RB0_CNTL=0x00008789`, `CP_RB0_BASE=0xff060000` (the ring's VA >> 8), and after
the write pointer is advanced and the doorbells rung, **GRBM goes to 0xa800302c** - so the doorbell wakes the
CP, but it fetches nothing (`CP_RB0_RPTR` stays 0). That is the first time anything on the gfx ring reacted,
and it narrows the problem to the ring fetch itself:

1. `CP_RB0_BASE` may have to be an **MC address** rather than the ring's VA for this ring;
2. the ring's translation may go through the **MMHUB** context 0, which the port only programs when
   `debug.phxmmvm` is on (it is off by default) - the same instance question that mattered for the IB fetch.

Those two are the next experiments, and both are one-line variants of `me0rb`.

### The gfx ring: four variants, none fetch (2026-11-24)

`mesboot me0rb` programs the legacy `CP_RB0_*` ring exactly as `gfx_v11_0_cp_gfx_resume` does - including
`GRBM_GFX_CNTL.PIPEID = 0` for `gfx_v11_0_cp_gfx_switch_pipe`, which is a different register from the
`GRBM_GFX_INDEX` the port uses to pick register-block instances. In every variant `CP_RB_ACTIVE` reads 1 and
the doorbell wakes the CP (`GRBM_STATUS` goes to 0xa800302c), but `CP_RB0_RPTR` never leaves 0:

| variant | ring base | rptr/poll addresses | result |
|---|---|---|---|
| A | ring **VA** >> 8 | VA | no fetch |
| B | ring **MC** >> 8 | VA | no fetch |
| C | ring VA, **MMHUB context 0** also programmed (`MMVM=1`) | VA | no fetch |
| D | ring **MC** >> 8 | **MC** | no fetch |

So the ring is live and the doorbell reaches the CP, but the fetch never completes. Everything in
`gfx_v11_0_cp_gfx_resume` has now been reproduced except the parts around it:

* `CP_RB_VMID` / `CP_RB_WPTR_DELAY` - both written, both read 0;
* the **gfx ME/PFP microcode** - amdgpu loads it into the CP's RAM through `CP_PFP_UCODE_ADDR/DATA` and
  `CP_ME_RAM_WADDR/DATA` (`gfx_v11_0_cp_gfx_load_microcode`) or leaves it to the PSP, and then enables the ME
  through `CP_ME_CNTL`. The port's PSP does load `pfp`/`me`, but nothing has verified that the gfx ME is
  actually running - only the MEC (compute) side is proven, because its queue executes;
* `CP_MEC_CNTL` and the RLC scheduling state (`RLC_CP_SCHEDULERS` reads 0x30e8 on this card).

The next round starts there: read the gfx ME's state (`CP_ME_CNTL`, `CP_ME_STATUS`, the CP's ucode base
registers) and compare it with what a Linux capture shows on the same card, then decide whether the ring
needs the microcode path or only a different enable.

### The gfx ME had no microcode entry point - and still does not fetch (2026-11-24)

`gfx_v11_0_config_gfx_rs64` points the PFP and ME at their microcode through
`CP_PFP_PRGRM_CNTR_START(_HI)` and `CP_ME_PRGRM_CNTR_START(_HI)`, taking the addresses from the firmware
headers. The port only ever programmed the **MES** entry point, which is why the gfx ME had no code to run.

The values come straight out of `gc_11_0_3_pfp.bin` / `gc_11_0_3_me.bin` (`struct gfx_firmware_header_v2_0`:
an 8-dword `common_firmware_header` then `ucode_feature_version`, `ucode_size_bytes`, `ucode_offset_bytes`,
`data_size_bytes`, `data_offset_bytes`, `ucode_start_addr_lo`, `ucode_start_addr_hi`):

```
pfp: ucode_start_addr_lo = 0x00003000, ucode_start_addr_hi = 0x00070000
me : the same
register value = (hi << 30) | (lo >> 2) = 0x00000C00,  _HI = hi >> 2 = 0x0001C000
```

`mesboot me0rb` now writes those for both PFP/ME pipes and runs amdgpu's reset/clear of the PFP and ME
pipes through `CP_ME_CNTL` (`PFP_PIPE0/1_RESET` bits 18/19, `ME_PIPE0/1_RESET` bits 20/21). The registers read
back `PFP_START=0xC00 ME_START=0xC00 CP_ME_CNTL=0`, so the entry points are in place.

The ring still does not fetch (`CP_RB0_RPTR` stays 0 while the doorbell wakes the CP). What is left, in the
order they should be tried:

1. the gfx ring may need its own VMID context or a `CP_RB_VMID` other than 0 - the compute queue and the
   client IB fetches use the MMHUB instance, and the gfx ring may not share that view;
2. `CP_RB0_CNTL`'s other bits (`RB_NO_UPDATE`, `RB_NO_FETCH`, `CACHE_POLICY`);
3. the RLC's per-pipe enable for the gfx ring (`RLC_CP_SCHEDULERS` already has bit 7 set at 0x30e8);
4. a Linux capture of the *same* card's `CP_RB0_*` and `CP_ME_CNTL` after amdgpu brings its gfx ring up -
   the debugfs index space does not expose the CP's segment-1 registers (they all read 0 there), so the
   capture has to come from inside the kernel or from a driver with a register dump of its own.

### After the doorbell the CP resets its own ring (2026-11-24)

Reading the `CP_RB0_*` registers after the ring wedges (with `GRBM_GFX_CNTL.PIPEID = 0` selected, so the
read is in the right pipe context) says the CP did not ignore the ring - it took it and then cleared it:

```
CP_RB_ACTIVE  = 0x00000000     <- was 1 when me0rb programmed it
CP_RB0_BASE   = 0x00000000     <- was 0xff060000
CP_RB0_CNTL   = 0x00000000     <- was 0x00008789
CP_RB0_RPTR   = 0x80000000     <- bit 31, the "rptr valid" bit
CP_RB0_WPTR   = 0x00000000
CP_STAT       = 0x8000c000
CP_PFP_PRGRM_CNTR_START = 0x00000C00   <- our entry point, persisted
CP_ME_PRGRM_CNTR_START  = 0x00000C00
```

That is the signature of a gfx ring the CP reset after a fault: it accepted the programming, was woken by the
doorbell, and dropped the ring state instead of fetching. Two consequences for the next round:

1. the entry points may themselves be wrong - `0xC00` was derived from `struct gfx_firmware_header_v2_0` by
   parsing `gc_11_0_3_pfp.bin` by hand, and the same parse applied to `gc_11_0_3_mes_2.bin` does not reproduce
   the MES value Linux uses (0xFC001400), so the field offsets are not yet trusted; the MES header has a
   different struct and is the one that can be cross-checked;
2. a fault would explain the ring reset, so the CP's fault status (and the ME/PFP status registers, which read
   0) is what to look at next - together with the exact firmware header structs from `amdgpu_ucode.h` rather
   than a hand parse.

### The gfx queue is MES-adopted - and the firmware parse is verified (2026-11-24)

Two results that change the plan.

**1. The firmware header parse is correct.** The same formula amdgpu uses,
`(ucode_start_addr_hi << 30) | (ucode_start_addr_lo >> 2)`, applied to `gc_11_0_3_mes_2.bin`
(`struct mes_firmware_header_v1_0`: `mes_uc_start_addr_lo = 0xf0005000` at dword 14, `_hi = 0xffffffff` at 15)
gives exactly `0xFC001400` - the value Linux programs and the port already uses. So the parse is trusted, and
the PFP/ME entry points derived the same way (`0x00000C00` and `_HI 0x0001C000`) are right. Programming them
was correct; it was not what the gfx ring was missing.

**2. The accelerator's gfx queue is MES-adopted, not a legacy ring.** `gq[1].ready` is only ever set by the
debug adopt selector (`PhxBringup: adopted compute queue` comes from the same call), which is the same
`mesboot gfxq` -> MES `ADD_QUEUE(gfx, map_legacy_kq)` -> adopt sequence the XTX uses. So on this port the gfx
queue depends on the **MES creating a queue**, which is the one thing that has never worked here.

That makes the legacy `CP_RB0_*` ring a dead end: the CP accepts the programming, the doorbell wakes it, and it
then clears `CP_RB_ACTIVE`/`CP_RB0_BASE`/`CP_RB0_CNTL` - a ring reset, not a fetch. It also puts the MES back
on the critical path for everything the XTX does, which is a better place to spend effort than fighting a ring
the hardware keeps dropping.

**MES retried with the new knowledge**, programming the MMHUB instance of context 0 as well (`MMVM=1`), since
the CP fetches through that instance: the three frames are still consumed (rptr 128 -> 256 -> 384) and neither
`api_status` nor the completion fence is written. The MMHUB instance was not the missing piece.

So the next round returns to the MES with two new tools it did not have before: a **verified** firmware-header
parser (which can check every MES field against the file instead of a constant) and the knowledge that the
GCVM/MMHUB instance split matters for anything the CP fetches.

### The MES instruction-cache step, and why the gfx queue is a dead end without it (2026-11-24)

`mes_v11_0_load_microcode` does more than point the pipes at their ucode. After
`CP_MES_PRGRM_CNTR_START(_HI)` and `CP_MES_IC_BASE_LO/HI` it also sets
`CP_MES_IC_BASE_CNTL = 0`, `CP_MES_MIBOUND_LO = 0x1FFFFF`, `CP_MES_MDBASE_LO/HI` (the firmware's **data**
segment address), `CP_MES_MDBOUND_LO = 0x7FFFF`, and then, when priming, `CP_MES_IC_OP_CNTL` with
INVALIDATE_CACHE (bit 0) followed by PRIME_ICACHE (bit 4) while waiting for ICACHE_PRIMED (bit 5).

The port never ran the cache step. `mesboot mesfull` now does (`SEG1(0x2820)`), and the register reads back
**0** with primed = 0 - so either the address is not `CP_MES_IC_OP_CNTL` in this block or the prime silently
fails. The three MES frames are still consumed and the completion is still never written.

**Strategic conclusion for the port:** the accelerator's gfx queue is only ever made ready by the debug adopt
selector, i.e. by `MES ADD_QUEUE(gfx)` followed by adopt - the same sequence the XTX uses. So with the MES
unable to create a queue, there is no gfx queue, and RADV's gfx streams keep landing on the MEC compute queue
where `SET_UCONFIG_REG` wedges them. The legacy `CP_RB0_*` ring was tried in four variants and the CP clears
its own ring state every time, so it is not the way out.

**That makes the MES the single highest-value target again**, and the port now has three things it did not
have when the MES work stopped:

1. a verified firmware-header parser (`(hi << 30) | (lo >> 2)` reproduces the known MES entry point
   `0xFC001400` exactly), so every field can be checked against the file;
2. the knowledge that the CP fetches through the MMHUB instance (tried for the MES: no change, but it rules
   that out);
3. the full list of registers the driver programs around the MES, including the data segment and the cache
   step the port was missing.

### The MES is a RISC-V core, and it is idle - not faulting (2026-11-24)

The MES block exposes its RISC-V trap state (`mesboot mestrap`): `MEPC`, `MCAUSE`, `MBADADDR`, `MSTATUS`,
`MSCRATCH` and `INSTR_PNTR`, per pipe. After a submission that consumes all three frames:

```
pipe0: MEPC=0 MCAUSE=0 MBADADDR=0 MSTATUS=0 MSCRATCH=0 INSTR_PNTR=0x00007104 MES_CNTL=0x0c000000
pipe1: MEPC=0 MCAUSE=0 MBADADDR=0 MSTATUS=0 MSCRATCH=0 INSTR_PNTR=0x00004f64 MES_CNTL=0x0c000000
```

Two facts follow:

* **no trap** - the firmware never faulted, so it is not crashing on the API frames;
* **`INSTR_PNTR` does not move** across samples a second apart, and is identical before and after the
  submission. The MES cores are parked, not servicing the ring.

Yet the scheduler ring's `rptr` advances 0 -> 128 -> 256 -> 384, so something *is* consuming the frames - and
with the MES parked, that something is the **CP's HQD** for the scheduler ring, which the port programs as an
ordinary queue with `CP_HQD_ACTIVE = 1`. The frames are also visible in VRAM at 0xA110000 with a valid API
header (`0x00040131` = type 1, opcode 19 = SET_HW_RSRC_1, dwsize 0x40) at 0xA110200.

So the picture is: the CP consumes MES API frames as if they were PM4 (and discards them, because they are
not), while the MES firmware - which is supposed to poll that ring - sits idle. That is a different failure
than anything assumed before: not a bad firmware load, not a wrong entry point, not a missing cache prime, but
**the queue never being handed to the MES**.

The next experiment follows directly: take the scheduler ring's `CP_HQD_ACTIVE` down (so the CP cannot consume
the frames) and see whether the MES picks them up instead - `rptr` advancing with the CP's HQD disabled would
prove the MES is polling, and `rptr` staying at 0 would prove it is not, and that the driver has to hand the
queue over explicitly.

### Proved: the CP was the consumer, the MES never polls the ring (2026-11-24)

`HQD_OFF=1` leaves the scheduler ring's `CP_HQD_ACTIVE` at 0, so the CP cannot fetch it. Submitting the same
three frames then gives:

```
sched hqd: rptr=0 wptr_lo=0 dbctl=0xe0000058 active=0      (before and after every frame)
mes SET_HW_RESOURCES / _1 / QUERY_SCHED_STATUS: fence=0, status=0 -> TIMEOUT
MES INSTR_PNTR unchanged: 0x00007104 and 0x00004f64
```

`rptr` stays **0**, so with the CP's HQD off nothing consumes the frames at all - which means the
`rptr 0 -> 128 -> 256 -> 384` seen in every earlier MES experiment was the **CP's HQD**, not the MES.

Combined with `INSTR_PNTR` being identical across boots, samples and submissions, the honest reading is that
**the MES firmware has never been shown to execute**. Everything previously taken as evidence of it running -
the scratch words, the per-pipe words, the consumed frames, the `0x00010000` in the scheduler context - is
explained by the CP or by the driver's own registers.

That is a cleaner place to be than a mystery: the port's MES enable sequence matches `mes_v11_0_enable`
(`CP_MES_CNTL` reset bits 16/17 then ACTIVE 26/27, per-pipe `PRGRM_CNTR_START`, `MTVEC`, priorities, MIE), the
firmware header parse is verified, and the remaining question is now narrow: **does the MES core actually
start?** The registers that answer it are the RISC-V trap and PC state, which `mesboot mestrap` reads, and the
one thing not yet checked is whether the firmware image the MES fetches from (`CP_MES_IC_BASE`, `MIBOUND`) is
the ucode buffer the PSP loaded, rather than an address copied from a Linux capture.

Next round: verify `CP_MES_IC_BASE` against the ucode buffer the port itself allocates and loads, instead of
trusting the captured constant; that is the last unchecked input to the MES.

## The MES works, the gfx queue exists, and RADV runs (2026-11-24)

**The completion fence must be an MC address.** That one detail unblocked everything the port had been stuck
on:

```
mes SET_HW_RESOURCES:   fence=1 (want 1) status=1 -> OK
mes SET_HW_RESOURCES_1: fence=2 (want 2) status=1 -> OK
mes QUERY_SCHED_STATUS: fence=3 (want 3) status=1 -> OK
```

`api_status.api_completion_fence_addr` had been a **GPU VA** (`gGartVa + ...`). The MES resolves that field as
**MC**, so the firmware completed every API call correctly and then wrote the completion value through an
address nobody could read - which is why the firmware's PC advanced, the frames were consumed, and yet no
fence ever landed. `MES_FENCE_MC` was already a knob in `mesboot`; it had never been the default. It is now,
and `MES_FENCE_VA=1` restores the old form for A/B.

With the MES actually completing API calls, the queue it had always refused to create appears:

```
mes ADD_QUEUE(gfx, map_legacy_kq): fence=4 (want 4) status=1 -> OK
gfx hqd after map: active=1 base=0x800a1700 cntl=0x20a0088a dbctl=0x40000458 rptr=0
gfx: scratch1=0xdeadbeef (want deadbeef) testbuf=0x6f7a6f7a (want 6f7a6f7a) grbm=0x0000302c
adopt gfx queue -> kr 0x0
```

The queue is created by the MES, executes a register write **and** a memory write, and is handed to the
accelerator, so `gq[1]` is ready - the queue RADV's graphics streams need.

### RADV, on the RX 7800 XT

```
device: AMD Radeon RX 7800 XT (RADV NAVI32) (driver 26.2.99); mode compute, 128x128, 64 iterations, 4 s
frames 270722 in 4.00 s = 67680.2 frames/s; GPU errors 0
GPU per frame  ms: avg 0.004 median 0.004 p99 0.004 max 0.011 (n 270722)
GRBM: 0x0000302c
```

A full Vulkan compute workload - 270k dispatches, zero GPU errors, 4 us of GPU time per frame - through RADV
on this port. That is the first time the navi32 port has run a real graphics API workload.

### The chain that got here

```
completion fence as MC          -> the MES completes its API calls
  -> MES ADD_QUEUE(gfx)         -> a gfx queue exists and executes
    -> gq[1] ready in the kext  -> RADV's gfx streams have the right queue
      -> RADV runs              -> 270722 dispatches, 0 GPU errors
```

Everything before this round was correct except that one address: the firmware load, the entry points, the
32 KiB scheduler ring, the MQD words, the queue masks, the 64-dword frame with its MC `*_gpu_mc_ptr` fields.
The lesson worth keeping: when a firmware completes work but writes its completion where the driver never
sees it, check the *address form* of the fence before suspecting the firmware.
