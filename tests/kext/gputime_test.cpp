// SPDX-License-Identifier: MIT
// gputime_test.cpp: host test of src/vermilion-boot/PhxGpuTime.h (GPU time per context, kext b157).
//   c++ -std=c++17 -Wall -Wextra -Werror -I src/vermilion-boot -o /tmp/gputime_test tests/kext/gputime_test.cpp && /tmp/gputime_test
#include "PhxGpuTime.h"
#include "PhxNativeABI.h"
#include <cstdio>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

using namespace phxgputime;

int main() {
    // packets: RADV's vkCmdWriteTimestamp2 on GFX11 (radv_query.c, ac_cmdbuf_cp.c, sid.h)
    uint32_t d[8] = {0};
    const uint64_t mc = 0x8000000000ull + 0x9700000ull + slotOff(1, 5);
    CHECK(emitStart(d, mc) == kStartDw && kStartDw == 6);
    CHECK(d[0] == 0xC0044000u && d[1] == 0x00110509u && d[2] == 0 && d[3] == 0);   // COPY_DATA, TIMESTAMP -> MEM, 64-bit, confirm
    CHECK(d[4] == static_cast<uint32_t>(mc) && d[5] == 0x80u);
    CHECK(emitEnd(d, mc + 8) == kEndDw && kEndDw == 8);
    CHECK(d[0] == 0xC0064900u && d[1] == 0x528u && d[2] == 0x63000000u);           // RELEASE_MEM BOTTOM_OF_PIPE_TS idx 5
    CHECK(d[3] == static_cast<uint32_t>(mc + 8) && d[4] == 0x80u && d[5] == 0 && d[6] == 0 && d[7] == 0);
    // slots: 3 queues x 64, 16 bytes each, wrapping with the history index
    CHECK(slotOff(0, 0) == 0 && slotOff(0, 63) == 63 * 16 && slotOff(0, 64) == 0 && slotOff(2, 1) == (128 + 1) * 16);
    CHECK(kAreaBytes == 3072 && kAreaBytes <= PHX_GPUTIME_SIZE);
    // accounting: in-order queue, overlap with the previous submission's tail is not counted twice
    uint64_t last = 0;
    CHECK(account(last, 1000, 1500) == 500 && last == 1500);
    CHECK(account(last, 1200, 1900) == 400 && last == 1900);   // started (top of pipe) while the previous one drained
    CHECK(account(last, 2500, 2600) == 100 && last == 2600);   // an idle gap is not busy
    CHECK(account(last, 2000, 2550) == 0 && last == 2600);     // entirely inside already-counted time
    CHECK(account(last, 0, 3000) == 0 && last == 2600);        // never stamped (slot cleared to 0)
    CHECK(account(last, 3000, 0) == 0 && account(last, 4000, 3500) == 0 && last == 2600);
    CHECK(ticksToUs(100) == 1 && ticksToUs(kHz) == 1000000);
    std::printf("gputime_test: %d/%d passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
