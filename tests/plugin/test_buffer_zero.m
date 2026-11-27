// SPDX-License-Identifier: MIT
// xcrun clang -mmacosx-version-min=26.0 -arch x86_64 -fobjc-arc tests/plugin/test_buffer_zero.m -framework Foundation -framework Metal -framework IOKit -o /tmp/test_buffer_zero
// Optional argv[1]: the candidate NVMTLDriver executable, loaded only in this process.
#include "test_metal_device.h"

// Fill a test buffer, release it, and check Metal's zero-initialization contract on a
// new buffer of the same length. Read only the bytes in the requested Metal range.
static int copyBytes(id<MTLCommandQueue> q, id<MTLBuffer> src, id<MTLBuffer> dst, size_t n)
{
    id<MTLCommandBuffer> cb = [q commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    if (!blit) return 1;
    [blit copyFromBuffer:src sourceOffset:0 toBuffer:dst destinationOffset:0 size:n];
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    return cb.status != MTLCommandBufferStatusCompleted;
}

int main(int argc, const char **argv)
{
    @autoreleasepool {
        if (argc > 2) return 2;
        id<MTLDevice> device = openDevice(argv[1]);
        id<MTLCommandQueue> queue = [device newCommandQueue];
        const unsigned lengths[] = {1, 2, 3, 4, 5, 6, 7, 8, 255, 256, 257, (1 << 20) + 3};
        const unsigned capacity = (1 << 20) + 32;
        id<MTLBuffer> staging = [device newBufferWithLength:capacity options:MTLResourceStorageModeShared];
        if (!queue || !staging.contents) return 1;
        int fails = 0;
        const MTLResourceOptions modes[] = {MTLResourceStorageModePrivate, MTLResourceStorageModeManaged};
        for (unsigned m = 0; m < (device.hasUnifiedMemory ? 1u : 2u); m++)
            for (unsigned k = 0; k < sizeof lengths / sizeof lengths[0]; k++) {
                const unsigned n = lengths[k];
                @autoreleasepool {
                    id<MTLBuffer> old = [device newBufferWithLength:n options:modes[m]];
                    memset(staging.contents, 0xc6, n);
                    if (old.length != n || copyBytes(queue, staging, old, n)) return 1;
                }
                @autoreleasepool {
                    id<MTLBuffer> fresh = [device newBufferWithLength:n options:modes[m]];
                    memset(staging.contents, 0xb4, n + 16);
                    if (fresh.length != n || copyBytes(queue, fresh, staging, n)) return 1;
                    const uint8_t *bytes = staging.contents;
                    int matches = 1;
                    for (unsigned i = 0; i < n + 16; i++)
                        if (bytes[i] != (i < n ? 0 : 0xb4)) matches = 0;
                    printf("%s length %u: %s\n", m == 0 ? "private" : "managed", n, matches ? "pass" : "mismatch");
                    fails += !matches;
                }
                if (m == 1) @autoreleasepool {
                    // The first submit can carry the new buffer's zero-fill and a CPU update in
                    // the same prologue. The CPU write must win, including at an odd final byte.
                    id<MTLBuffer> edited = [device newBufferWithLength:n options:modes[m]];
                    if (edited.length != n || !edited.contents) return 1;
                    ((uint8_t *)edited.contents)[n - 1] = 0x71;
                    [edited didModifyRange:NSMakeRange(n - 1, 1)];
                    memset(staging.contents, 0xb4, n + 16);
                    if (copyBytes(queue, edited, staging, n)) return 1;
                    const uint8_t *bytes = staging.contents;
                    int matches = 1;
                    for (unsigned i = 0; i < n + 16; i++) {
                        uint8_t expected = i < n ? 0 : 0xb4;
                        if (i == n - 1) expected = 0x71;
                        if (bytes[i] != expected) matches = 0;
                    }
                    printf("managed CPU update length %u: %s\n", n, matches ? "pass" : "mismatch");
                    fails += !matches;
                }
            }
        if (!device.hasUnifiedMemory) {
            int perThread[4] = {0};
            int *results = perThread;
            dispatch_apply(4, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^(size_t thread) {
                id<MTLCommandQueue> q = [device newCommandQueue];
                for (unsigned k = 0; k < 8; k++) @autoreleasepool {
                    size_t n = 37 + thread;
                    id<MTLBuffer> edited = [device newBufferWithLength:n options:MTLResourceStorageModeManaged];
                    id<MTLBuffer> readback = [device newBufferWithLength:n options:MTLResourceStorageModeShared];
                    if (!edited.contents || !readback.contents) { results[thread]++; continue; }
                    uint8_t value = 1 + k + thread * 8;
                    ((uint8_t *)edited.contents)[n - 1] = value;
                    [edited didModifyRange:NSMakeRange(n - 1, 1)];
                    if (copyBytes(q, edited, readback, n)) { results[thread]++; continue; }
                    for (size_t i = 0; i < n; i++)
                        if (((uint8_t *)readback.contents)[i] != (i + 1 == n ? value : 0)) results[thread]++;
                }
            });
            int bad = perThread[0] + perThread[1] + perThread[2] + perThread[3];
            printf("concurrent Managed updates on four queues: %s\n", bad ? "mismatch" : "pass");
            fails += bad;
        }
        return fails != 0;
    }
}
