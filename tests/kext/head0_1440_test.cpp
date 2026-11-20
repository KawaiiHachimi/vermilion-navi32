// SPDX-License-Identifier: MIT
// head0_1440_test.cpp: host test of src/vermilion-accel/accel/phxhead.h's head-0 mode list for the XTX machine's 1440p
// monitor (b128 rows 3-5) next to upstream's AOC rows 1-2.
//   c++ -std=c++17 -Wall -Wextra -Werror -I src/vermilion-accel/accel -o /tmp/head0_1440_test tests/kext/head0_1440_test.cpp && /tmp/head0_1440_test
#include "phxhead.h"
#include <cstdio>
#include <cstring>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

// The LG ULTRAGEAR+ EDID blocks 0-1 as macOS reads them on the XTX machine (2026-11-20), with its serial number fields
// cleared (ID serial bytes 12-15 = 0, the 0xFF descriptor empty, checksums recomputed: tools/capture strip_edid). A generic
// example of that model, not anyone's monitor.
static const uint8_t kLgEdid[256] = {
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x1e, 0x6d, 0x7c, 0x5c, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x22, 0x01, 0x04, 0xc5, 0x3b, 0x21, 0x78, 0xf9, 0x5d, 0xa5, 0xad, 0x52, 0x3f, 0xaf, 0x25,
    0x0e, 0x50, 0x54, 0x21, 0x09, 0x00, 0xd1, 0xc0, 0x61, 0x40, 0x45, 0x40, 0x31, 0x40, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x56, 0x5e, 0x00, 0xa0, 0xa0, 0xa0, 0x29, 0x50, 0x30, 0x20,
    0x35, 0x00, 0x4e, 0x4d, 0x21, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0xfd, 0x0c, 0x30, 0xf0, 0x87,
    0x87, 0x6a, 0x01, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x4c,
    0x47, 0x20, 0x55, 0x4c, 0x54, 0x52, 0x41, 0x47, 0x45, 0x41, 0x52, 0x2b, 0x00, 0x00, 0x00, 0xff,
    0x00, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x02, 0xe7,
    0x02, 0x03, 0x31, 0x73, 0x23, 0x0f, 0x57, 0x07, 0x83, 0x01, 0x00, 0x00, 0x41, 0x3f, 0xe2, 0x00,
    0x6a, 0xe3, 0x05, 0xc0, 0x00, 0xe6, 0x06, 0x05, 0x01, 0x73, 0x4f, 0x01, 0x74, 0x1a, 0x00, 0x00,
    0x03, 0x03, 0x30, 0xf0, 0x00, 0xa0, 0x73, 0x01, 0x4f, 0x02, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xef, 0xe7, 0x00, 0xa0, 0xa0, 0xa0, 0x4c, 0x50, 0x30, 0x20, 0x35, 0x00, 0x4e, 0x4d, 0x21,
    0x00, 0x00, 0x1a, 0x6f, 0xc2, 0x00, 0xa0, 0xa0, 0xa0, 0x55, 0x50, 0x30, 0x20, 0x35, 0x00, 0x4e,
    0x4d, 0x21, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53,
};

