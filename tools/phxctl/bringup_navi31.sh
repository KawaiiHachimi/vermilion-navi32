#!/bin/bash
# SPDX-License-Identifier: MIT
# RX 7900 XT (Navi 31, GC 11.0.0 / PSP 13.0.0 / SMU 13.0.0) GFX power-up, in the order the Linux 6.8 driver used on
# this card (from a log of the PSP commands Linux sent; the capture tool is not included):
# PSP ring, TOC, SMU firmware, TMR, every engine's firmware through LOAD_IP_FW, RLC autoload, wait for
# BOOTLOAD_COMPLETE, then the RS64 program-counter setup Linux wrote (gfx_v11_0_config_gfx_rs64).
# Differences from the Radeon 780M bring-up: TOC comes from inside psp_13_0_0_sos.bin; the SMU firmware is loaded by the
# driver; CP firmware is RS64 + per-pipe stacks; RLC_V is loaded; no EnableGfxImu SMU message (APU-only — on a dGPU
# AUTOLOAD_RLC starts IMU/RLC); the bootload status register is 0xee82 (0xee7e is the GC 11.0.1 variant).
# Offsets/sizes inside each firmware file follow Linux 6.8's ucode headers and match what Linux loaded, ucode by ucode.
# Firmware: linux-firmware amdgpu/ files (non-kicker on both boards) in $FW. Run as root or with sudo.
# A reboot is armed for 3 minutes in case the PSP hangs and cancelled at the end.
set -u
SUDO=""; [ "$(id -u)" = 0 ] || SUDO=sudo
# ---- ASIC selection: Navi 31 (0x744c) or Navi 32 (0x747e) --------------------------------------------------------
# The boot kext publishes the ASIC it matched (property phx-asic); ASIC=navi31|navi32 overrides it for a dry run.
# File names and the offsets inside each file are per ASIC: see tools/phxctl/NAVI32-FIRMWARE.md for the Navi 32 table
# (parsed from the firmware headers; the PSP component sizes were cross-checked against RedNavi's extraction for
# this board). Navi 32 has no SPL component: that load is skipped when PSP_SPL_OFF is empty.
ASIC=${ASIC:-$(ioreg -r -n PhxBringup -l 2>/dev/null | sed -n 's/.*"phx-asic" = "\([a-z0-9]*\)".*/\1/p' | head -1)}
case "$ASIC" in
navi32)
    FW=${FW:-$HOME/gpu/fw/navi32}
    PSP_SOS=psp_13_0_10_sos.bin
    PSP_KDB_OFF=256;    PSP_KDB_SIZE=7488
    PSP_SPL_OFF=;       PSP_SPL_SIZE=
    PSP_SYS_OFF=7744;   PSP_SYS_SIZE=61696
    PSP_SOS_OFF=69440;  PSP_SOS_SIZE=109216
    # Also loaded before the SOS: without them the Navi 32 sOS gets its version into C2PMSG_58 but never sets
    # C2PMSG_81 (measured 2026-11-23; RedNavi's chain on this same card loaded all seven and got 81 != 0).
    PSP_SOC_OFF=178656; PSP_SOC_SIZE=94464;   PSP_SOC_CMD=0x000B0000
    PSP_INTF_OFF=273120; PSP_INTF_SIZE=33024; PSP_INTF_CMD=0x000D0000
    PSP_DBG_OFF=306144; PSP_DBG_SIZE=28928;   PSP_DBG_CMD=0x000C0000
    PSP_RAS_OFF=337376; PSP_RAS_SIZE=16640;   PSP_RAS_CMD=0x000E0000
    PSP_TOC_OFF=335072; PSP_TOC_SIZE=2304
    SMU=smu_13_0_10.bin; SMU_OFF=256; SMU_SIZE=261632
    SDMA=sdma_6_0_3.bin
    PFP=gc_11_0_3_pfp.bin; ME=gc_11_0_3_me.bin; MEC=gc_11_0_3_mec.bin
    MES2=gc_11_0_3_mes_2.bin; MES1=gc_11_0_3_mes1.bin; IMU=gc_11_0_3_imu.bin
    PFP_OFF=256;  PFP_SIZE=91488;   PFP_STK_OFF=91744;  PFP_STK_SIZE=139264
    ME_OFF=256;   ME_SIZE=174992;   ME_STK_OFF=175248;  ME_STK_SIZE=139264
    MEC_OFF=256;  MEC_SIZE=267008;  MEC_STK_OFF=267264; MEC_STK_SIZE=139264
    MES2_OFF=256; MES2_SIZE=126080; MES2_DAT_OFF=126336; MES2_DAT_SIZE=131072
    MES1_OFF=256; MES1_SIZE=87008;  MES1_DAT_OFF=87264;  MES1_DAT_SIZE=131072
    IMU_OFF=256;  IMU_SIZE=66048;   IMU_DAT_OFF=66304;  IMU_DAT_SIZE=66048
    RLC=gc_11_0_3_rlc.bin
    RLC_GPM_OFF=25344;  RLC_GPM_SIZE=2560
    RLC_SRM_OFF=27904;  RLC_SRM_SIZE=30368
    RLC_IRAM_OFF=58272; RLC_IRAM_SIZE=66048
    RLC_DRAM_OFF=124320; RLC_DRAM_SIZE=33280
    RLC_P_OFF=157600;   RLC_P_SIZE=8704
    RLC_V_OFF=166304;   RLC_V_SIZE=12800
    RLC_G_OFF=256;      RLC_G_SIZE=25088
    ;;
