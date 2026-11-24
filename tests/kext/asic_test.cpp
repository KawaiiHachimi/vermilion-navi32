// SPDX-License-Identifier: MIT
// asic_test.cpp: host test of src/vermilion-boot/PhxAsic.h - the per-ASIC values this port depends on.
//   c++ -std=c++17 -Wall -Wextra -Werror -I src/vermilion-boot -o /tmp/asic_test tests/kext/asic_test.cpp && /tmp/asic_test
//
// The point is that a wrong constant here is silent: GC 11.0.3 moved the GC memory-hub VM block by +0x10 dwords and
// the L2/protection-fault block by +0x04, and GB_ADDR_CONFIG goes into the MES frame and into RADV's pool setup.
// Reading 0x545 on a Navi 32 die, or shifting by 0, does not crash - it programs the wrong registers.
#include "PhxAsic.h"
#include <cstdio>
#include <cstring>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main() {
    const phxasic::Asic &n31 = phxasic::nav31();
    const phxasic::Asic &n32 = phxasic::nav32();

    // Navi 31: the reference die this driver was measured on.
    CHECK(n31.deviceId == 0x744c);
    CHECK(n31.gcVersion == 0x000b0000u);                 // GC 11.0.0
    CHECK(n31.gbAddrConfig == 0x00000545u);              // read from the RX 7900 XT
    CHECK(n31.gcVmShift == 0 && n31.gcL2Shift == 0);     // no shift on 11.0.0
    CHECK(!n31.navi32);

    // Navi 32: everything this port had to change.
    CHECK(n32.deviceId == 0x747e);
    CHECK(n32.gcVersion == 0x000b0300u);                 // GC 11.0.3
    CHECK(n32.gbAddrConfig == 0x00000444u);              // read from the RX 7800 XT
    CHECK(n32.gcVmShift == 0x10);                        // VM context/aperture block, GC memory hub
    CHECK(n32.gcL2Shift == 0x04);                        // L2 / protection-fault block
    CHECK(n32.navi32);
    CHECK(std::strcmp(n32.name, "navi32") == 0 && std::strcmp(n31.name, "navi31") == 0);

    // select(): both known ids, and the policy for anything else.
    CHECK(&phxasic::select(0x744c) == &n31);
    CHECK(&phxasic::select(0x747e) == &n32);
    CHECK(&phxasic::select(0x73bf) == &n31);             // Navi 21: not this driver's die, keeps the reference
    CHECK(&phxasic::select(0x0000) == &n31);             // no id at all
    CHECK(&phxasic::select(0xffff) == &n31);

    // The two dies must never be equal on the values that matter: a copy-paste would show up here.
    CHECK(n31.gbAddrConfig != n32.gbAddrConfig);
    CHECK(n31.gcVersion != n32.gcVersion);
    CHECK(n31.gcVmShift != n32.gcVmShift);
    CHECK(n31.gcL2Shift != n32.gcL2Shift);

    // Device ids are unique in the table (select() returns the first match).
    for (const phxasic::Asic &a : phxasic::kTable)
        for (const phxasic::Asic &b : phxasic::kTable)
            if (&a != &b) CHECK(a.deviceId != b.deviceId);

    std::printf("asic_test: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}