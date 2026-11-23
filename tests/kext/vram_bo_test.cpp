// SPDX-License-Identifier: MIT
// vram_bo_test.cpp: host test of src/vermilion-boot/PhxVramPool.h (the VRAM BO pool) and PhxCpDma.h (the clear's
// DMA_DATA fill packets), kext b156.
//   c++ -std=c++17 -Wall -Wextra -Werror -I src/vermilion-boot -o /tmp/vram_bo_test tests/kext/vram_bo_test.cpp && /tmp/vram_bo_test
#include "PhxVramPool.h"
#include "PhxCpDma.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

using namespace phxvram;
static constexpr uint64_t MiB = 1ull << 20;

static bool sorted(const Pool &p) {
    for (uint32_t i = 0; i < p.n; i++) {
        if (p.ext[i].off >= p.ext[i].end) return false;
        if (i && p.ext[i - 1].end >= p.ext[i].off) return false;   // merged: never touching
    }
    return true;
}

static void poolBasics() {
    static Pool p;
    CHECK(!init(p, 512 * MiB, 513 * MiB));                     // rounds inwards to nothing
    CHECK(init(p, 511 * MiB, 24448 * MiB + 5));                // base up to 512, end down to 24448
    CHECK(p.base == 512 * MiB && p.end == 24448 * MiB && p.n == 1);
    CHECK(freeBytes(p) == (24448 - 512) * MiB);
    const uint64_t a = alloc(p, 4096);                          // rounds to 64 KiB
    CHECK(a == 512 * MiB && p.used == kGranule);
    const uint64_t b = alloc(p, 3 * MiB);                       // >= 2 MiB: 2 MiB aligned, so past a's 64 KiB
    CHECK(b == 514 * MiB && b % kFragment == 0);
    CHECK(p.n == 2 && sorted(p));                               // the hole [512M+64K, 514M) and the rest
    const uint64_t c = alloc(p, 64 << 10);                      // small: fills the hole first (first fit)
    CHECK(c == 512 * MiB + kGranule);
    CHECK(release(p, b, 3 * MiB) && release(p, a, 4096) && release(p, c, 64 << 10));
    CHECK(p.n == 1 && p.used == 0 && freeBytes(p) == (24448 - 512) * MiB && sorted(p));
    CHECK(p.allocs == 3 && p.frees == 3);
    CHECK(!release(p, 600 * MiB, 4096));                        // free space: refused
    CHECK(!release(p, 100 * MiB, 4096));                        // outside the pool
    CHECK(!release(p, 0, 0));
    CHECK(alloc(p, 0) == 0 && alloc(p, 30000 * MiB) == 0);
    const uint64_t all = alloc(p, (24448 - 512) * MiB);         // exactly the pool
    CHECK(all == 512 * MiB && p.n == 0 && alloc(p, 4096) == 0);
    CHECK(release(p, all, (24448 - 512) * MiB) && p.n == 1);
}

static void poolFullArray() {
    // a full extent array, built by hand: kMaxExtents holes of 4 MiB, each starting 64 KiB past a 2 MiB boundary
    static Pool p;
    CHECK(init(p, 512 * MiB, 512 * MiB + kMaxExtents * 8 * MiB));
    for (uint32_t i = 0; i < kMaxExtents; i++)
        p.ext[i] = Extent{512 * MiB + i * 8 * MiB + kGranule, 512 * MiB + i * 8 * MiB + 4 * MiB + kGranule};
    p.n = kMaxExtents;
    p.used = (p.end - p.base) - freeBytes(p);
    CHECK(sorted(p));
    const uint64_t f0 = p.fails;
    CHECK(alloc(p, 2 * MiB) == 0 && p.fails == f0 + 1);        // every hole would split in two: refused
    CHECK(alloc(p, 64 << 10) == 512 * MiB + kGranule);          // at a hole's start: no split
    CHECK(p.n == kMaxExtents);
    const uint64_t u = p.used;
    CHECK(!release(p, 512 * MiB + 6 * MiB, 64 << 10));          // touches no free extent, no slot: lost, counted
    CHECK(p.lost == kGranule && p.used == u - kGranule);
    CHECK(release(p, 512 * MiB + kGranule, 64 << 10));          // merges back into hole 0
    CHECK(p.n == kMaxExtents && sorted(p) && p.ext[0].off == 512 * MiB + kGranule);
}