*)
    ASIC=navi31
    FW=${FW:-$HOME/gpu/fw/navi31}
    PSP_SOS=psp_13_0_0_sos.bin
    PSP_KDB_OFF=259344; PSP_KDB_SIZE=5888
    PSP_SPL_OFF=265232; PSP_SPL_SIZE=672
    PSP_SYS_OFF=256;    PSP_SYS_SIZE=157584
    PSP_SOS_OFF=157840; PSP_SOS_SIZE=99200
    PSP_SOC_OFF=; PSP_SOC_SIZE=; PSP_SOC_CMD=
    PSP_INTF_OFF=; PSP_INTF_SIZE=; PSP_INTF_CMD=
    PSP_DBG_OFF=; PSP_DBG_SIZE=; PSP_DBG_CMD=
    PSP_RAS_OFF=; PSP_RAS_SIZE=; PSP_RAS_CMD=
    PSP_TOC_OFF=257040; PSP_TOC_SIZE=2304
    SMU=smu_13_0_0.bin; SMU_OFF=256; SMU_SIZE=261632
    SDMA=sdma_6_0_0.bin
    PFP=gc_11_0_0_pfp.bin; ME=gc_11_0_0_me.bin; MEC=gc_11_0_0_mec.bin
    MES2=gc_11_0_0_mes_2.bin; MES1=gc_11_0_0_mes1.bin; IMU=gc_11_0_0_imu.bin
    PFP_OFF=256;  PFP_SIZE=91488;   PFP_STK_OFF=91744;  PFP_STK_SIZE=139264
    ME_OFF=256;   ME_SIZE=174992;   ME_STK_OFF=175248;  ME_STK_SIZE=139264
    MEC_OFF=256;  MEC_SIZE=267008;  MEC_STK_OFF=267264; MEC_STK_SIZE=139264
    MES2_OFF=256; MES2_SIZE=126016; MES2_DAT_OFF=126272; MES2_DAT_SIZE=131072
    MES1_OFF=256; MES1_SIZE=86912;  MES1_DAT_OFF=87168;  MES1_DAT_SIZE=131072
    IMU_OFF=256;  IMU_SIZE=66048;   IMU_DAT_OFF=66304;  IMU_DAT_SIZE=66048
    RLC=gc_11_0_0_rlc.bin
    RLC_GPM_OFF=25344;  RLC_GPM_SIZE=2560
    RLC_SRM_OFF=27904;  RLC_SRM_SIZE=36640
    RLC_IRAM_OFF=64544; RLC_IRAM_SIZE=66048
    RLC_DRAM_OFF=130592; RLC_DRAM_SIZE=33280
    RLC_P_OFF=163872;   RLC_P_SIZE=8704
    RLC_V_OFF=172576;   RLC_V_SIZE=12800
    RLC_G_OFF=256;      RLC_G_SIZE=25088
    ;;
esac

PHXCTL=${PHXCTL:-/usr/local/sbin/phxctl}   # root-owned copy, so a password-less sudo rule for it does not grant arbitrary root
P(){ $SUDO "$PHXCTL" "$@"; }

