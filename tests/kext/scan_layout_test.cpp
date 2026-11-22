// SPDX-License-Identifier: MIT
// scan_layout_test.cpp: host test of src/vermilion-boot/PhxScanLayout.h (the GPU-present slot layout, kext b126).
//   c++ -std=c++17 -Wall -Wextra -Werror -I src/vermilion-boot -o /tmp/scan_layout_test tests/kext/scan_layout_test.cpp && /tmp/scan_layout_test
#include "PhxScanLayout.h"
#include "PhxNativeABI.h"
#include <cstdio>

using namespace phxscanlay;

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static constexpr uint64_t kXtVram = 20464ull << 20;    // RX 7900 XT, 20 GiB card (MMHUB FB location)
static constexpr uint64_t kXtxVram = 24560ull << 20;   // RX 7900 XTX: VRAM 0x80_0000_0000 - 0x85_feff_ffff (Linux, 2026-11-19)
static constexpr uint32_t kBase = 0x8000, kXtxTop = 0x85ff;   // DCN_VM_FB_LOCATION_BASE / _TOP as Linux programs them on the XTX
static uint64_t frame(uint32_t w, uint32_t h) { return static_cast<uint64_t>(w) * 4 * h; }   // pitch = w px (GOP linear)

int main() {
    Layout l{};
    // the small layout is exactly the b44..b125 one (PhxNativeABI.h)
    CHECK(kSmallBase == PHXN_SCAN_SLOT_VRAM(0) && kSmallStride == PHXN_SCAN_SLOT_BYTES);
    for (uint32_t k = 0; k < kSlots; k++) CHECK(slotOff(Layout{kSmallBase, kSmallStride, kSmall}, k) == PHXN_SCAN_SLOT_VRAM(k));
    CHECK(PHXN_SCAN_MAX_SLOTS == kSlots);

    // upstream XT, two 1080p monitors: small, whatever the DCN window or VRAM reads (b125 checked neither)
    CHECK(choose(frame(1920, 1080), kXtVram, kBase, 0x84ff, &l) == kSmall && l.base == 24 * kMiB && l.stride == 8 * kMiB);
    CHECK(choose(frame(1920, 1080), 0, 0, 0, &l) == kSmall);
    CHECK(choose(frame(1920, 1080), kXtVram, kBase, kBase, &l) == kSmall);   // a 16 MiB DCN window: still small, as before
    CHECK(choose(kSmallStride, kXtxVram, kBase, kXtxTop, &l) == kSmall);     // exactly 8 MiB: small
    CHECK(choose(frame(1680, 1050), kXtVram, kBase, 0x84ff, nullptr) == kSmall);

    // the XTX's 2560x1440 monitor: large at 256/288/320 MiB
    CHECK(frame(2560, 1440) == 14745600ull);
    CHECK(choose(frame(2560, 1440), kXtxVram, kBase, kXtxTop, &l) == kLarge);
    CHECK(l.kind == kLarge && slotOff(l, 0) == 256 * kMiB && slotOff(l, 1) == 288 * kMiB && slotOff(l, 2) == 320 * kMiB);
    CHECK(frame(2560, 1440) <= l.stride);
    CHECK(choose(kSmallStride + 1, kXtxVram, kBase, kXtxTop, &l) == kLarge);   // just over 8 MiB
    CHECK(choose(frame(2560, 1600), kXtxVram, kBase, kXtxTop, &l) == kLarge);
    CHECK(choose(frame(3440, 1440), kXtxVram, kBase, kXtxTop, &l) == kLarge);
    CHECK(choose(frame(3840, 2160), kXtxVram, kBase, kXtxTop, &l) == kLarge);   // 31.6 MiB
    CHECK(choose(kLargeStride, kXtxVram, kBase, kXtxTop, &l) == kLarge);        // exactly 32 MiB
    CHECK(choose(frame(2560, 1440), kXtVram, kBase, 0x84ff, &l) == kLarge);     // an XT with a 1440p monitor (unverified)

    // the large slots stay clear of every fixed region (phxhead.h / PhxNativeABI.h map) and inside a 20 GiB card
    CHECK(kLargeBase >= PHX_VRAM_PT_OFF + PHX_VRAM_PT_SIZE);
    CHECK(kLargeBase >= 256 * kMiB);   // past BAR0 as macOS sizes it, so past mesboot's GART window too
    CHECK(kLargeEnd == 352 * kMiB && kLargeEnd < kXtVram);

    // refusals: nothing is chosen, *out untouched
    l = Layout{1, 2, 7};
    CHECK(choose(0, kXtxVram, kBase, kXtxTop, &l) == kErrEmpty);
    CHECK(choose(kLargeStride + 1, kXtxVram, kBase, kXtxTop, &l) == kErrTooBig);
    CHECK(choose(frame(5120, 2880), kXtxVram, kBase, kXtxTop, &l) == kErrTooBig);
    CHECK(choose(frame(2560, 1440), 0, kBase, kXtxTop, &l) == kErrVram);
    CHECK(choose(frame(2560, 1440), kLargeEnd - 1, kBase, kXtxTop, &l) == kErrVram);
    CHECK(choose(frame(2560, 1440), kLargeEnd, kBase, kBase + 21, &l) == kLarge);     // VRAM and window 352 MiB: exactly enough
    l = Layout{1, 2, 7};
    CHECK(choose(frame(2560, 1440), kXtxVram, kBase, kBase + 20, &l) == kErrDcn);     // window 336 MiB
    CHECK(choose(frame(2560, 1440), kXtxVram, kBase, kBase, &l) == kErrDcn);          // window 16 MiB (the GOP's console only)
    CHECK(choose(frame(2560, 1440), kXtxVram, kBase, kBase - 1, &l) == kErrDcn);      // top < base: window off
    CHECK(choose(frame(2560, 1440), kXtxVram, 0, 0xffffff, &l) == kLarge);            // a full 24-bit window
    CHECK(choose(frame(2560, 1440), kXtxVram, 0xff008000, 0xff0085ff, &l) == kLarge); // bits above 23 ignored
    CHECK(l.kind == kLarge);
    l = Layout{1, 2, 7};
    CHECK(choose(frame(2560, 1440), kXtxVram, kBase, kBase + 20, &l) < 0 && l.base == 1 && l.stride == 2 && l.kind == 7);

    // names for the log
    for (int r = kErrDcn; r <= kLarge; r++) CHECK(name(r)[0] != '?');
    CHECK(name(99)[0] == '?');

    std::printf("scan_layout_test: %d/%d passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