static void poolFuzz() {
    static Pool p;
    const uint64_t base = 512 * MiB, end = 1536 * MiB;
    CHECK(init(p, base, end));
    std::vector<uint8_t> owner((end - base) / kGranule, 0);     // model: granule -> live
    struct L { uint64_t off, bytes; };
    std::vector<L> live;
    std::srand(12345);
    int bad = 0;
    for (int it = 0; it < 200000; it++) {
        if (live.empty() || (std::rand() % 100) < 55) {
            const uint64_t bytes = (std::rand() % 4) == 0 ? (1 + std::rand() % 64) * MiB : 1 + std::rand() % (1 << 20);
            const uint64_t o = alloc(p, bytes);
            if (!o) continue;
            const uint64_t sz = sizeFor(bytes);
            if (o % alignFor(sz) || o < base || o + sz > end) bad++;
            for (uint64_t g = (o - base) / kGranule; g < (o - base + sz) / kGranule; g++) { if (owner[g]) bad++; owner[g] = 1; }
            live.push_back({o, bytes});
        } else {
            const size_t k = std::rand() % live.size();
            const L l = live[k];
            live[k] = live.back(); live.pop_back();
            if (!release(p, l.off, l.bytes)) bad++;
            for (uint64_t g = (l.off - base) / kGranule; g < (l.off - base + sizeFor(l.bytes)) / kGranule; g++) owner[g] = 0;
        }
        if ((it & 1023) == 0) {
            uint64_t used = 0;
            for (uint8_t x : owner) used += x;
            if (used * kGranule != p.used || freeBytes(p) + p.used != end - base || !sorted(p)) bad++;
        }
    }
    CHECK(bad == 0);
    for (const L &l : live) CHECK(release(p, l.off, l.bytes));
    CHECK(p.n == 1 && p.used == 0 && p.lost == 0);
}

static void cpdma() {
    using namespace phxcpdma;
    CHECK(kMaxBytes == 32736);
    uint32_t ib[64] = {0};
    const uint64_t mc = 0x8000000000ull + 147 * MiB;
    CHECK(fill(ib, 64, mc, 4096, 0, true) == 7);
    CHECK(ib[0] == 0xC0055000u && ib[1] == 0xC0300000u && ib[2] == 0 && ib[3] == 0);   // last packet: CP_SYNC on gfx
    CHECK(ib[4] == static_cast<uint32_t>(mc) && ib[5] == 0x80u && ib[6] == 4096);
    CHECK(fill(ib, 64, mc, 4096, 0x5a5a5a5au, false) == 7 && ib[1] == 0x40300000u && ib[2] == 0x5a5a5a5au);   // MEC: no CP_SYNC
    const uint64_t big = 3ull * kMaxBytes + 100;
    CHECK(dwordsFor(big) == 28);
    CHECK(fill(ib, 27, mc, big, 0, true) == 0);                 // too small a buffer
    CHECK(fill(ib, 64, mc, big, 0, true) == 28);
    CHECK(ib[1] == 0x40300000u && ib[8] == 0x40300000u && ib[15] == 0x40300000u && ib[22] == 0xC0300000u);
    CHECK(ib[6] == kMaxBytes && ib[13] == kMaxBytes && ib[20] == kMaxBytes && ib[27] == 100);
    CHECK(ib[4] == static_cast<uint32_t>(mc) && ib[11] == static_cast<uint32_t>(mc + kMaxBytes) && ib[25] == static_cast<uint32_t>(mc + 3ull * kMaxBytes));
    CHECK(fill(ib, 64, mc, 6, 0, true) == 0 && fill(ib, 64, mc + 2, 8, 0, true) == 0 && fill(ib, 64, mc, 0, 0, true) == 0);
    // 1 GiB in one IB: the dwords fit the 2 MiB clear-IB area (PHX_VRAM_CLEARIB_SIZE)
    CHECK(dwordsFor(1ull << 30) * 4 <= (2ull << 20));
}

int main() {
    poolBasics();
    poolFullArray();
    poolFuzz();
    cpdma();
    std::printf("vram_bo_test: %d/%d passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
