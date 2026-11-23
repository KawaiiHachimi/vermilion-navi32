#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# smufeatures.py <low> <high> | <mask64> [<other low> <other high>]: names the SMU v13.0.0 feature bits of a running-
# features mask (FEATURE_*_BIT of Linux's smu13_driver_if_v13_0_0.h, MIT, as of 2026-11). macOS reads the mask with
# `phxctl smuinfo` (low / high words); Linux shows it in pp_features. With a second mask, prints the difference.
import sys

NAMES = {
    0: "FW_DATA_READ",
    1: "DPM_GFXCLK",
    2: "DPM_GFX_POWER_OPTIMIZER",
    3: "DPM_UCLK",
    4: "DPM_FCLK",
    5: "DPM_SOCCLK",
    6: "DPM_MP0CLK",
    7: "DPM_LINK",
    8: "DPM_DCN",
    9: "VMEMP_SCALING",
    10: "VDDIO_MEM_SCALING",
    11: "DS_GFXCLK",
    12: "DS_SOCCLK",
    13: "DS_FCLK",
    14: "DS_LCLK",
    15: "DS_DCFCLK",
    16: "DS_UCLK",
    17: "GFX_ULV",
    18: "FW_DSTATE",
    19: "GFXOFF",
    20: "BACO",
    21: "MM_DPM",
    22: "SOC_MPCLK_DS",
    23: "BACO_MPCLK_DS",
    24: "THROTTLERS",
    25: "SMARTSHIFT",
    26: "GTHR",
    27: "ACDC",
    28: "VR0HOT",
    29: "FW_CTF",
    30: "FAN_CONTROL",
    31: "GFX_DCS",
    32: "GFX_READ_MARGIN",
    33: "LED_DISPLAY",
    34: "GFXCLK_SPREAD_SPECTRUM",
    35: "OUT_OF_BAND_MONITOR",
    36: "OPTIMIZED_VMIN",
    37: "GFX_IMU",
    38: "BOOT_TIME_CAL",
    39: "GFX_PCC_DFLL",
    40: "SOC_CG",
    41: "DF_CSTATE",
    42: "GFX_EDC",
    43: "BOOT_POWER_OPT",
    44: "CLOCK_POWER_DOWN_BYPASS",
    45: "DS_VCN",
    46: "BACO_CG",
    47: "MEM_TEMP_READ",
    48: "ATHUB_MMHUB_PG",
    49: "SOC_PCC",
    50: "EDC_PWRBRK",
    51: "BOMXCO_SVI3_PROG",
}


def mask(args):
    v = [int(a, 0) for a in args]
    return v[0] if len(v) == 1 else v[0] | v[1] << 32


def names(m):
    return [NAMES.get(i, "SPARE_%d" % i) for i in range(64) if m >> i & 1]


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) not in (1, 2, 4):
        sys.exit(__doc__ or "usage: smufeatures.py <low> <high> [<other low> <other high>]")
    if len(a) == 4:
        m1, m2 = mask(a[:2]), mask(a[2:])
        print("only in the first: " + (", ".join(names(m1 & ~m2)) or "none"))
        print("only in the second: " + (", ".join(names(m2 & ~m1)) or "none"))
    else:
        m = mask(a)
        print("on: " + ", ".join(names(m)))
        print("off: " + ", ".join(NAMES[i] for i in range(52) if not m >> i & 1))
