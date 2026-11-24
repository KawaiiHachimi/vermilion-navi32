// SPDX-License-Identifier: MIT
// PhxAsic.h: the per-ASIC identity of this driver (GC version, GB_ADDR_CONFIG, the two GC memory-hub shifts) as a
// pure function, so the values a port depends on can be tested on any Mac (tests/kext/asic_test.cpp).
//
// Both dies (Navi 31 = GC 11.0.0, Navi 32 = GC 11.0.3) report the same IP base indices - GC 0x1260 / 0xa000 /
// 0x1c000, MP0/MP1 0x16000, MMHUB 3.0.0, NBIF 4.3.0, DCN 3.2.0 - so every absolute register offset stays valid
// except inside the GC memory hub, where GC 11.0.3 moved two groups: the VM context/aperture block by +0x10 dwords
// and the L2/protection-fault block by +0x04. GB_ADDR_CONFIG is per die: the RX 7900 XT (Navi 31) reads 0x545,
// the RX 7800 XT (Navi 32) reads 0x444 - both read from the card, not from a table.
#ifndef PHX_ASIC_H
#define PHX_ASIC_H

#include <stdint.h>

namespace phxasic {

struct Asic {
    const char *name;      // "navi31", "navi32"
    uint32_t deviceId;     // PCI device id (vendor is always 0x1002)
    uint32_t gcVersion;    // (major << 16) | (minor << 8) | revision
    uint32_t gbAddrConfig; // GB_ADDR_CONFIG as the card reports it
    uint32_t gcVmShift;    // added to the GC memory-hub VM context/aperture offsets (dwords)
    uint32_t gcL2Shift;    // added to the GC memory-hub L2 / protection-fault offsets (dwords)
    bool navi32;           // true for the Navi 32 die (GC 11.0.3)
};

// Navi 31 (RX 7900 XT/XTX, GC 11.0.0): the reference values every other offset in this driver was measured against.
static constexpr Asic kNav31 = {"navi31", 0x744c, (11u << 16) | (0u << 8) | 0u, 0x00000545u, 0x0u, 0x0u, false};
// Navi 32 (RX 7800 XT 16 GiB and RX 7700 XT 12 GiB share device id 0x747e; VRAM size tells them apart, not the id).
static constexpr Asic kNav32 = {"navi32", 0x747e, (11u << 16) | (3u << 8) | 0u, 0x00000444u, 0x10u, 0x04u, true};

// The table a device id is looked up in. Unknown ids keep the Navi 31 values: this driver's offsets are measured
// against that die, so an unknown card behaves like the reference until someone measures it.
static constexpr Asic kTable[] = {kNav31, kNav32};

// select() is the whole policy: an unknown device id is Navi 31, never a guess.
inline const Asic &select(uint32_t deviceId) {
    for (const Asic &a : kTable)
        if (a.deviceId == deviceId) return deviceId == kNav32.deviceId ? kNav32 : kNav31;
    return kNav31;
}

inline const Asic &nav31() { return kNav31; }
inline const Asic &nav32() { return kNav32; }

}   // namespace phxasic

#endif   // PHX_ASIC_H