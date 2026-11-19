// SPDX-License-Identifier: MIT
// PhxScanLayout.h: where the three GPU-present slots (scanout selectors 9..14, phx_scan.cpp) sit in VRAM, chosen by the
// size of the frame the live plane scans. Pure (no kernel headers), so tests/kext/scan_layout_test.cpp builds it on a
// host.
//
// - small, a frame up to 8 MiB (1920x1080x4 = 7.9 MiB, upstream's RX 7900 XT with two 1080p monitors): 24/32/40 MiB,
//   between phxfb's flip buffer B (16 MiB) and PHX_VRAM_RING_MIN (48 MiB). The layout of every build since b44,
//   unchanged; nothing is checked that b44..b125 did not check.
// - large, a frame over 8 MiB and up to 32 MiB (2560x1440x4 = 14.06 MiB on the RX 7900 XTX machine, up to
//   3840x2160x4 = 31.6 MiB): 256/288/320 MiB, [256 MiB, 352 MiB). That is past every fixed region (the page-table pool
//   ends at 240 MiB) and past BAR0 (256 MiB on macOS), so the CPU has no view of a large slot: only the GPU writes it
//   (a client BO rebound onto it, vmMapVram) and only HUBP0 reads it. No code in this kext, PhxAccel, mesboot or the
//   scripts uses VRAM there (audit 2026-11-19; BOs are system memory). The firmware's regions sit at the top of VRAM
//   (DMUB REGION4 near the top on the XT; Linux on the XTX: PSP TMR at 0x85_fc00_0000, firmware above 24560 MiB).
//   Taken only when both the VRAM (MMHUB FB location) and the display's FB window (DCN_VM_FB_LOCATION_BASE/TOP, which
//   the GOP programs) reach 352 MiB, so HUBP0 is never pointed at an address it cannot fetch.
#pragma once
#include <stdint.h>

namespace phxscanlay {

constexpr uint64_t kMiB = 1ull << 20;
constexpr uint32_t kSlots = 3;
constexpr uint64_t kSmallBase = 24 * kMiB, kSmallStride = 8 * kMiB;     // = PHXN_SCAN_SLOT_VRAM / PHXN_SCAN_SLOT_BYTES
constexpr uint64_t kLargeBase = 256 * kMiB, kLargeStride = 32 * kMiB;
constexpr uint64_t kLargeEnd = kLargeBase + kSlots * kLargeStride;      // 352 MiB

enum : int {
    kSmall = 0,
    kLarge = 1,
    kErrEmpty = -1,   // no frame (height 0)
    kErrTooBig = -2,  // over 32 MiB
    kErrVram = -3,    // the VRAM ends before 352 MiB
    kErrDcn = -4,     // the display's FB window ends before 352 MiB (or is disabled: top < base)
};

struct Layout {
    uint64_t base, stride;   // slot k at VRAM byte offset base + k * stride; a frame may take up to stride bytes
    int kind;                // kSmall or kLarge
};

// bytes = pitch x height of the plane; vramBytes = the VRAM size from the MMHUB FB location (0 = unknown, refuses large);
// dcnBase / dcnTop = DCN_VM_FB_LOCATION_BASE / _TOP bits 23:0 (16 MiB units, top inclusive).
inline int choose(uint64_t bytes, uint64_t vramBytes, uint32_t dcnBase, uint32_t dcnTop, Layout *out) {
    if (!bytes) return kErrEmpty;
    if (bytes <= kSmallStride) {
        if (out) *out = Layout{kSmallBase, kSmallStride, kSmall};
        return kSmall;
    }
    if (bytes > kLargeStride) return kErrTooBig;
    if (vramBytes < kLargeEnd) return kErrVram;
    const uint64_t dcnBytes = (dcnTop & 0xffffffu) >= (dcnBase & 0xffffffu)
                                  ? (static_cast<uint64_t>((dcnTop & 0xffffffu) - (dcnBase & 0xffffffu)) + 1) << 24 : 0;
    if (dcnBytes < kLargeEnd) return kErrDcn;
    if (out) *out = Layout{kLargeBase, kLargeStride, kLarge};
    return kLarge;
}

constexpr uint64_t slotOff(const Layout &l, uint32_t k) { return l.base + static_cast<uint64_t>(k) * l.stride; }

inline const char *name(int r) {
    switch (r) {
        case kSmall: return "small (24/32/40 MiB)";
        case kLarge: return "large (256/288/320 MiB)";
        case kErrEmpty: return "refused: no frame";
        case kErrTooBig: return "refused: frame over 32 MiB";
        case kErrVram: return "refused: VRAM ends below 352 MiB";
        case kErrDcn: return "refused: the display's FB window ends below 352 MiB";
        default: return "?";
    }
}

}  // namespace phxscanlay
