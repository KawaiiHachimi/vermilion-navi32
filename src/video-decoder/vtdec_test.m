// SPDX-License-Identifier: MIT
// vtdec_test <movie> <frames> [<libphxdec_hevc.dylib>]: decodes the first <frames> video samples of <movie> through a
// VTDecompressionSession (x420 / 420v output) and prints one md5 per output frame (Y rows then CbCr rows, visible
// width only) plus the PTS. With a decoder dylib it registers our VCN decoder in this process first (WP6 B5 test);
// without, Apple's decoder runs. Prints whether the session reports a hardware decoder.
// VTTEST_I420=1 adds an "i420" md5 per frame: the frame as planar I420 (Y, then Cb, then Cr), 16-bit little-endian
// samples with the value in the low 10 bits for x420, the format of the conformance md5 files (libaom's
// test vectors, <vector>.md5), so a decoder can be checked against them.
#import <AVFoundation/AVFoundation.h>
#import <Metal/Metal.h>
#import <VideoToolbox/VideoToolbox.h>
#import <IOSurface/IOSurface.h>
#import <CommonCrypto/CommonDigest.h>
#include <dlfcn.h>

static int gFrames;

// md5 of a biplanar 4:2:0 frame (420v / x420) as planar I420, the layout libaom's test-vector md5s use
static void md5_i420(CVImageBufferRef img, unsigned char d[16])
{
    const bool ten = CVPixelBufferGetPixelFormatType(img) == 'x420';
    CC_MD5_CTX c; CC_MD5_Init(&c);
    const uint8_t *y0 = CVPixelBufferGetBaseAddressOfPlane(img, 0), *c0 = CVPixelBufferGetBaseAddressOfPlane(img, 1);
    const size_t ys = CVPixelBufferGetBytesPerRowOfPlane(img, 0), cs = CVPixelBufferGetBytesPerRowOfPlane(img, 1);
    const size_t w = CVPixelBufferGetWidth(img), h = CVPixelBufferGetHeight(img), cw = (w + 1) / 2, ch = (h + 1) / 2;
    uint8_t row[2 * 8192];
    for (size_t y = 0; y < h; y++) {
        if (!ten) { CC_MD5_Update(&c, y0 + y * ys, (CC_LONG)w); continue; }
        const uint16_t *src = (const uint16_t *)(y0 + y * ys);
        for (size_t x = 0; x < w; x++) { const uint16_t v = src[x] >> 6; row[2 * x] = v & 0xff; row[2 * x + 1] = v >> 8; }
        CC_MD5_Update(&c, row, (CC_LONG)(2 * w));
    }
    for (int comp = 0; comp < 2; comp++)
        for (size_t y = 0; y < ch; y++) {
            size_t n = 0;
            for (size_t x = 0; x < cw; x++) {
                if (ten) {
                    const uint16_t v = ((const uint16_t *)(c0 + y * cs))[2 * x + comp] >> 6;
                    row[n++] = v & 0xff; row[n++] = v >> 8;
                } else
                    row[n++] = c0[y * cs + 2 * x + comp];
            }
            CC_MD5_Update(&c, row, (CC_LONG)n);
        }
    CC_MD5_Final(d, &c);
}
static void out_cb(void *ref, void *frameRef, OSStatus st, VTDecodeInfoFlags flags, CVImageBufferRef img, CMTime pts, CMTime dur)
{
    if (st || !img) { printf("frame %d: status %d flags 0x%x no image\n", gFrames++, (int)st, (unsigned)flags); return; }
    CVPixelBufferLockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
    CC_MD5_CTX c; CC_MD5_Init(&c);
    const OSType fmt = CVPixelBufferGetPixelFormatType(img);
    const size_t bpp = (fmt == 'x420' || fmt == 'xf20') ? 2 : 1;
    for (size_t p = 0; p < CVPixelBufferGetPlaneCount(img); p++) {
        const uint8_t *b = CVPixelBufferGetBaseAddressOfPlane(img, p);
        const size_t s = CVPixelBufferGetBytesPerRowOfPlane(img, p), h = CVPixelBufferGetHeightOfPlane(img, p);
        const size_t w = CVPixelBufferGetWidthOfPlane(img, p) * bpp * (p ? 2 : 1);
        for (size_t y = 0; y < h; y++) CC_MD5_Update(&c, b + y * s, (CC_LONG)w);
    }
    unsigned char d[16]; CC_MD5_Final(d, &c);
    unsigned char di[16];
    const bool i420 = getenv("VTTEST_I420") && CVPixelBufferGetWidth(img) <= 8192;
    if (i420) md5_i420(img, di);
    CVPixelBufferUnlockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
    printf("frame %d pts %lld fmt %.4s %zux%zu md5 ", gFrames++, pts.value, (const char *)&(OSType){CFSwapInt32HostToBig(fmt)},
           CVPixelBufferGetWidth(img), CVPixelBufferGetHeight(img));
    for (int i = 0; i < 16; i++) printf("%02x", d[i]);
    if (i420) { printf(" i420 "); for (int i = 0; i < 16; i++) printf("%02x", di[i]); }
    printf("\n");
    if (getenv("VTTEST_ATTACH") && gFrames == 1) {   // VTTEST_ATTACH=1: the first frame's buffer attachments (colorimetry)
        CFDictionaryRef a = CVBufferCopyAttachments(img, kCVAttachmentMode_ShouldPropagate);
        printf("attachments (propagated): %s\n", a ? [[(__bridge NSDictionary *)a description] UTF8String] : "none");
        if (a) CFRelease(a);
        IOSurfaceRef sf = CVPixelBufferGetIOSurface(img);
        if (sf) {
            const uint8_t *b0 = IOSurfaceGetBaseAddress(sf);
            IOSurfaceLock(sf, kIOSurfaceLockReadOnly, NULL);
            for (size_t p = 0; p < IOSurfaceGetPlaneCount(sf); p++)
                printf("surface plane %zu: offset %td, bytes per row %zu\n", p, (const uint8_t *)IOSurfaceGetBaseAddressOfPlane(sf, p) - b0,
                       IOSurfaceGetBytesPerRowOfPlane(sf, p));
            IOSurfaceUnlock(sf, kIOSurfaceLockReadOnly, NULL);
            printf("surface alloc %zu\n", IOSurfaceGetAllocSize(sf));
        }
        a = CVBufferCopyAttachments(img, kCVAttachmentMode_ShouldNotPropagate);
        printf("attachments (not propagated): %s\n", a ? [[(__bridge NSDictionary *)a description] UTF8String] : "none");
        if (a) CFRelease(a);
    }
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    @autoreleasepool {
        if (argc < 3) { fprintf(stderr, "usage: vtdec_test <movie> <frames> [decoder.dylib]\n"); return 2; }
        const int want = atoi(argv[2]);
        if (argc > 3) {
            void *h = dlopen(argv[3], RTLD_NOW);
            if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
            OSStatus (*reg)(void) = dlsym(h, "phxdec_register");
            printf("phxdec_register -> %d\n", reg ? (int)reg() : -1);
        }
        // VTTEST_METAL=<MB>: open the Metal device (our plugin over RADV) and keep that many MB of private buffers alive
        // before decoding, as Chrome's GPU process does (two Vulkan devices in one process; WP6 B7)
        NSMutableArray *keep = [NSMutableArray array];
        if (getenv("VTTEST_METAL")) {
            id<MTLDevice> md = MTLCreateSystemDefaultDevice();
            const int mb = atoi(getenv("VTTEST_METAL"));
            for (int i = 0; i < mb / 16; i++) { id<MTLBuffer> b = [md newBufferWithLength:16u << 20 options:MTLResourceStorageModePrivate]; if (b) [keep addObject:b]; }
            printf("metal device %s, %lu x 16 MB private buffers\n", md.name.UTF8String, (unsigned long)keep.count);
        }
        AVURLAsset *a = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:@(argv[1])] options:nil];
        __block NSArray *tracks = nil;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        [a loadTracksWithMediaType:AVMediaTypeVideo completionHandler:^(NSArray *t, NSError *e) { tracks = t; dispatch_semaphore_signal(sem); }];
        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
        if (!tracks.count) { printf("no video track\n"); return 1; }
        NSError *err = nil;
        AVAssetReader *r = [AVAssetReader assetReaderWithAsset:a error:&err];
        AVAssetReaderTrackOutput *o = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:tracks[0] outputSettings:nil];
        [r addOutput:o];
        if (![r startReading]) { printf("startReading: %s\n", r.error.description.UTF8String); return 1; }
        VTDecompressionSessionRef s = NULL;
        CMFormatDescriptionRef fd = NULL;
        int sent = 0;
        uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        while (sent < want) {
            CMSampleBufferRef sb = [o copyNextSampleBuffer];
            if (!sb) break;
            if (!CMSampleBufferGetFormatDescription(sb) || !CMSampleBufferGetDataBuffer(sb) || !CMSampleBufferGetNumSamples(sb)) {
                CFRelease(sb);
                continue;
            }
            if (!s) {
                fd = CMSampleBufferGetFormatDescription(sb);
                if (getenv("VTTEST_FD")) {
                    CFDictionaryRef ex = CMFormatDescriptionGetExtensions(fd);
                    NSDictionary *exd = (__bridge NSDictionary *)ex;
                    for (id k in exd) {
                        id v = exd[k];
                        NSString *vs = [v isKindOfClass:[NSData class]] ? [NSString stringWithFormat:@"<data %lu>", (unsigned long)[(NSData *)v length]] : [[v description] substringToIndex:MIN((NSUInteger)160, [[v description] length])];
                        printf("fd ext %s = %s\n", [[k description] UTF8String], [vs UTF8String]);
                    }
                }
                const int bd = 10; OSType fmt = 'x420';
                CFNumberRef bpc = CMFormatDescriptionGetExtension(fd, kCMFormatDescriptionExtension_BitsPerComponent);
                int v = bd; if (bpc) CFNumberGetValue(bpc, kCFNumberIntType, &v);
                if (v == 8) fmt = '420v';
                NSDictionary *dst = @{(id)kCVPixelBufferPixelFormatTypeKey: @(fmt), (id)kCVPixelBufferIOSurfacePropertiesKey: @{}};
                VTDecompressionOutputCallbackRecord cb = {out_cb, NULL};
                OSStatus st = VTDecompressionSessionCreate(NULL, fd, NULL, (__bridge CFDictionaryRef)dst, &cb, &s);
                CFBooleanRef hw = NULL;
                if (!st) VTSessionCopyProperty(s, kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder, NULL, &hw);
                printf("session create %d, codec %.4s, hardware decoder %s\n", (int)st,
                       (const char *)&(OSType){CFSwapInt32HostToBig(CMFormatDescriptionGetMediaSubType(fd))},
                       hw ? (CFBooleanGetValue(hw) ? "yes" : "no") : "?");
                if (hw) CFRelease(hw);
                if (st) { CFRelease(sb); return 1; }
                // VTTEST_HDRFIRST=1 (AV1): first send a sample with only a temporal delimiter + the sequence header from
                // av1C, the way Chrome's first YouTube sample looks (80 bytes, no frame; WP6 B7)
                CFDictionaryRef atoms = CMFormatDescriptionGetExtension(fd, kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms);
                CFDataRef av1c = atoms ? CFDictionaryGetValue(atoms, CFSTR("av1C")) : NULL;
                if (getenv("VTTEST_HDRFIRST") && av1c && CFDataGetLength(av1c) > 4) {
                    const size_t n = (size_t)CFDataGetLength(av1c) - 4 + 2;
                    uint8_t *b = malloc(n);
                    b[0] = 0x12; b[1] = 0x00;   // OBU_TEMPORAL_DELIMITER, has_size, size 0
                    memcpy(b + 2, CFDataGetBytePtr(av1c) + 4, n - 2);
                    CMBlockBufferRef bb = NULL;
                    CMSampleBufferRef hs = NULL;
                    CMSampleTimingInfo ti = {kCMTimeInvalid, CMSampleBufferGetPresentationTimeStamp(sb), kCMTimeInvalid};
                    CMBlockBufferCreateWithMemoryBlock(NULL, b, n, kCFAllocatorMalloc, NULL, 0, n, 0, &bb);
                    CMSampleBufferCreateReady(NULL, bb, fd, 1, 1, &ti, 1, &n, &hs);
                    VTDecodeInfoFlags hi = 0;
                    const OSStatus hst = VTDecompressionSessionDecodeFrame(s, hs, 0, NULL, &hi);
                    printf("headers-only sample (%zu bytes): DecodeFrame %d, info 0x%x\n", n, (int)hst, (unsigned)hi);
                    CFRelease(hs);
                    CFRelease(bb);
                }
            }
            VTDecodeInfoFlags inf = 0;
            const VTDecodeFrameFlags df = getenv("VTTEST_TEMPORAL") ? kVTDecodeFrame_EnableTemporalProcessing : 0;
            OSStatus st = VTDecompressionSessionDecodeFrame(s, sb, df, NULL, &inf);
            if (st) printf("DecodeFrame %d: %d\n", sent, (int)st);
            CFRelease(sb);
            sent++;
        }
        printf("calling FinishDelayedFrames\n");
        VTDecompressionSessionFinishDelayedFrames(s);
        printf("FinishDelayedFrames returned, waiting\n");
        VTDecompressionSessionWaitForAsynchronousFrames(s);
        uint64_t t1 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        printf("sent %d, frames out %d, %.1f ms (%.1f fps)\n", sent, gFrames, (t1 - t0) / 1e6, gFrames * 1e9 / (double)(t1 - t0));
        VTDecompressionSessionInvalidate(s);
        CFRelease(s);
    }
    return 0;
}