# VRAM layout (PhxNativeABI.h, navi31): FB MC base 0x80_0000_0000; ring carve-out at 48 MiB; TMR at 96 MiB;
# fw_buf at 128 MiB. The TOC is staged in the last MiB of fw_buf so it never overlaps a LOAD_IP_FW slot.
RING_OFF=0x3000000
TOC_BUF=0xF00000            # fw_buf-relative
TOC_MC_LO=0x08F00000        # 0x80_0000_0000 + 128 MiB + 15 MiB
# PSP TMR: the size comes from the LOAD_TOC response, the placement from where Linux put it on this board
# (dmesg: "reserve 0xa700000 from 0x83e0000000 for PSP TMR" -> VRAM offset 0x3e0000000, 512 MiB below the
# top of 16 GiB). The old hardcoded pair (96 MiB / 19 MiB) made SETUP_TMR answer 0xffff0006.
TMR_OFF=${TMR_OFF:-0x3e0000000}   # VRAM byte offset of the TMR (board constant, see comment above)
TMR_SIZE=${TMR_SIZE:-0}           # 0 = take it from LOAD_TOC; a value overrides it
# The PSP response buffer lives in the cmd block at ring + 16 KiB + 0x360; its fifth dword is the TMR size.
RESP_VRAM=${RESP_VRAM:-0x3004360}

for f in "$PSP_SOS" "$SMU" "$SDMA" "$PFP" "$ME" "$MEC" "$MES2" "$MES1" "$IMU" "$RLC"; do
  [ -f "$FW/$f" ] || { echo "missing $FW/$f"; exit 1; }
done

reg(){ P regs "$1" | awk '{print $NF}'; }

# PSP bootloader stage (Linux psp_v13_0_bootloader_load_component / _load_sos, as traced on this card): after a
# cold boot through the GOP the sOS is NOT running (C2PMSG_81 = 0) and the bootloader waits with C2PMSG_35 = 0x80000000.
# Each component is staged at fw_buf+0 (VRAM 128 MiB, 1 MiB aligned; MC 0x80_0800_0000 >> 20 = 0x80080), its address
# goes to C2PMSG_36 and the command to C2PMSG_35. Linux used a GTT buffer; VRAM is the driver's own debug option.
FWBUF_MC_1M=0x80080
ZERO=$FW/zero-1m.bin
[ -f "$ZERO" ] || head -c 1048576 /dev/zero > "$ZERO"
bl_wait(){
  local v
  for _ in $(seq 1 300); do v=$(reg 0x16063); [ $(( v >> 31 )) -eq 1 ] && return 0; sleep 0.01; done
  echo "PSP bootloader not ready (C2PMSG_35=$v)"; return 1
}
bl_load(){   # <name> <offset in sos.bin> <size> <bootloader cmd>
  bl_wait || return 1
  P stage "$ZERO" 0 1048576 0 >/dev/null
  P stage "$FW/$PSP_SOS" "$2" "$3" 0 >/dev/null
  P wreg 0x16064 $FWBUF_MC_1M >/dev/null
  P wreg 0x16063 "$4" >/dev/null
  echo "  $1 ($3 B) cmd $4 sent"
}

