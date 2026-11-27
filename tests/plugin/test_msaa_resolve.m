// SPDX-License-Identifier: MIT
// xcrun clang -arch x86_64 -fobjc-arc tests/plugin/test_msaa_resolve.m -framework Foundation -framework Metal -framework IOKit -o /tmp/test_msaa_resolve
// Optional argv[1]: the candidate NVMTLDriver executable, loaded only in this process.
#include "test_metal_device.h"

int main(int argc, const char **argv)
{
    @autoreleasepool {
        if (argc > 2) return 2;
        id<MTLDevice> device = openDevice(argv[1]);
        enum { width = 13, height = 7, pitch = 256, bytes = pitch * height };
        if (!device || ![device supportsTextureSampleCount:4]) return 1;
        printf("device %s, plugin %s\n", device.name.UTF8String, argv[1] ?: "system");
        MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                       width:width height:height mipmapped:NO];
        desc.storageMode = MTLStorageModePrivate;
        desc.usage = MTLTextureUsageRenderTarget;
        id<MTLTexture> resolved = [device newTextureWithDescriptor:desc];
        desc.textureType = MTLTextureType2DMultisample;
        desc.sampleCount = 4;
        id<MTLTexture> msaa = [device newTextureWithDescriptor:desc];
        id<MTLBuffer> result = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
        id<MTLBuffer> upload = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
        if (!resolved || !msaa || !result.contents || !upload.contents) return 1;
        uint8_t *input = upload.contents, *output = result.contents;
        for (size_t i = 0; i < bytes; ++i) input[i] = (uint8_t)(i * 31 + i / pitch);
        id<MTLCommandQueue> queue = [device newCommandQueue];
        MTLSize size = MTLSizeMake(width, height, 1);
        MTLOrigin origin = MTLOriginMake(0, 0, 0);
        for (int pass = 0; pass < 2; ++pass) {
            memset(output, 0x5a, bytes);
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            if (!cb) return 1;
            if (pass == 1) {
                MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
                rp.colorAttachments[0].texture = msaa;
                rp.colorAttachments[0].resolveTexture = resolved;
                rp.colorAttachments[0].loadAction = MTLLoadActionClear;
                rp.colorAttachments[0].clearColor = MTLClearColorMake(1, 0, 1, 1);
                rp.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
                id<MTLRenderCommandEncoder> render = [cb renderCommandEncoderWithDescriptor:rp];
                if (!render) return 1;
                [render endEncoding];
            }
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            if (!blit) return 1;
            if (pass == 0)
                [blit copyFromBuffer:upload sourceOffset:0 sourceBytesPerRow:pitch sourceBytesPerImage:bytes sourceSize:size
                           toTexture:resolved destinationSlice:0 destinationLevel:0 destinationOrigin:origin];
            [blit copyFromTexture:resolved sourceSlice:0 sourceLevel:0 sourceOrigin:origin sourceSize:size
                         toBuffer:result destinationOffset:0 destinationBytesPerRow:pitch destinationBytesPerImage:bytes];
            [blit endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            if (cb.status != MTLCommandBufferStatusCompleted) {
                NSLog(@"command failed: %@", cb.error);
                return 1;
            }
            const uint8_t bgra[] = {255, 0, 255, 255};
            for (size_t i = 0; i < bytes; ++i) {
                uint8_t expected = 0x5a;
                if (i % pitch < width * 4) expected = pass == 0 ? input[i] : bgra[i % 4];
                if (output[i] != expected) {
                    fprintf(stderr, "pass %d byte %zu: expected %u got %u\n", pass, i, expected, output[i]);
                    return 1;
                }
            }
            printf("%s: %u pixels and row padding verified\n", pass ? "4x clear / resolve / read" : "1x upload / read", width * height);
        }
        return 0;
    }
}
