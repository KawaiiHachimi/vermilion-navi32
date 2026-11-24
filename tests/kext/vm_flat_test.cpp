// SPDX-License-Identifier: MIT
// vm_flat_test.cpp: host test of the client page-table path in src/vermilion-boot/PhxVmTables.h.
//
//   c++ -std=c++17 -Wall -Wextra -I src/vermilion-boot -o /tmp/vm_flat_test tests/kext/vm_flat_test.cpp \
//     && /tmp/vm_flat_test
//
// What it guards: on Navi 32 a client context uses ONE flat depth-0 table indexed by va>>12 (see the note on
// Ctx::flat: the GCVM four-level walk does not translate for a client VMID on this die, measured). That index
// arithmetic and the bounds check are the difference between every client address translating and none, and they
// are pure host code, so they are tested here rather than on the card.
#include "PhxVmTables.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace phxvm;

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

// The walker takes an E for its hooks; a flat context never calls any of them, which is part of what is checked.
struct Env {
    // The walker's templates instantiate every branch, so this has to accept the volatile pointer that
    // takeParked() passes; a flat context never reaches that path.
    void flush(volatile uint64_t *p) { (void)p; std::printf("FAIL flush called on a flat context\n"); fails++; }
    void event(Ev) {}
    uint64_t tablePhys(Ctx &, uint32_t) { return 0; }
    // takeParked() needs these two even on a flat context: the templates instantiate the whole walker.
    int mode() const { return 0; }
    uint64_t retired() const { return 0; }
    uint64_t poolPhys = 0;
    uint8_t *poolCpu = nullptr;
    Shadow *sh = nullptr;
};

int main() {
    // A flat table covering 4 pages, as phx_vm.cpp lays one out for a client VMID.
    const uint32_t pages = 4;
    std::vector<uint64_t> tbl(pages, 0);
    Ctx c{};
    c.flat = true;
    c.cpu = reinterpret_cast<uint8_t *>(tbl.data());
    c.flatPages = pages;
    Env e;

    // Entry i is table[i], for the four pages the table covers.
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t g = kNil;
        uint64_t *p = pte(e, c, static_cast<uint64_t>(i) << 12, false, &g);
        CHECK(p == &tbl[i]);
        CHECK(g == kNil);          // a flat context has no pool table to report
    }

    // One past the end, and far past it, are refused rather than reading off the table.
    CHECK(pte(e, c, static_cast<uint64_t>(pages) << 12, false, nullptr) == nullptr);
    CHECK(pte(e, c, 0x7ffffff000ull, false, nullptr) == nullptr);
    CHECK(pte(e, c, 0xffffffffff000ull, false, nullptr) == nullptr);

    // The index is va >> 12 exactly: the low 12 bits of the VA never change which entry is addressed.
    uint64_t *a = pte(e, c, 0x1000, false, nullptr);
    uint64_t *b = pte(e, c, 0x1fff, false, nullptr);
    CHECK(a && b && a == b);
    CHECK(pte(e, c, 0x2000, false, nullptr) != a);

    // A context with no table yet (cpu == nullptr) has no entries at all - the state a client VMID is in before
    // its table is mapped into BAR0.
    Ctx none{};
    none.flat = true;
    none.flatPages = pages;
    CHECK(pte(e, none, 0, false, nullptr) == nullptr);

    // The address mask used on every leaf: a physical address keeps bits 12..47 and nothing else.
    CHECK((0x0000123456789abcull & kAddrMask) == 0x0000123456789000ull);
    CHECK((0xfffffffffffff000ull & kAddrMask) == 0x0000fffffffff000ull);

    // kNil is out of range for every real pool table number, which is what makes it usable as "none".
    CHECK(kNil > kTables || kNil == 0xFFFF);

    std::printf("vm_flat_test: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}