#!/bin/bash
# SPDX-License-Identifier: MIT
# navi32_up.sh: bring the RX 7800 XT (Navi 32) up far enough for RADV, from a cold boot, in one command.
#
#   sudo bash navi32_up.sh            # bring up, then verify with vkbench
#   sudo bash navi32_up.sh --no-test  # bring up only
#
# What it does, in order (each step is a mesboot/phxctl subcommand that docs/NAVI32.md explains):
#   bringup_navi31.sh   PSP firmware, TMR, SMU, RLC, GCVM, MES firmware
#   mesfull             MES reset + per-pipe program counters (bringup leaves it halted)
#   gart                GART window + context 0 (both VM instances with MMVM=1)
#   (CP_ME_CNTL = 0)    unhalt the ME/MEC
#   kiqsched            the MES API frames - these only complete because the completion fence is MC
#   gfxq ADOPT=1        MES ADD_QUEUE(gfx) + ring test + adopt into VermilionAccel (gq[1])
#   computeq ADOPT=1    same for the MEC compute queue (gq[0])
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
PHX=${PHXCTL:-/usr/local/sbin/phxctl}
MES=${MESBOOT:-/usr/local/sbin/mesboot}
ASIC=${ASIC:-navi32}
step() { printf '== %s\n' "$*"; }

step 'bringup (PSP, TMR, SMU, RLC, GCVM, MES firmware)'
# Capture output and exit status without a pipeline swallowing bringup_navi31.sh's refusal (exit 3 on a warm GPU).
set +e
BU_OUT=$( cd "$HERE" && env ASIC=$ASIC WATCHDOG=0 bash bringup_navi31.sh 2>&1 )
BU_RC=$?
set -e
printf '%s\n' "$BU_OUT" | tail -3
printf '%s\n' "$BU_OUT" | grep -m1 '^sOS check:' || true
if [ "$BU_RC" -ne 0 ]; then
  echo "bring-up failed"
  exit 1
fi

step 'MES full init'
$MES mesfull | tail -1

step 'GART + context 0 (both VM instances)'
env MMVM=1 $MES gart | tail -2

step 'unhalt the ME/MEC'
$PHX wreg 0xa803 0x0 >/dev/null

step 'MES API frames'
$MES kiqsched | grep -E 'mes (SET|QUERY)' || true

step 'gfx queue via MES + adopt'
env ADOPT=1 $MES gfxq | grep -E 'ADD_QUEUE|gfx:|adopt' | tail -3

step 'compute queue via MES-less direct programming + adopt'
env ADOPT=1 $MES computeq | grep -E 'ring test|adopt' | tail -2

step 'VRAM-backed BOs (the pool needs the queues, so it comes after the adopt)'
# The selftest proves the GPU can write the pool through CP DMA; debug.phxbo_vram is refused until it passes in this
# boot. Then arm the pool for the processes whose buffers should live in VRAM (WindowServer's frames and every
# GPU client: 535 GB/s instead of 47.7 through PCIe).
sysctl -w debug.phxbo_vram_selftest=1 >/dev/null 2>&1
st=$(sysctl -n debug.phxbo_vram_selftest 2>/dev/null || echo 0)
if [ "$st" = 1 ]; then
  sysctl -w debug.phxbo_vram=1 >/dev/null 2>&1
  sysctl -w debug.phxbo_vram_proc="WindowServer,vkbench,metaltest,metalbench,apiprobe,vkwsi" >/dev/null 2>&1
  echo "  selftest passed, VRAM BOs armed: $(sysctl -n debug.phxbo_vram_stat 2>/dev/null | sed -n 2p | cut -c1-90)"
else
  echo "  selftest $st: VRAM BOs stay off (buffers in system memory)"
fi

step 'VCN bring-up (video decode engine)'
if [ "${PHX_SKIP_VCN:-0}" = 1 ]; then
  # called from phxboot-navi31.sh, which runs VCN itself after its own done line (it owns /var/log/phxboot.log)
  echo '  skipped (the boot daemon brings VCN up after done)'
elif [ -f "$HOME/gpu/fw/navi32/vcn_4_0_0.bin" ]; then
  # vcn_up.sh refuses to touch the GPU unless the boot daemon logged done for this boot; this script is that
  # step, so record it. The pre-flight checks that follow are the real safety (PSP idle, SMU OK, address config).
  sudo sh -c 'printf "== navi32_up.sh bring-up\ndone\n" > /var/log/phxboot.log' 2>/dev/null || true
  env FW=$HOME/gpu/fw/navi32/vcn_4_0_0.bin EXPECT_GB_ADDR_CONFIG=0x00000444 \
      bash "$HERE/vcn_up.sh" --run 2>&1 | grep -E 'M1 PASS|STOP|step 7|step 8' | tail -4
else
  echo '  no navi32 VCN firmware; skipped'
fi

if [ "${1:-}" != --no-test ]; then
  step 'verify: vkbench compute on RADV'
  VKBENCH_DRIVER=/Library/GPUBundles/nvmtl/libvulkan_radeon.dylib "${VKBENCH:-$HERE/../vkbench}" compute 3 64 128 128 2>&1 | tail -3 || true
fi

printf '== GRBM_STATUS: %s\n' "$($PHX regs 0x2004 2>/dev/null | tail -1)"
# Banner matched by phxboot-navi31.sh (grep -qE "navi3[12] up:") to run sdma_idle -> gfx_cg -> uclk_boot before done.
echo "navi32 up: compute + gfx queues ready"