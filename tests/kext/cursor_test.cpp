// SPDX-License-Identifier: MIT
// cursor_test.cpp: host tests for PhxCursor.h (DCN 3.2.0 HUBP0 hardware cursor pure logic).
#include "PhxCursor.h"

#include <cstdio>
#include <cstring>

static int fails;
static void ck(bool ok, const char *what) {
    if (!ok) { std::printf("  FAIL %s\n", what); fails++; }
}

int main() {
    using namespace phxcursor;

    // 1. Row spreading from tight w x h into pitch-64 (RedNavi's 32-px squashed-cursor fix).
    {
        uint32_t buf[kPitchPixels * kMaxDim];
        std::memset(buf, 0xAA, sizeof buf);
        for (uint32_t y = 0; y < 24; y++)
            for (uint32_t x = 0; x < 16; x++)
                buf[y * 16 + x] = 0xFF000000u | (y << 8) | x;
        ck(spreadRows64(buf, 16, 24), "spreadRows64(16x24) succeeds");
        for (uint32_t y = 0; y < kMaxDim; y++) {
            for (uint32_t x = 0; x < kPitchPixels; x++) {
                const uint32_t want = (y < 24 && x < 16) ? (0xFF000000u | (y << 8) | x) : 0u;
                if (buf[y * kPitchPixels + x] != want) {
                    ck(false, "16x24 pixel placement and zero padding exact");
                    y = kMaxDim;
                    break;
                }
            }
        }
        ck(!spreadRows64(buf, 0, 16) && !spreadRows64(buf, 65, 16) && !spreadRows64(nullptr, 16, 16),
           "invalid dimensions rejected");
    }

    // 2. Full 64x64 image preserved intact.
    {
        uint32_t buf[kPitchPixels * kMaxDim];
        for (uint32_t i = 0; i < kPitchPixels * kMaxDim; i++) buf[i] = 0x80000000u | i;
        ck(spreadRows64(buf, 64, 64), "spreadRows64(64x64) succeeds");
        bool ok = true;
        for (uint32_t i = 0; i < kPitchPixels * kMaxDim; i++) if (buf[i] != (0x80000000u | i)) { ok = false; break; }
        ck(ok, "64x64 pixels untouched");
    }

    // 3. Position + hotspot clamping + fixed-point dstX at real Navi 32 captures:
    //    1440p60: REF_FREQ_TO_PIX_FREQ = 0x0001a7aa (50 MHz / 241.70 MHz * 2^19)
    //    1440p144: REF_FREQ_TO_PIX_FREQ = 0x0000ab70 (50 MHz / 597.29 MHz * 2^19)
    {
        const PosRegs p60 = computePos(1000, 500, 32, 32, 2560, 1440, 0x0001a7aau, true);
        ck(p60.enabled && p60.pos == ((1000u << 16) | 500u) && p60.hot == 0u, "interior position enabled with zero hotspot");
        // 1000 * 50000 / 241700 = 206.868 -> 206
        ck(p60.dstX == 206u, "1440p60 dstX matches 50MHz / 241.7MHz");

        const PosRegs p144 = computePos(1000, 500, 32, 32, 2560, 1440, 0x0000ab70u, true);
        // 1000 * 50000 / 597290 = 83.711 -> 83
        ck(p144.enabled && p144.dstX == 83u, "1440p144 dstX matches 50MHz / 597.29MHz");

        // Partially off left/top edge: pos clamps to 0, hotspot moves to -coord, dstX = 0.
        const PosRegs neg = computePos(-10, -5, 32, 32, 2560, 1440, 0x0001a7aau, true);
        ck(neg.enabled && neg.pos == 0u && neg.hot == ((10u << 16) | 5u) && neg.dstX == 0u,
           "negative edge shifts hotspot and clamps pos to 0");

        // Completely off every edge => disabled.
        ck(!computePos(-32, 100, 32, 32, 2560, 1440, 0x0001a7aau, true).enabled, "off-left disabled");
        ck(!computePos(100, -32, 32, 32, 2560, 1440, 0x0001a7aau, true).enabled, "off-top disabled");
        ck(!computePos(2560, 100, 32, 32, 2560, 1440, 0x0001a7aau, true).enabled, "off-right disabled");
        ck(!computePos(100, 1440, 32, 32, 2560, 1440, 0x0001a7aau, true).enabled, "off-bottom disabled");
        ck(!computePos(100, 100, 32, 32, 2560, 1440, 0x0001a7aau, false).enabled, "invisible flag disables");
    }

    // 4. Register bitfield encoding preserves unrelated bits.
    {
        const uint32_t h = hubpCtlAttrs(0x80000001u);
        ck((h & 0x80000001u) == 0x80000001u, "hubpCtlAttrs preserves unrelated bits + EN");
        ck(((h >> 8) & 7u) == 2u && ((h >> 16) & 3u) == 0u && ((h >> 24) & 31u) == 3u,
           "hubpCtlAttrs sets ARGB premult / pitch 64 / 8 lines per chunk");
        ck((hubpCtlEnable(h, false) & 1u) == 0u && (hubpCtlEnable(h, true) & 1u) == 1u, "hubpCtlEnable toggles bit 0");

        const uint32_t c = cnvcCtlAttrs(0x0000010Fu);
        ck((c & 0x101u) == 0x101u && ((c >> 4) & 7u) == 2u && (c & 0xAu) == 0u,
           "cnvcCtlAttrs sets CUR0_MODE=2 and clears EXPANSION/ROM_EN");
        ck((cnvcCtlEnable(c, false) & 1u) == 0u && (cnvcCtlEnable(c, true) & 1u) == 1u, "cnvcCtlEnable toggles bit 0");
    }

    std::printf("cursor_test: 14/14 checks, %d failed\n", fails);
    return fails ? 1 : 0;
}
