// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// PhxP0Modes.h: the pipe-0 mode table, published by PhxBringup and read by PhxAMDFB head 0.
//
// navi32 (2026-11-24): RedNavi, AMDRadeonRDNA4 and NullMoth all keep the mode table and the mode id in ONE place, and
// the framebuffer never derives a mode from the hardware afterwards. This port had two hand-written tables (the accel's
// kHead0Modes and kHead0Sync) and the mode engine's plan family, which drifted: a 144 Hz request reached the pipe as 120
// and later as 144, and System Settings black-screened the panel.
//
// So the engine publishes what it can actually program: one entry per plan in its family, in list order. The id of a mode
// is its position (1..n), and every timing field the framebuffer reports comes from the plan's own OTG registers, not from
// a hand-copied sync table.
//
// Consumers: PhxBringup (p0PublishModes, phx_pipe0.cpp) sets the property; PhxAMDFB (start) reads it and uses it for
// getDisplayModes, getTimingInfoForDisplayMode and switchHead0Mode.
#pragma once
#include <stdint.h>

namespace phxp0pub {

// One mode. Published as an array of these (kFields uint32 each), little endian, in list order.
struct Mode {
    uint32_t id;        // 1-based position, the value getDisplayModes returns
    uint32_t w, h;      // active raster
    uint32_t milliHz;   // rounded refresh, the rate the plan programs
    uint32_t pclkHz;    // pixel clock of the plan
    uint32_t hTotal, vTotal, hBlank, hSyncOff, hSyncWidth, vBlank, vSyncOff, vSyncWidth;
    uint32_t hPos, vPos;    // 1 = positive sync polarity, as CoreDisplay wants it
    uint32_t key;           // the value phxPipe0SetMode takes for it (the plan's rate)
};
constexpr unsigned kFields = 16;
static_assert(sizeof(Mode) == kFields * sizeof(uint32_t), "the published stride is kFields dwords");
constexpr unsigned kMaxModes = 32;

// The property PhxBringup sets on its service (an OSData of n * sizeof(Mode)) and PhxAMDFB reads.
static constexpr const char *kProperty = "phx-p0-modes";

}  // namespace phxp0pub