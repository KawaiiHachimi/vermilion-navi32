// SPDX-License-Identifier: MIT
// PhxGpuTime.h: GPU time per context (b157, thread 02 part C): which process holds the GPU, and for how long.
//
// Kernel-free: tests/kext/gputime_test.cpp checks the packets and the accounting on a host.
// With debug.phxgpu_time_on=1, gpuSubmit brackets each submission's IBs with two GPU timestamps, written to the
// submission's slot in the kext's VRAM area (PHX_GPUTIME_OFF): a top-of-pipe one before the IBs and a bottom-of-pipe
// one after them, ahead of the fence. These are the packets RADV emits for vkCmdWriteTimestamp2 on GFX11
// (radv_query.c: TOP_OF_PIPE -> COPY_DATA from the GPU timestamp, WR_CONFIRM | COUNT_SEL; otherwise RELEASE_MEM
// BOTTOM_OF_PIPE_TS with DATA_SEL 3 and INT_SEL 3; Mesa ac_cmdbuf_cp.c, sid.h). No register write, no new message.
// The timestamp is SMUIO's 100 MHz GOLDEN_TSC (the counter phx_client.cpp already hands to RADV: gpu_counter_freq
// 100000 kHz in Mesa's amdgpu_devices.c for this family).
//
// When a submission retires, its busy time is end - max(start, the end of the queue's previous submission): the
// top-of-pipe stamp is taken when the CP reaches the packet, which can be before the previous submission has drained,
// and one queue runs its submissions in order. Waits inside the IBs (WAIT_REG_MEM, barriers) count as busy: this
// measures how long a context holds the queue, not how hard the shaders work.
#pragma once
#include <stdint.h>

namespace phxgputime {

constexpr uint64_t kHz = 100000000ull;
constexpr uint32_t kQueues = 3, kSlots = 64, kSlotBytes = 16;   // per slot: start (8 bytes), end (8 bytes)
constexpr uint32_t kAreaBytes = kQueues * kSlots * kSlotBytes;
constexpr uint32_t kStartDw = 6, kEndDw = 8;
constexpr uint16_t kNoCtx = 0xFFFF;   // a submission without stamps (switch off, a kernel IB, VCN)

constexpr uint32_t kPkt3CopyData = 0xC0044000u;   // PKT3(COPY_DATA 0x40, 4)
constexpr uint32_t kCopyTimestamp = 9u | (5u << 8) | (1u << 16) | (1u << 20);   // SRC timestamp, DST mem, 64-bit, confirm
constexpr uint32_t kPkt3ReleaseMem = 0xC0064900u; // PKT3(RELEASE_MEM 0x49, 6)
constexpr uint32_t kBottomOfPipeTs = 0x28u | (5u << 8);   // EVENT_TYPE BOTTOM_OF_PIPE_TS (40), EVENT_INDEX 5
constexpr uint32_t kSelTimestamp = (3u << 29) | (3u << 24);   // DATA_SEL timestamp, INT_SEL send data after confirm

constexpr uint32_t slotOff(uint32_t queue, uint32_t slot) { return (queue * kSlots + (slot & (kSlots - 1))) * kSlotBytes; }

inline uint32_t emitStart(uint32_t *d, uint64_t mc) {
    d[0] = kPkt3CopyData;
    d[1] = kCopyTimestamp;
    d[2] = 0;
    d[3] = 0;
    d[4] = static_cast<uint32_t>(mc);
    d[5] = static_cast<uint32_t>(mc >> 32);
    return kStartDw;
}

inline uint32_t emitEnd(uint32_t *d, uint64_t mc) {
    d[0] = kPkt3ReleaseMem;
    d[1] = kBottomOfPipeTs;
    d[2] = kSelTimestamp;
    d[3] = static_cast<uint32_t>(mc);
    d[4] = static_cast<uint32_t>(mc >> 32);
    d[5] = 0;
    d[6] = 0;
    d[7] = 0;
    return kEndDw;
}

// One retired submission's busy ticks; lastEnd is the queue's running end. 0 for a slot the GPU never stamped
// (cleared to 0 before the submission) or a backwards pair.
inline uint64_t account(uint64_t &lastEnd, uint64_t start, uint64_t end) {
    if (!start || !end || end < start)
        return 0;
    const uint64_t s = start > lastEnd ? start : lastEnd;
    const uint64_t b = end > s ? end - s : 0;
    if (end > lastEnd)
        lastEnd = end;
    return b;
}

constexpr uint64_t ticksToUs(uint64_t t) { return t / (kHz / 1000000ull); }

} // namespace phxgputime
