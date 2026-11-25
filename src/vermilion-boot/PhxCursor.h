// SPDX-License-Identifier: MIT
// PhxCursor.h: DCN 3.2.0 HUBP0 / DPP0 hardware cursor plane pure logic and platform-call ABI (b163).
//
// Ported from RedNavi's verified RX 7800 XT HUBP0 cursor plane (Linux hubp32_cursor_set_attributes,
// dpp3_set_cursor_attributes, hubp1_cursor_set_position). Kernel-free so VermilionBoot, VermilionAccel
// and tests/kext/cursor_test.cpp share the exact register layout, row-spreading and coordinate clipping.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace phxcursor {

constexpr uint32_t kMaxDim = 64u;
constexpr uint32_t kPitchPixels = 64u;
constexpr uint32_t kImageBytes = kPitchPixels * kMaxDim * 4u;   // 16 KiB inside the 64 KiB VRAM slot

// DCN 3.2.0 absolute BAR5 dword offsets (DMU segment 2 base 0x34c0, identical on Navi 31 and Navi 32).
constexpr uint32_t kRegPriViewportDim    = 0x3AAAu;   // HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION: w | (h << 16)
constexpr uint32_t kRegCursorSettings    = 0x3B1Bu;   // HUBPREQ0_CURSOR_SETTINGS (0x34c0 + 0x65b)
constexpr uint32_t kRegRefFreqToPixFreq  = 0x3B1Cu;   // HUBPREQ0_REF_FREQ_TO_PIX_FREQ (0x34c0 + 0x65c, 0.19 fixed pt)
constexpr uint32_t kRegCursorControl     = 0x3B38u;   // CURSOR0_0_CURSOR_CONTROL (0x34c0 + 0x678)
constexpr uint32_t kRegCursorAddrLo      = 0x3B39u;   // CURSOR0_0_CURSOR_SURFACE_ADDRESS (0x34c0 + 0x679)
constexpr uint32_t kRegCursorAddrHi      = 0x3B3Au;   // CURSOR0_0_CURSOR_SURFACE_ADDRESS_HIGH (0x34c0 + 0x67a)
constexpr uint32_t kRegCursorSize        = 0x3B3Bu;   // CURSOR0_0_CURSOR_SIZE (0x34c0 + 0x67b)
constexpr uint32_t kRegCursorPosition    = 0x3B3Cu;   // CURSOR0_0_CURSOR_POSITION (0x34c0 + 0x67c)
constexpr uint32_t kRegCursorHotSpot     = 0x3B3Du;   // CURSOR0_0_CURSOR_HOT_SPOT (0x34c0 + 0x67d)
constexpr uint32_t kRegCursorDstOffset   = 0x3B3Fu;   // CURSOR0_0_CURSOR_DST_OFFSET (0x34c0 + 0x67f)
constexpr uint32_t kRegCnvcCursorControl = 0x41B1u;   // CNVC_CUR0_CURSOR0_CONTROL (0x34c0 + 0xcf1)

// HUBPREQ0_CURSOR_SETTINGS: DST_Y_OFFSET = 0, CHUNK_HDL_ADJUST = 3 (Linux dcn32 default).
constexpr uint32_t kSettingsVal = 0x00000300u;
constexpr uint32_t kSize64x64   = (kMaxDim << 16) | kMaxDim;

// CURSOR0_0_CURSOR_CONTROL attribute bits: MODE = 2 (color pre-multiplied ARGB8888), PITCH = 0 (64 px),
// LINES_PER_CHUNK = 3 (8 lines per chunk, for 33..64 px width). Preserves unrelated bits including bit 0 (EN).
constexpr uint32_t hubpCtlAttrs(uint32_t oldCtl) {
    return (oldCtl & ~(0x00000700u | 0x00030000u | 0x1F000000u)) | (2u << 8) | (0u << 16) | (3u << 24);
}
constexpr uint32_t hubpCtlEnable(uint32_t oldCtl, bool en) {
    return (oldCtl & ~1u) | (en ? 1u : 0u);
}

// CNVC_CUR0_CURSOR0_CONTROL attribute bits: CUR0_MODE = 2 (ARGB pre-multiplied), EXPANSION_MODE = 0, ROM_EN = 0.
constexpr uint32_t cnvcCtlAttrs(uint32_t oldCnvc) {
    return (oldCnvc & ~(0x00000070u | 0x00000002u | 0x00000008u)) | (2u << 4);
}
constexpr uint32_t cnvcCtlEnable(uint32_t oldCnvc, bool en) {
    return (oldCnvc & ~1u) | (en ? 1u : 0u);
}

