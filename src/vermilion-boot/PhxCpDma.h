// SPDX-License-Identifier: MIT
// PhxCpDma.h: the CP DMA fill packets the kext's VRAM clear runs (b156, thread 02 part B).
//
// Kernel-free: tests/kext/vram_bo_test.cpp checks the encoding on a host. The packet is the one RADV emits for
// vkCmdFillBuffer on GFX11 (Mesa radv_cp_dma.c, radv_cs_emit_cp_dma with CP_DMA_CLEAR | CP_DMA_USE_L2; field
// positions from Mesa's generated amd_cp_packets_gfx11.h):
//   PKT3(DMA_DATA 0x50, 5 dwords)                            0xC0055000
//   header: DST_SEL 3 (DST_ADDR_USING_L2) << 20 | SRC_SEL 2 (DATA) << 29, CP_SYNC << 31 on the last packet (ME only;
//           the MEC always syncs, as Mesa sets it only for the gfx IP)
//   the 32-bit fill value, 0, DST_ADDR_LO, DST_ADDR_HI
//   command: BYTE_COUNT bits 25:0 (no SAS / DAS / SAIC / DAIC / RAW_WAIT, as Mesa's fill)
// Mesa limits one GFX11 packet to 32767 bytes, aligned down to 32 (cp_dma_max_byte_count): kMaxBytes.
// The kext runs these in an IB of its own at VMID 0, where the destination is an MC address in the FB aperture (as
// the RELEASE_MEM fence it already writes to fbMcBase + fenceOff).
#pragma once
#include <stdint.h>

namespace phxcpdma {

constexpr uint32_t kPkt3DmaData = 0xC0055000u;
constexpr uint32_t kHdrFill = (3u << 20) | (2u << 29);
constexpr uint32_t kCpSync = 1u << 31;
constexpr uint32_t kMaxBytes = 32767u & ~31u;   // 32736
constexpr uint32_t kDwPerPacket = 7;

constexpr uint64_t packetsFor(uint64_t bytes) { return (bytes + kMaxBytes - 1) / kMaxBytes; }
constexpr uint64_t dwordsFor(uint64_t bytes) { return packetsFor(bytes) * kDwPerPacket; }

// Writes the fill of [dstMc, dstMc + bytes) with `value` into ib. bytes and dstMc must be multiples of 4.
// Returns the dwords written, 0 if they don't fit in capDw (nothing useful written then) or the arguments are bad.
inline uint32_t fill(uint32_t *ib, uint32_t capDw, uint64_t dstMc, uint64_t bytes, uint32_t value, bool gfx) {
    if (!bytes || (bytes & 3) || (dstMc & 3) || dwordsFor(bytes) > capDw)
        return 0;
    uint32_t k = 0;
    while (bytes) {
        const uint32_t n = bytes > kMaxBytes ? kMaxBytes : static_cast<uint32_t>(bytes);
        bytes -= n;
        ib[k++] = kPkt3DmaData;
        ib[k++] = kHdrFill | ((gfx && !bytes) ? kCpSync : 0u);
        ib[k++] = value;
        ib[k++] = 0;
        ib[k++] = static_cast<uint32_t>(dstMc);
        ib[k++] = static_cast<uint32_t>(dstMc >> 32);
        ib[k++] = n;
        dstMc += n;
    }
    return k;
}

} // namespace phxcpdma
