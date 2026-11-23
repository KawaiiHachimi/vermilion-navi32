// SPDX-License-Identifier: MIT
// PhxVramPool.h: the range allocator over the client-BO part of VRAM (b156, thread 02 part B: VRAM-backed BOs).
//
// Kernel-free on purpose: tests/kext/vram_bo_test.cpp runs this exact code on a host. The caller serializes every
// call (PhxBringup::vramLock). Metadata lives here, never in VRAM.
//
// The pool is [base, end) in VRAM byte offsets (PhxNativeABI.h: PHX_VRAM_POOL_BASE, and an end below the firmware at
// the top of VRAM). Sizes round up to 64 KiB; a BO of 2 MiB or more starts on a 2 MiB boundary (amdgpu's PTE fragment
// size on this card), a smaller one on 64 KiB. Free space is a sorted array of extents, first fit, merged on free.
// A full extent array makes an alloc that would split an extent fail (the caller falls back to system memory, as
// amdgpu falls back to GTT); a free that can merge with neither neighbour into a full array is lost and counted.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace phxvram {

constexpr uint64_t kGranule = 64ull << 10, kFragment = 2ull << 20;
constexpr uint32_t kMaxExtents = 2048;

struct Extent { uint64_t off, end; };

struct Pool {
    uint64_t base{0}, end{0};
    uint32_t n{0};
    Extent ext[kMaxExtents];
    uint64_t used{0}, peak{0}, allocs{0}, frees{0}, fails{0}, lost{0};
};

constexpr uint64_t roundUp(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }
constexpr uint64_t sizeFor(uint64_t bytes) { return roundUp(bytes, kGranule); }
constexpr uint64_t alignFor(uint64_t size) { return size >= kFragment ? kFragment : kGranule; }

// base and end are rounded inwards to 2 MiB; returns false (an empty pool) when nothing is left
inline bool init(Pool &p, uint64_t base, uint64_t end) {
    p = Pool{};
    base = roundUp(base, kFragment);
    end &= ~(kFragment - 1);
    if (end <= base)
        return false;
    p.base = base;
    p.end = end;
    p.ext[0] = Extent{base, end};
    p.n = 1;
    return true;
}

inline uint64_t freeBytes(const Pool &p) {
    uint64_t s = 0;
    for (uint32_t i = 0; i < p.n; i++)
        s += p.ext[i].end - p.ext[i].off;
    return s;
}

inline uint64_t largest(const Pool &p) {
    uint64_t m = 0;
    for (uint32_t i = 0; i < p.n; i++)
        if (p.ext[i].end - p.ext[i].off > m)
            m = p.ext[i].end - p.ext[i].off;
    return m;
}

// The VRAM offset of `bytes` (rounded with sizeFor), or 0: no extent fits, or the split needs a slot the array lacks.
inline uint64_t alloc(Pool &p, uint64_t bytes) {
    if (!bytes || bytes > p.end - p.base) {
        p.fails++;
        return 0;
    }
    const uint64_t size = sizeFor(bytes), align = alignFor(size);
    for (uint32_t i = 0; i < p.n; i++) {
        Extent &e = p.ext[i];
        const uint64_t a = roundUp(e.off, align);
        if (a < e.off || a + size > e.end)
            continue;
        const bool head = a > e.off, tail = a + size < e.end;
        if (head && tail) {   // the extent splits in two: one more slot
            if (p.n >= kMaxExtents)
                continue;
            for (uint32_t k = p.n; k > i + 1; k--)
                p.ext[k] = p.ext[k - 1];
            p.ext[i + 1] = Extent{a + size, e.end};
            e.end = a;
            p.n++;
        } else if (head) {
            e.end = a;
        } else if (tail) {
            e.off = a + size;
        } else {
            for (uint32_t k = i; k + 1 < p.n; k++)
                p.ext[k] = p.ext[k + 1];
            p.n--;
        }
        p.used += size;
        if (p.used > p.peak)
            p.peak = p.used;
        p.allocs++;
        return a;
    }
    p.fails++;
    return 0;
}

// Returns a range alloc() handed out (the same `bytes`). false: outside the pool or overlapping free space (a caller
// bug: nothing changes), or lost to a full array.
inline bool release(Pool &p, uint64_t off, uint64_t bytes) {
    const uint64_t size = sizeFor(bytes), end = off + size;
    if (!bytes || off < p.base || end > p.end || end < off)
        return false;
    uint32_t i = 0;   // the first extent after the range
    while (i < p.n && p.ext[i].off < end)
        i++;
    if ((i > 0 && p.ext[i - 1].end > off) || (i < p.n && p.ext[i].off < end))
        return false;
    const bool left = i > 0 && p.ext[i - 1].end == off, right = i < p.n && p.ext[i].off == end;
    if (left && right) {
        p.ext[i - 1].end = p.ext[i].end;
        for (uint32_t k = i; k + 1 < p.n; k++)
            p.ext[k] = p.ext[k + 1];
        p.n--;
    } else if (left) {
        p.ext[i - 1].end = end;
    } else if (right) {
        p.ext[i].off = off;
    } else {
        if (p.n >= kMaxExtents) {
            p.lost += size;
            p.used -= size;
            return false;
        }
        for (uint32_t k = p.n; k > i; k--)
            p.ext[k] = p.ext[k - 1];
        p.ext[i] = Extent{off, end};
        p.n++;
    }
    p.used -= size;
    p.frees++;
    return true;
}

} // namespace phxvram