int main() {
    using namespace phxhead;
    uint32_t ids[kNMaxModes];
    // the LG at 2560x1440: its three DTDs, in table order
    const unsigned n = modeList(0, kLgEdid, sizeof kLgEdid, 2560, 1440, ids);
    CHECK(n == 3 && ids[0] == 3 && ids[1] == 4 && ids[2] == 5);
    CHECK(modeForTotals(0, 2720, 1481, ids, n) == 3 && modeForTotals(0, 2720, 1525, ids, n) == 4 && modeForTotals(0, 2720, 1516, ids, n) == 5);
    CHECK(modeForTotals(0, 2200, 1125, ids, n) == 0);
    // the rate phxPipe0SetMode gets: 60 / 120 / 144 (59.951 Hz was 59 = refused before b128)
    CHECK(switchHz(*headMode(0, 3)) == 60 && switchHz(*headMode(0, 4)) == 120 && switchHz(*headMode(0, 5)) == 144);
    CHECK(switchHz(*headMode(0, 1)) == 60 && switchHz(*headMode(0, 2)) == 144);   // the AOC: unchanged
    // the measured-rate fallback finds them too
    CHECK(modeFor(0, 59950, ids, n) == 3 && modeFor(0, 120010, ids, n) == 4 && modeFor(0, 143900, ids, n) == 5);
    // detailed timings as IOGraphicsLib decodes the LG's DTDs: 497.75 MHz, blanking 160 / 85, sync H+ V-
    DetailedTiming t{};
    CHECK(detailedTiming(0, 4, &t) && t.pixelHz == 497750000ull && t.hBlank == 160 && t.vBlank == 85 && t.hSyncOffset == 48 &&
          t.hSyncWidth == 32 && t.vSyncOffset == 3 && t.vSyncWidth == 5 && t.hSyncPos && !t.vSyncPos);
    CHECK(detailedTiming(0, 3, &t) && t.pixelHz == 241500000ull && t.vBlank == 41);
    CHECK(detailedTiming(0, 5, &t) && t.pixelHz == 593750000ull && t.vBlank == 76);
    // frame periods for the VBL tick
    CHECK(modeFrameNs(*headMode(0, 4)) / 1000 == 8333 && modeFrameNs(*headMode(0, 5)) / 1000 == 6944);
    // the AOC's EDID at 1920x1080 still lists exactly its two modes; the LG offers nothing at 1080p and the AOC nothing at 1440p
    CHECK(modeList(0, kAocEdid, sizeof kAocEdid, 1920, 1080, ids) == 2 && ids[0] == 1 && ids[1] == 2);
    CHECK(modeList(0, kLgEdid, sizeof kLgEdid, 1920, 1080, ids) == 0);
    CHECK(modeList(0, kAocEdid, sizeof kAocEdid, 2560, 1440, ids) == 0);
    // only the first 128 bytes: the 120 / 144 DTDs are in the CTA block, so just the 60 Hz mode (no list: n < 2)
    CHECK(modeList(0, kLgEdid, 128, 2560, 1440, ids) == 1);
    // head 1 never gets the 1440p rows
    CHECK(modeList(1, kLgEdid, sizeof kLgEdid, 2560, 1440, ids) == 0);
    // b134: the full EDID (block 2 = the LG's DisplayID 1.2 block, read on the box 2026-11-20 through phxedid_full; it has no
    // product-ID block, so no serial) adds the 240 Hz DisplayID Type I timing; 256 bytes alone do not
    {
        static const uint8_t kBlk2[128] = {0x70,0x12,0x79,0x03,0x00,0x01,0x00,0x0c,0x10,0x17,0x09,0x0d,0x00,0x0a,0xa0,0x05,0x90,0x78,0x4e,0xbb,0x0f,0x00,0x0a,0xa4,0x14,0x0e,0x0e,0x07,0x01,0x23,0x00,0x00,0x00,0x03,0x01,0x14,0xca,0x9c,0x01,0x84,0xff,0x09,0x9f,0x00,0x2f,0x80,0x1f,0x00,0x9f,0x05,0xb2,0x00,0x02,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7c,0x90};
        uint8_t full[384];
        std::memcpy(full, kLgEdid, 256); std::memcpy(full + 256, kBlk2, 128);
        const unsigned n4 = modeList(0, full, sizeof full, 2560, 1440, ids);
        CHECK(n4 == 4 && ids[3] == 6);
        CHECK(modeForTotals(0, 2720, 1619, ids, n4) == 6 && switchHz(*headMode(0, 6)) == 240);
        CHECK(detailedTiming(0, 6, &t) && t.pixelHz == 1056750000ull && t.hBlank == 160 && t.vBlank == 179 && t.hSyncPos && !t.vSyncPos);
        CHECK(modeFor(0, 239960, ids, n4) == 6 && modeFrameNs(*headMode(0, 6)) / 1000 == 4167);
        CHECK(modeList(0, kLgEdid, sizeof kLgEdid, 2560, 1440, ids) == 3);   // without block 2: no 240
        uint8_t bad[384]; std::memcpy(bad, full, 384); bad[256 + 40] ^= 1;    // another active width: not offered
        CHECK(modeList(0, bad, sizeof bad, 2560, 1440, ids) == 3);
    }
    // b138: the anySize offer (PhxBringup's phx-p0-keys) on the LG's 2560x1440 surface adds the EDID's established /
    // standard timings and VIC 63 (capture v3 edid: est 640x480 / 800x600 / 1024x768 / 1280x1024@75, std 1920x1080@60,
    // 1024x768, 800x600, 640x480, CTA VIC 63); the XT's lists and the plain offer stay as they were
    {
        const unsigned na = modeList(0, kLgEdid, sizeof kLgEdid, 2560, 1440, ids, true);
        const uint32_t want[] = {1, 3, 4, 5, 7, 8, 9, 10, 11, 12};
        bool same = na == sizeof want / sizeof want[0];
        for (unsigned i = 0; same && i < na; i++) same = ids[i] == want[i];
        CHECK(same);
        if (!same) { std::printf("  anySize:"); for (unsigned i = 0; i < na; i++) std::printf(" %u", ids[i]); std::printf("\n"); }
        CHECK(modeList(0, kAocEdid, sizeof kAocEdid, 1920, 1080, ids, true) == 2);   // anySize only acts on a 2560x1440 surface
        CHECK(modeList(1, kLgEdid, sizeof kLgEdid, 2560, 1440, ids, true) == 0);
        uint8_t noStd[256]; std::memcpy(noStd, kLgEdid, 256);
        for (unsigned o = 35; o < 54; o++) noStd[o] = o < 38 ? 0 : 1;   // no established / standard timings
        const unsigned nn = modeList(0, noStd, sizeof noStd, 2560, 1440, ids, true);
        CHECK(nn == 4 && ids[3] == 7);   // the 1440p DTDs and VIC 63 only
        // the keys PhxBringup b137 takes, the rows' pitch, and the totals shared by two rates
        CHECK(switchKey(*headMode(0, 3), 2560, 1440) == 60 && switchKey(*headMode(0, 6), 2560, 1440) == 240);
        CHECK(switchKey(*headMode(0, 1), 2560, 1440) == 1080060 && switchKey(*headMode(0, 7), 2560, 1440) == 1080120);
        CHECK(switchKey(*headMode(0, 8), 2560, 1440) == 1024075 && switchKey(*headMode(0, 9), 2560, 1440) == 768060);
        CHECK(switchKey(*headMode(0, 10), 2560, 1440) == 600060 && switchKey(*headMode(0, 11), 2560, 1440) == 480060);
        CHECK(switchKey(*headMode(0, 12), 2560, 1440) == 480059 && switchKey(*headMode(0, 1), 1920, 1080) == 60);   // the XT: Hz
        CHECK(modeRowBytes(*headMode(0, 3), 2560, 1440, 10240) == 10240 && modeRowBytes(*headMode(0, 1), 2560, 1440, 10240) == 8192);
        CHECK(modeRowBytes(*headMode(0, 8), 2560, 1440, 10240) == 5120 && modeRowBytes(*headMode(0, 10), 2560, 1440, 10240) == 4096);
        CHECK(modeRowBytes(*headMode(0, 12), 2560, 1440, 10240) == 3072 && modeRowBytes(*headMode(0, 1), 1920, 1080, 7680) == 7680);
        const unsigned nb = modeList(0, kLgEdid, sizeof kLgEdid, 2560, 1440, ids, true);
        CHECK(modeForTotalsHz(0, 2200, 1125, 119990, ids, nb) == 7 && modeForTotalsHz(0, 2200, 1125, 60010, ids, nb) == 1);
        CHECK(modeForTotalsHz(0, 800, 525, 59940, ids, nb) == 12 && modeForTotalsHz(0, 800, 525, 60000, ids, nb) == 11);
        CHECK(modeForTotalsHz(0, 2720, 1516, 143000, ids, nb) == 5 && modeForTotalsHz(0, 1, 1, 60000, ids, nb) == 0);
        DetailedTiming t8{};
        CHECK(detailedTiming(0, 8, &t8) && t8.pixelHz == 135000000ull && t8.hBlank == 408 && t8.vBlank == 42 && t8.hSyncPos && t8.vSyncPos);
        CHECK(detailedTiming(0, 9, &t8) && t8.hSyncWidth == 136 && !t8.hSyncPos && !t8.vSyncPos);
    }
    std::printf("head0_1440_test: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
