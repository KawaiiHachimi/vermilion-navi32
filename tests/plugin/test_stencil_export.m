// SPDX-License-Identifier: MIT
// Build with clang -fobjc-arc -framework Foundation -framework IOKit -framework Metal.
// Run with zero arguments for the installed driver, or pass a candidate's bundle executable.
#import "test_metal_device.h"

static int checkStencil(id<MTLDevice> device) {
    NSString *source = @"#include <metal_stdlib>\nusing namespace metal;\n"
        "vertex float4 vs(uint i [[vertex_id]]) { return float4(i == 1 ? 3 : -1, i == 2 ? 3 : -1, 0, 1); }\n"
        "struct Export { uint ref [[stencil]]; };\n"
        "fragment Export mark(float4 p [[position]]) { return {p.x < 8 ? 0xa5u : 0x3cu}; }\n"
        "struct Paint { float4 other[10]; float4 color; };\n"
        "fragment float4 cover(device const Paint& paint [[buffer(0)]]) { return paint.color; }";
    NSError *error = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:source options:nil error:&error];
    if (!lib) { NSLog(@"shader: %@", error); return 1; }

    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [lib newFunctionWithName:@"vs"];
    pd.fragmentFunction = [lib newFunctionWithName:@"mark"];
    pd.stencilAttachmentPixelFormat = MTLPixelFormatStencil8;
    MTLRenderPipelineColorAttachmentDescriptor *color = pd.colorAttachments[0];
    color.pixelFormat = MTLPixelFormatRGBA8Unorm;
    color.writeMask = MTLColorWriteMaskNone;
    id<MTLRenderPipelineState> mark = [device newRenderPipelineStateWithDescriptor:pd error:&error];
    if (!mark) { NSLog(@"export pipeline: %@", error); return 1; }

    pd.fragmentFunction = [lib newFunctionWithName:@"cover"];
    color.writeMask = MTLColorWriteMaskAll;
    color.blendingEnabled = YES;
    color.sourceRGBBlendFactor = color.sourceAlphaBlendFactor = MTLBlendFactorOne;
    color.destinationRGBBlendFactor = color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    id<MTLRenderPipelineState> cover = [device newRenderPipelineStateWithDescriptor:pd error:&error];
    if (!cover) { NSLog(@"cover pipeline: %@", error); return 1; }

    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:color.pixelFormat
        width:16 height:8 mipmapped:NO];
    td.storageMode = MTLStorageModePrivate;
    td.usage = MTLTextureUsageRenderTarget;
    id<MTLTexture> target = [device newTextureWithDescriptor:td];
    td.pixelFormat = MTLPixelFormatStencil8;
    id<MTLTexture> stencil = [device newTextureWithDescriptor:td];

    MTLDepthStencilDescriptor *ds = [MTLDepthStencilDescriptor new];
    MTLStencilDescriptor *face = [MTLStencilDescriptor new];
    face.depthStencilPassOperation = MTLStencilOperationReplace;
    ds.frontFaceStencil = ds.backFaceStencil = face;
    id<MTLDepthStencilState> write = [device newDepthStencilStateWithDescriptor:ds];
    face.depthStencilPassOperation = MTLStencilOperationKeep;
    face.stencilCompareFunction = MTLCompareFunctionEqual;
    ds.frontFaceStencil = ds.backFaceStencil = face;
    id<MTLDepthStencilState> compare = [device newDepthStencilStateWithDescriptor:ds];

    const NSUInteger pitch = 256;
    id<MTLBuffer> output = [device newBufferWithLength:pitch * 8 options:MTLResourceStorageModeShared];
    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (!target || !stencil || !write || !compare || !output || !queue) return 1;
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = target;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 1, 1);
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.stencilAttachment.texture = stencil;
    rp.stencilAttachment.loadAction = MTLLoadActionClear;
    rp.stencilAttachment.clearStencil = 0;
    rp.stencilAttachment.storeAction = MTLStoreActionStore;

    id<MTLCommandBuffer> cb = [queue commandBuffer];
    id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
    [e setRenderPipelineState:mark];
    [e setDepthStencilState:write];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [e setRenderPipelineState:cover];
    [e setDepthStencilState:compare];
    [e setStencilReferenceValue:0xa5];
    float paint[44] = {0};
    paint[40] = paint[43] = 0.4f;  // premultiplied red, at byte offset 160
    [e setFragmentBytes:paint length:sizeof paint atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [e endEncoding];

    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(16,8,1) toBuffer:output destinationOffset:0
        destinationBytesPerRow:pitch destinationBytesPerImage:pitch * 8];
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if (cb.status != MTLCommandBufferStatusCompleted) { NSLog(@"command: %@", cb.error); return 1; }
    unsigned wrong = 0;
    for (NSUInteger y = 0; y < 8; y++) {
        for (NSUInteger x = 0; x < 16; x++) {
            const uint8_t *pixel = (const uint8_t *)output.contents + y * pitch + x * 4;
            const uint8_t expected[] = {x < 8 ? 102 : 0, 0, x < 8 ? 153 : 255, 255};
            if (memcmp(pixel, expected, 4)) wrong++;
        }
    }
    printf("Stencil export, masked coverage and single blend: %u/128 correct\n", 128 - wrong);
    return wrong != 0;
}

int main(int argc, const char **argv) {
    @autoreleasepool {
        if (argc > 2) return 2;
        id<MTLDevice> device = openDevice(argc == 2 ? argv[1] : NULL);
        return device ? checkStencil(device) : 1;
    }
}
