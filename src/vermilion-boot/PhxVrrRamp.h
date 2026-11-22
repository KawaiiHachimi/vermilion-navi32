// SPDX-License-Identifier: MIT
// PhxVrrRamp.h: Adaptive Sync's frame-length ramp (debug.phxvrr_ramp; since VermilionBoot b147).
//
// Why: an OLED's brightness follows the frame length, so a frame that jumps from the floor (16.7 ms at 60 Hz) to 240 Hz
// (4.2 ms) shows as a flicker. Seen on the XTX machine's LG 27GS95QE; the same monitor does it on Windows and Linux,
// where it shows less because the desktop runs at a fixed rate and only fullscreen games use VRR. macOS's Variable
// covers the whole desktop, idle at the floor and animations at 240. The ramp bounds the change: at each flip the next
// frames may be at most `pct` % shorter or longer than the frames since the previous flip, inside the mode's range
// (V_TOTAL down to the floor's V_TOTAL_MAX). The window is OTG0 V_TOTAL_MIN / MAX, the registers Linux's freesync
// module re-programs during a stream too (its below-the-range and static-screen ramps, modules/freesync/freesync.c);
// no other register. pct 0 = no ramp (the mode's full range).
// Kernel-free; used by phx_scan.cpp's scanPresent; host test tests/kext/vrr_ramp_test.cpp.
#pragma once
#include <stdint.h>

namespace phxvrr {

// The frame length in lines (V_TOTAL + 1) a frame of periodNs lasts at a mode of vTotal (register value) and hz.
inline uint32_t linesFromPeriod(uint64_t periodNs, uint32_t vTotal, uint32_t hz) {
    if (!periodNs || !hz) return 0;
    const uint64_t l = periodNs * (static_cast<uint64_t>(vTotal) + 1) * hz / 1000000000ull;
    return l > 0xffffffu ? 0xffffffu : static_cast<uint32_t>(l);
}

// The next window (register values: lines - 1) around `lines`, at most pct % either way, inside [vmin, vmax].
// pct 0, out of 1-50, no measurement or an empty range = the full range.
inline void window(uint32_t lines, uint32_t vmin, uint32_t vmax, uint32_t pct, uint32_t *lo, uint32_t *hi) {
    *lo = vmin; *hi = vmax;
    if (!pct || pct > 50 || !lines || vmax <= vmin) return;
    const uint64_t cur = lines - 1;
    uint64_t l = cur * (100 - pct) / 100, h = (cur * (100 + pct) + 99) / 100;
    if (l < vmin) l = vmin;
    if (l > vmax) l = vmax;
    if (h > vmax) h = vmax;
    if (h < l) h = l;
    *lo = static_cast<uint32_t>(l); *hi = static_cast<uint32_t>(h);
}

// Write order that never leaves MIN above MAX between the two writes: true = MAX first (Linux's order), false = MIN first
// (the window moves down past the old MIN). newLo plays no part; it is taken so a call names the whole new window.
inline bool maxFirst(uint32_t newLo, uint32_t newHi, uint32_t oldLo) { (void)newLo; return newHi >= oldLo; }

}  // namespace phxvrr