# Cold-GPU check, before the watchdog and before any write. The power-up below is the cold-boot sequence: on a warm
# GPU (the sOS already running, e.g. after a warm reboot from Linux) it would load the SMU firmware into a running SMU,
# which Linux never does. So a running sOS, or a C2PMSG_81 that can't be read, is a refusal: nothing is written to the
# GPU, no reboot is armed, exit 3. The refusal line is the last line printed: navi31_up.sh shows the last three and
# reports "bring-up failed", and phxboot-navi31.sh matches "refused: warm GPU" and takes its fallback (m2_fail on a
# desktop boot, firmware display only otherwise). A full power-off makes the GPU cold again.
# RESUME=after_tmr continues on purpose on a GPU this script brought up earlier in the same boot; it skips the check.
if [ "${RESUME:-}" != after_tmr ]; then
  echo "== sOS alive? (C2PMSG_81 sign of life, C2PMSG_58 version) =="
  P regs 0x16091 0x1607a 0x16063
  # Only phxctl's exact answer counts ("0x16091: 0x%08x"). Anything else (a failed read prints "(error) 0x00000000",
  # an open failure prints its own message) is unreadable: refused too, but said as such.
  # stderr is kept: phxctl reports an open failure there ("PhxBringup service not found"), and the refusal names it.
  SOSOUT=$(P regs 0x16091 2>&1)
  SOSLINE=$(printf '%s\n' "$SOSOUT" | grep -Ex '0x16091: 0x[0-9a-f]{8}' | head -1)
  if [ -z "$SOSLINE" ]; then
    echo "refused: warm GPU check failed: C2PMSG_81 unreadable ($(printf '%s' "$SOSOUT" | head -1 | cut -c1-80)): no firmware loaded, nothing written"
    exit 3
  fi
  SOS=${SOSLINE##* }
  if [ "$SOS" != "0x00000000" ]; then
    echo "refused: warm GPU (PSP sOS running, C2PMSG_81=$SOS): no firmware loaded, nothing written; shut the machine down fully (power off), then boot macOS"
    exit 3
  fi
  echo "sOS check: C2PMSG_81=$SOS (cold)"   # navi31_up.sh passes this line on, the daemon logs it at every boot
fi

echo "== watchdog: reboot in 3 min unless cancelled =="
# WATCHDOG=0 disarms the reboot for a hands-on session: when the watchdog fires it leaves /etc/nologin behind,
# which locks every non-root login out until the reboot completes.
if [ "${WATCHDOG:-1}" = 1 ]; then
  ( $SUDO /sbin/shutdown -r +3 >/dev/null 2>&1 & )   # matches the navi31 sudoers rule exactly
else
  echo "WATCHDOG=0: no reboot armed"
fi

L(){ P loadfw "$@" 2>&1 | grep loadfw | tee -a ~/gpu/fwaddr.log; }

# RESUME=after_tmr: continue on the ring left by an earlier STOP_AFTER=smu run (sOS up, SMU fw + TMR already
# loaded). Loading the SMU firmware a second time into a running SMU is something Linux never does.
if [ "${RESUME:-}" != after_tmr ]; then
  : > ~/gpu/fwaddr.log
  # the check above found the sOS not running (C2PMSG_81 = 0): start it through the PSP bootloader.
  # sOS liveness is C2PMSG_81 != 0 (Linux psp_v13_0_is_sos_alive, and RedNavi measured a non-zero, per-boot
  # random value on this card). C2PMSG_58 carries the sOS firmware version and is printed for diagnosis only:
  # the PSP bootloader sets it even when the sOS never starts (seen on Navi 32, 2026-11-23).
  sos_alive(){ [ "$(reg 0x16091)" != "0x00000000" ]; }
  echo "== PSP bootloader: KDB, SPL, SYS_DRV, SOS (offsets of the psp_v2 firmware layout) =="
  bl_load KDB   $PSP_KDB_OFF $PSP_KDB_SIZE 0x00080000 || exit 1
  [ -z "$PSP_SPL_OFF" ] || bl_load SPL $PSP_SPL_OFF $PSP_SPL_SIZE 0x10000000 || exit 1   # Navi 32 has no SPL
  bl_load SYS_DRV $PSP_SYS_OFF $PSP_SYS_SIZE 0x00010000 || exit 1
  # The sOS is loaded last, and on Navi 32 only after the rest of the chain (PSP 13.0.10): otherwise it
  # reports its version in C2PMSG_58 and then never sets C2PMSG_81.
  [ -z "$PSP_SOC_OFF" ]  || bl_load SOC_DRV  $PSP_SOC_OFF  $PSP_SOC_SIZE  "$PSP_SOC_CMD"  || exit 1
  [ -z "$PSP_INTF_OFF" ] || bl_load INTF_DRV $PSP_INTF_OFF $PSP_INTF_SIZE "$PSP_INTF_CMD" || exit 1
  [ -z "$PSP_DBG_OFF" ]  || bl_load DBG_DRV  $PSP_DBG_OFF  $PSP_DBG_SIZE  "$PSP_DBG_CMD"  || exit 1
  [ -z "$PSP_RAS_OFF" ]  || bl_load RAS_DRV  $PSP_RAS_OFF  $PSP_RAS_SIZE  "$PSP_RAS_CMD"  || exit 1
  bl_load SOS   $PSP_SOS_OFF $PSP_SOS_SIZE 0x00020000 || exit 1
  for _ in $(seq 1 200); do sos_alive && break; sleep 0.01; done
  P regs 0x1607a 0x16091 0x16080
  sos_alive || { echo "sOS did not start; leaving the watchdog armed"; exit 1; }
  echo "sOS up, version $(reg 0x1607a) (Linux reports 0x00321717 on Navi 32, 0x00310035 on Navi 31)"
  if [ "${STOP_AFTER:-}" = sos ]; then
    $SUDO /usr/bin/killall shutdown 2>/dev/null; echo "STOP_AFTER=sos: stopped with the sOS up, watchdog cancelled"; exit 0
  fi
  P psp create $RING_OFF
  echo "== LOAD_TOC (from $PSP_SOS) =="
  P stage "$FW/$PSP_SOS" $PSP_TOC_OFF $PSP_TOC_SIZE $TOC_BUF >/dev/null
  P raw 0x20 $TOC_MC_LO 0x80 0x900 0 0 0
  # The response carries the TMR size the PSP wants (psp_gfx_resp, 5th dword): RedNavi read 0xa700000 here on
  # this same card. Read it back from VRAM rather than trusting a constant.
  # phxctl vram prints "vram+0xOFF: w0 w1 w2 ..." with bare 8-digit hex words.
  TOC_WORDS=$(P vram $RESP_VRAM 6 2>/dev/null | sed -n "s/^vram+0x[0-9a-fA-F]*: //p" | head -1)
  LIVE_TMR=$(echo "$TOC_WORDS" | awk "{print \$5}")
  if [ -n "$LIVE_TMR" ] && [ "$LIVE_TMR" != "00000000" ]; then TMR_SIZE=$((0x$LIVE_TMR)); fi
  echo "  LOAD_TOC response (status, session, fw_lo, fw_hi, tmr_size): $TOC_WORDS"
  [ "$TMR_SIZE" = 0 ] && { echo "  no TMR size in the LOAD_TOC response; using the historical 0xa700000"; TMR_SIZE=0xa700000; }

  echo "== load firmware =="
  L "$FW/$SMU" $SMU_OFF $SMU_SIZE 18 0x000000            # SMC (before SETUP_TMR, as Linux)
  echo "== SETUP_TMR =="
  # TMR=0 skips it: on Navi 32 the command answers status 0xffff0006, and a TMR the PSP did not accept is a
  # plausible reason for EnableAllSmuFeatures never being answered.
  if [ "${TMR:-1}" = 1 ]; then
    # Six arguments, as RedNavi sent them on this card (RedNavi/kern_probe.cpp sendPspSetupTmr):
    #   mc_lo, mc_hi, size, flags (2 = virt/phy pair), pa_lo, pa_hi
    # mc = VRAM MC base + offset (the GPU address the PSP sees), pa = BAR0 physical address + the same offset
    # (amdgpu_gmc_vram_pa). Both bases are published by the kext.
    FB_MC=$(ioreg -r -c PhxBringup -l 2>/dev/null | sed -n "s/.*\"phx-fb-mc-base\" = \([0-9][0-9]*\).*/\1/p" | head -1)
    FB_PA=$(ioreg -r -c PhxBringup -l 2>/dev/null | sed -n "s/.*\"phx-fb-aper-phys\" = \([0-9][0-9]*\).*/\1/p" | head -1)
    [ -n "$FB_MC" ] || { echo "SETUP_TMR: no phx-fb-mc-base property (kext not up?)"; exit 1; }
    [ -n "$FB_PA" ] || { echo "SETUP_TMR: no phx-fb-aper-phys property"; exit 1; }
    TMR_MC=$((FB_MC + TMR_OFF))
    TMR_PA=$((FB_PA + TMR_OFF))
    TMR_MC_LO=$(printf 0x%08x $((TMR_MC & 0xffffffff)))
    TMR_MC_HI=$(printf 0x%08x $(( (TMR_MC >> 32) & 0xffffffff )))
    TMR_PA_LO=$(printf 0x%08x $((TMR_PA & 0xffffffff)))
    TMR_PA_HI=$(printf 0x%08x $(( (TMR_PA >> 32) & 0xffffffff )))
    TMR_SIZE_HEX=$(printf 0x%x "$TMR_SIZE")
    echo "  SETUP_TMR args: mc ${TMR_MC_LO}:${TMR_MC_HI} (0x$(printf %x $TMR_MC)) size ${TMR_SIZE_HEX} flags 0x2 pa ${TMR_PA_LO}:${TMR_PA_HI} (0x$(printf %x $TMR_PA))"
    P raw 0x5 "$TMR_MC_LO" "$TMR_MC_HI" "$TMR_SIZE_HEX" 0x2 "$TMR_PA_LO" "$TMR_PA_HI"
  else
    echo "TMR=0: SETUP_TMR skipped"
  fi
  if [ "${STOP_AFTER:-}" = smu ]; then
    echo "== SMU check: TestMessage(0x1234) -> 0x1235, GetSmuVersion (Linux: 0x004e8200 = 78.130.0), GetDriverIfVersion (0x40) =="
    P smu 0x01 0x1234; P smu 0x02 0; P smu 0x03 0
    $SUDO /usr/bin/killall shutdown 2>/dev/null; echo "STOP_AFTER=smu: stopped after SMC + TMR, watchdog cancelled"; exit 0
  fi
else
  echo "== RESUME=after_tmr: reusing the existing ring; SMU check =="
  P psp status
  P smu 0x02 0
fi
L "$FW/$SDMA" 256 17408 71 0x040000              # SDMA_UCODE_TH0
L "$FW/$SDMA" 17664 16896 72 0x050000            # SDMA_UCODE_TH1
L "$FW/$PFP" $PFP_OFF $PFP_SIZE 87 0x060000           # CP_RS64_PFP
L "$FW/$ME" $ME_OFF $ME_SIZE 88 0x080000           # CP_RS64_ME
L "$FW/$MEC" $MEC_OFF $MEC_SIZE 89 0x0b0000          # CP_RS64_MEC
L "$FW/$PFP" $PFP_STK_OFF $PFP_STK_SIZE 90 0x100000        # CP_RS64_PFP_P0_STACK
L "$FW/$PFP" $PFP_STK_OFF $PFP_STK_SIZE 91 0x130000        # CP_RS64_PFP_P1_STACK
L "$FW/$ME" $ME_STK_OFF $ME_STK_SIZE 92 0x160000        # CP_RS64_ME_P0_STACK
L "$FW/$ME" $ME_STK_OFF $ME_STK_SIZE 93 0x190000        # CP_RS64_ME_P1_STACK
L "$FW/$MEC" $MEC_STK_OFF $MEC_STK_SIZE 94 0x1c0000       # CP_RS64_MEC_P0_STACK
L "$FW/$MEC" $MEC_STK_OFF $MEC_STK_SIZE 95 0x1f0000       # CP_RS64_MEC_P1_STACK
L "$FW/$MEC" $MEC_STK_OFF $MEC_STK_SIZE 96 0x220000       # CP_RS64_MEC_P2_STACK
L "$FW/$MEC" $MEC_STK_OFF $MEC_STK_SIZE 97 0x250000       # CP_RS64_MEC_P3_STACK
L "$FW/$MES2" $MES2_OFF $MES2_SIZE 33 0x280000        # CP_MES
L "$FW/$MES2" $MES2_DAT_OFF $MES2_DAT_SIZE 34 0x2a0000     # CP_MES_DATA
L "$FW/$MES1" $MES1_OFF $MES1_SIZE 81 0x2c0000          # CP_MES1 (KIQ)
L "$FW/$MES1" $MES1_DAT_OFF $MES1_DAT_SIZE 82 0x2e0000       # CP_MES1_DATA
L "$FW/$IMU" $IMU_OFF $IMU_SIZE 68 0x300000           # IMU_I
L "$FW/$IMU" $IMU_DAT_OFF $IMU_DAT_SIZE 69 0x320000         # IMU_D
L "$FW/$RLC" "$RLC_GPM_OFF"  "$RLC_GPM_SIZE"  20 0x340000   # RLC_RESTORE_LIST_GPM_MEM
L "$FW/$RLC" "$RLC_SRM_OFF"  "$RLC_SRM_SIZE"  21 0x350000   # RLC_RESTORE_LIST_SRM_MEM
L "$FW/$RLC" "$RLC_IRAM_OFF" "$RLC_IRAM_SIZE" 26 0x360000   # RLC_IRAM
L "$FW/$RLC" "$RLC_DRAM_OFF" "$RLC_DRAM_SIZE" 48 0x380000   # RLC_DRAM
L "$FW/$RLC" "$RLC_P_OFF"    "$RLC_P_SIZE"    25 0x390000   # RLC_P
L "$FW/$RLC" "$RLC_V_OFF"    "$RLC_V_SIZE"    7  0x3a0000   # RLC_V
L "$FW/$RLC" "$RLC_G_OFF"    "$RLC_G_SIZE"    8  0x3b0000   # RLC_G

echo "== AUTOLOAD_RLC =="
P autoload

# On a dGPU the SMU powers GFX up: Linux sent this right after AUTOLOAD_RLC (+745 ms), and the RLC autoload only
# completed afterwards. Tables go to our VRAM (+144 MiB driver table, +145 MiB tools table, MC 0x80_09xx_xxxx);
# the kext refuses DRAM addresses outside that window. TransferTableSmu2Dram(1) reads the combo PPTable from PMFW.
S(){ local out; out=$(P smu "$@"); echo "$out"; echo "$out" | grep -q "(OK)" || { echo "SMU message $1 failed; the watchdog reboots in a few minutes"; exit 1; }; }
echo "== SMU init =="
S 0x03 0              # GetDriverIfVersion
S 0x02 0              # GetSmuVersion
# This order is RedNavi's, verified on this same card (docs/COMPAT.md:74, RedNavi/kern_probe.cpp:1026-1034):
# driver/tools DRAM addresses, RunDcBtc, the allowed-features masks (always rejected with CmdRejectedPrereq while
# SCPM is on - the PSP owns the mask - so a rejection is tolerated), and only then EnableAllSmuFeatures, which is
# what boots the RLC (BOOTLOAD_STATUS 0xc000001f). TransferTableSmu2Dram is NOT in that list: sending it before the
# enable made the enable time out here.
S 0x0E 0x80           # SetDriverDramAddrHigh
S 0x0F 0x09000000     # SetDriverDramAddrLow
S 0x10 0x80           # SetToolsDramAddrHigh
S 0x11 0x09100000     # SetToolsDramAddrLow
S 0x36 0              # RunDcBtc
POUT=$(P smu 0x05 0xffffffff); echo "$POUT"   # SetAllowedFeaturesMaskHigh (CmdRejectedPrereq is expected)
POUT=$(P smu 0x04 0xffffffff); echo "$POUT"   # SetAllowedFeaturesMaskLow  (idem)
if [ "${SMU_ENABLE:-1}" = 1 ]; then
  echo "  EnableAllSmuFeatures:"; P smu 0x06 0
  # PMFW may enable the features asynchronously: watch RLC_RLCS_BOOTLOAD_STATUS for BOOTLOAD_COMPLETE for 2 s
  # before calling it a failure (RedNavi saw the RLC boot right after this message).
  started=0
  for i in $(seq 1 20); do
    bl=$(reg 0x0ee82)
    if [ $(( bl >> 31 )) -eq 1 ]; then started=1; echo "  RLC booted after the enable: BOOTLOAD_STATUS $bl"; break; fi
    sleep 0.1
  done
  [ $started -eq 1 ] || { echo "EnableAllSmuFeatures did not bring the RLC up (BOOTLOAD_STATUS $bl)"; exit 1; }
else
  echo "SMU_ENABLE=0: EnableAllSmuFeatures not sent (the RLC autoload will not complete without it)"
fi
S 0x0D 0; S 0x0C 0    # GetRunningSmuFeaturesHigh / Low
echo "== wait for CP_STAT == 0 and RLC_RLCS_BOOTLOAD_STATUS bit 31 (Linux saw 0xc000001f) =="
ok=0
for i in $(seq 1 50); do
  out=$(P regs 0x21a0 0xee82)
  cp=$(echo "$out" | awk '/0x021a0/{print $2}'); bl=$(echo "$out" | awk '/0x0ee82/{print $2}')
  if [ "$cp" = "0x00000000" ] && [ $(( bl >> 31 )) -eq 1 ]; then ok=1; break; fi
  sleep 0.1
done
echo "$out"
if [ $ok -ne 1 ]; then
  echo "autoload did not complete; the watchdog reboots in a few minutes (do not cancel it late: a killed"
  echo "shutdown leaves /etc/nologin behind and blocks every login until a manual restart)"; exit 1
fi

echo "== RS64 program-counter setup (Linux gfx_v11_0_config_gfx_rs64, 33 writes from the trace) =="
P wreg 0x0a900 0x00000000   # GRBM_GFX_CNTL pipe 0
P wreg 0x030a4 0x00000c00   # CP_PFP_PRGRM_CNTR_START
P wreg 0x030b9 0x0001c000   # CP_PFP_PRGRM_CNTR_START_HI
P wreg 0x0a900 0x00000001   # GRBM_GFX_CNTL pipe 1
P wreg 0x030a4 0x00000c00   # CP_PFP_PRGRM_CNTR_START
P wreg 0x030b9 0x0001c000   # CP_PFP_PRGRM_CNTR_START_HI
P wreg 0x0a900 0x00000000   # GRBM_GFX_CNTL
P wreg 0x0a803 0x153c0000   # CP_ME_CNTL
P wreg 0x0a803 0x15300000   # CP_ME_CNTL
P wreg 0x0a900 0x00000000   # GRBM_GFX_CNTL pipe 0
P wreg 0x030a5 0x00000c00   # CP_ME_PRGRM_CNTR_START
P wreg 0x030d9 0x0001c000   # CP_ME_PRGRM_CNTR_START_HI
P wreg 0x0a900 0x00000001   # GRBM_GFX_CNTL pipe 1
P wreg 0x030a5 0x00000c00   # CP_ME_PRGRM_CNTR_START
P wreg 0x030d9 0x0001c000   # CP_ME_PRGRM_CNTR_START_HI
P wreg 0x0a900 0x00000000   # GRBM_GFX_CNTL
P wreg 0x0a803 0x15300000   # CP_ME_CNTL
P wreg 0x0a803 0x15000000   # CP_ME_CNTL
P wreg 0x0a900 0x00000004   # GRBM_GFX_CNTL MEC pipe 0
P wreg 0x0c900 0x00000c00   # CP_MEC_RS64_PRGRM_CNTR_START
P wreg 0x0c938 0x0001c000   # CP_MEC_RS64_PRGRM_CNTR_START_HI
P wreg 0x0a900 0x00000005   # GRBM_GFX_CNTL MEC pipe 1
P wreg 0x0c900 0x00000c00   # CP_MEC_RS64_PRGRM_CNTR_START
P wreg 0x0c938 0x0001c000   # CP_MEC_RS64_PRGRM_CNTR_START_HI
P wreg 0x0a900 0x00000006   # GRBM_GFX_CNTL MEC pipe 2
P wreg 0x0c900 0x00000c00   # CP_MEC_RS64_PRGRM_CNTR_START
P wreg 0x0c938 0x0001c000   # CP_MEC_RS64_PRGRM_CNTR_START_HI
P wreg 0x0a900 0x00000007   # GRBM_GFX_CNTL MEC pipe 3
P wreg 0x0c900 0x00000c00   # CP_MEC_RS64_PRGRM_CNTR_START
P wreg 0x0c938 0x0001c000   # CP_MEC_RS64_PRGRM_CNTR_START_HI
P wreg 0x0a900 0x00000000   # GRBM_GFX_CNTL
P wreg 0x0c904 0x400f0000   # CP_MEC_RS64_CNTL
P wreg 0x0c904 0x40000000   # CP_MEC_RS64_CNTL

echo "== doorbells (Linux values; the GOP boot leaves them off) =="
# NBIO doorbell aperture, S2A doorbell routing (above the 1 MiB BAR5 window: indirect through
# BIF_BX0_PCIE_INDEX2/DATA2 = 0xe/0xf, as amdgpu_device_indirect_wreg), CP MEC/RB doorbell ranges.
IW(){ P wreg 0xe "$(printf 0x%x $(( $1 * 4 )))" >/dev/null; P wreg 0xf "$2" >/dev/null; }
P wreg 0xde0 0x1 >/dev/null                                   # RCC DOORBELL_APER_EN
IW 0x507a40 0x30000007; IW 0x507a41 0x00057801; IW 0x507a42 0x3051001d
IW 0x507a43 0x3000000d; IW 0x507a44 0x40118809; IW 0x507a45 0x70118c0f
P wreg 0x305c 0x0 >/dev/null; P wreg 0x305d 0x450 >/dev/null # CP_MEC_DOORBELL_RANGE_LOWER/UPPER
P wreg 0x305a 0x458 >/dev/null; P wreg 0x305b 0x7f8 >/dev/null # CP_RB_DOORBELL_RANGE_LOWER/UPPER
P regs 0xde0 0x305c 0x305d 0x305a 0x305b

echo "== unhalt MEC (4 pipes) and ME, as Linux's last CP writes before MES start =="
P wreg 0xc904 0x3c000000 >/dev/null   # CP_MEC_RS64_CNTL: pipes 0-3 active, MEC_HALT clear
P wreg 0xa803 0x01000000 >/dev/null   # CP_ME_CNTL
P regs 0xc904 0xa803

echo "== state: CP_STAT, GRBM_STATUS, CP_ME_CNTL, CP_MEC_RS64_CNTL, IMU fw (expect 0x0b1f4b00) =="
P regs 0x21a0 0x2004 0xa803 0xc904 0xe068
$SUDO /usr/bin/killall shutdown 2>/dev/null; echo "watchdog cancelled rc=$?"