// IOFramebuffer::convertCursorImage packs rows tightly with stride = w pixels; the HUBP0 cursor plane reads pitch 64.
// Spread rows in place from bottom to top and zero-pad the unused tail of each row plus rows h..63.
inline bool spreadRows64(uint32_t *buf, uint32_t w, uint32_t h) {
    if (!buf || w == 0 || h == 0 || w > kMaxDim || h > kMaxDim) return false;
    if (w < kPitchPixels) {
        for (uint32_t r = h; r > 0; r--) {
            const uint32_t y = r - 1;
            const uint32_t *src = buf + y * w;
            uint32_t *dst = buf + y * kPitchPixels;
            for (uint32_t x = w; x > 0; x--) dst[x - 1] = src[x - 1];
            for (uint32_t x = w; x < kPitchPixels; x++) dst[x] = 0u;
        }
    }
    for (uint32_t y = h; y < kMaxDim; y++) {
        uint32_t *dst = buf + y * kPitchPixels;
        for (uint32_t x = 0; x < kPitchPixels; x++) dst[x] = 0u;
    }
    return true;
}

struct PosRegs {
    bool enabled;
    uint32_t pos;      // CURSOR_POSITION: (posX << 16) | posY
    uint32_t hot;      // CURSOR_HOT_SPOT: (hotX << 16) | hotY
    uint32_t dstX;     // CURSOR_DST_OFFSET: visible X scaled by HUBPREQ0_REF_FREQ_TO_PIX_FREQ (0.19 fixed point)
};

// Linux amdgpu_dm / hubp1_cursor_set_position coordinate rule:
//   - completely off-screen => disabled;
//   - partially off the left/top edge => clamp pos to 0 and shift hot-spot by -coord;
//   - dstX scaled from pixels to DCHUB refclks using the live 21-bit 0.19 fixed-point REF_FREQ_TO_PIX_FREQ register.
inline PosRegs computePos(int32_t x, int32_t y, uint32_t cw, uint32_t ch, uint32_t dispW, uint32_t dispH,
                          uint32_t refFreqToPix, bool visible) {
    PosRegs out{false, 0u, 0u, 0u};
    if (!visible || !cw || !ch || cw > kMaxDim || ch > kMaxDim || !dispW || !dispH) return out;
    const int64_t ix = x, iy = y, iw = cw, ih = ch, dw = dispW, dh = dispH;
    if (ix >= dw || ix + iw <= 0 || iy >= dh || iy + ih <= 0) return out;
    const uint32_t posX = x < 0 ? 0u : static_cast<uint32_t>(x);
    const uint32_t posY = y < 0 ? 0u : static_cast<uint32_t>(y);
    const uint32_t hotX = x < 0 ? static_cast<uint32_t>(-ix) : 0u;
    const uint32_t hotY = y < 0 ? static_cast<uint32_t>(-iy) : 0u;
    const uint32_t ratio = refFreqToPix & 0x1FFFFFu;
    out.enabled = true;
    out.pos = (posX << 16) | (posY & 0xFFFFu);
    out.hot = (hotX << 16) | (hotY & 0xFFFFu);
    out.dstX = static_cast<uint32_t>((static_cast<uint64_t>(posX) * ratio) >> 19);
    return out;
}

}  // namespace phxcursor

// Platform-function ABI between PhxAMDFB (VermilionAccel) and PhxBringup (VermilionBoot).
#define PHX_CURSOR_ABI_VERSION 1u
struct phx_cursor_image {
    uint32_t version;
    uint32_t width;        // 1..64
    uint32_t height;       // 1..64
    uint32_t disp_w;       // active raster width (0 = read HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION)
    uint32_t disp_h;       // active raster height
    const uint32_t *argb;  // 64x64 pitch-64 pre-multiplied ARGB8888 pixels
};
struct phx_cursor_state {
    uint32_t version;
    int32_t x;
    int32_t y;
    uint32_t visible;      // 0 = hide, 1 = show
    uint32_t disp_w;       // active raster width (0 = read HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION)
    uint32_t disp_h;       // active raster height
};
