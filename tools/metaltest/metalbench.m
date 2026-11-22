// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vermilion contributors
//
// metalbench: a repeatable Metal load for measuring the GPU (clocks, power, temperature, frame times) while
// tools/phxctl/smumetrics.py samples the SMU. Everything is drawn off screen, so WindowServer's present path is
// not part of the number.
//
//   metalbench render  [seconds=20] [iterations=64] [width=2560] [height=1440]
//       one full-screen triangle per frame; the fragment shader runs a Mandelbrot loop of <iterations> per pixel
//       (fill rate and ALU; no textures, no vertex load)
//   metalbench compute [seconds=20] [iterations=64] [width=2560] [height=1440]
//       the same loop as a compute kernel writing a texture
//   metalbench submit  [seconds=10]
//       tiny draws (a 16x16 target), one command buffer each, each waited for: the cost of a submit and a fence
//       wait through the plugin, RADV and the kext (CPU bound)
//
// render and compute keep at most 3 command buffers in flight. Output: frames, frames/s, the wall time per frame
// (average, median, 99th percentile, longest) and, where the driver reports them, the GPU time per frame.
// Exit codes: 0 ok, 1 no Metal device, 2 shader compile failed, 3 pipeline failed, 4 GPU error.
// Build: scripts/build/metalbench.sh.
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <mach/mach_time.h>
#include <stdlib.h>

static NSString *const kSrc =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "struct P { uint iters; uint frame; float w; float h; };\n"
     "static float4 mandel(float2 uv, uint iters, uint frame) {\n"
     "  float2 c = float2(-0.743643887 + (uv.x - 0.5) * 0.004, 0.131825904 + (uv.y - 0.5) * 0.00225);\n"
     "  c += float2(frame & 63u) * 1e-7;\n"
     "  float2 z = float2(0.0); float acc = 0.0;\n"
     "  for (uint i = 0; i < iters; i++) {\n"
     "    z = float2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;\n"
     "    acc += 1.0 / (1.0 + dot(z, z));\n"   // no early exit: every pixel costs the same
     "    z = clamp(z, float2(-4.0), float2(4.0));\n"
     "  }\n"
     "  float t = acc / float(iters);\n"
     "  return float4(t, fract(t * 7.0), fract(t * 31.0), 1.0);\n"
     "}\n"
     "struct V { float4 pos [[position]]; float2 uv; };\n"
     "vertex V vs(uint vid [[vertex_id]]) {\n"
     "  float2 p = float2((vid << 1) & 2, vid & 2);\n"
     "  V o; o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0); o.uv = p; return o;\n"
     "}\n"
     "fragment float4 fs(V in [[stage_in]], constant P &p [[buffer(0)]]) { return mandel(in.uv, p.iters, p.frame); }\n"
     "fragment float4 fs_flat(V in [[stage_in]]) { return float4(in.uv, 0.0, 1.0); }\n"
     "kernel void ck(texture2d<float, access::write> out [[texture(0)]], constant P &p [[buffer(0)]],\n"
     "               uint2 gid [[thread_position_in_grid]]) {\n"
     "  if (gid.x >= out.get_width() || gid.y >= out.get_height()) return;\n"
     "  out.write(mandel(float2(gid) / float2(p.w, p.h), p.iters, p.frame), gid);\n"
     "}\n";

typedef struct { uint32_t iters, frame; float w, h; } Params;

static double now_s(void) {
  static mach_timebase_info_data_t tb;
  if (!tb.denom) mach_timebase_info(&tb);
  return (double)mach_absolute_time() * tb.numer / tb.denom / 1e9;
}
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static void report(const char *what, double *v, long n) {
  if (n <= 0) { printf("%s: no samples\n", what); return; }
  double sum = 0;
  for (long i = 0; i < n; i++) sum += v[i];
  qsort(v, n, sizeof(double), cmpd);
  printf("%s ms: avg %.3f median %.3f p99 %.3f max %.3f (n %ld)\n", what, sum / n * 1e3, v[n / 2] * 1e3,
         v[(long)(n * 0.99)] * 1e3, v[n - 1] * 1e3, n);
}

int main(int argc, char **argv) {
  @autoreleasepool {
    const char *mode = argc > 1 ? argv[1] : "render";
    BOOL isCompute = !strcmp(mode, "compute"), isSubmit = !strcmp(mode, "submit");
    if (!isCompute && !isSubmit && strcmp(mode, "render")) {
      fprintf(stderr, "usage: metalbench render|compute [seconds] [iterations] [width] [height] | submit [seconds]\n");
      return 64;
    }
    double secs = argc > 2 ? atof(argv[2]) : (isSubmit ? 10 : 20);
    uint32_t iters = argc > 3 ? (uint32_t)atoi(argv[3]) : 64;
    int W = argc > 4 ? atoi(argv[4]) : 2560, H = argc > 5 ? atoi(argv[5]) : 1440;
    if (isSubmit) { W = 16; H = 16; }

    id<MTLDevice> dev = nil;
    for (id<MTLDevice> d in MTLCopyAllDevices())
      if ([d.name containsString:@"Radeon"] || [d.name containsString:@"7900"]) dev = d;
    if (!dev) dev = MTLCreateSystemDefaultDevice();
    if (!dev) { fprintf(stderr, "NO METAL DEVICE\n"); return 1; }
    printf("device: %s; mode %s, %dx%d, %u iterations, %.0f s\n", dev.name.UTF8String, mode, W, H, iters, secs);

    NSError *err = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:kSrc options:nil error:&err];
    if (!lib) { fprintf(stderr, "LIBRARY COMPILE FAILED: %s\n", err.description.UTF8String); return 2; }

    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                  width:W height:H mipmapped:NO];
    td.storageMode = MTLStorageModePrivate;
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
    id<MTLTexture> target[3];
    for (int i = 0; i < 3; i++) target[i] = [dev newTextureWithDescriptor:td];

    id<MTLRenderPipelineState> rps = nil;
    id<MTLComputePipelineState> cps = nil;
    if (isCompute) {
      cps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"ck"] error:&err];
    } else {
      MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
      pd.vertexFunction = [lib newFunctionWithName:@"vs"];
      pd.fragmentFunction = [lib newFunctionWithName:isSubmit ? @"fs_flat" : @"fs"];
      pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
      rps = [dev newRenderPipelineStateWithDescriptor:pd error:&err];
    }
    if (!rps && !cps) { fprintf(stderr, "PIPELINE FAILED: %s\n", err.description.UTF8String); return 3; }

    id<MTLCommandQueue> q = [dev newCommandQueue];
    dispatch_semaphore_t inflight = dispatch_semaphore_create(isSubmit ? 1 : 3);
    long cap = 4000000, n = 0;
    double *wall = malloc(cap * sizeof(double)), *gpu = malloc(cap * sizeof(double));
    __block long ngpu = 0, nerr = 0, done = 0;
    __block double lastDone = 0;
    NSLock *lock = [NSLock new];

    double t0 = now_s(), tprev = t0;
    while (now_s() - t0 < secs && n < cap) {
      @autoreleasepool {
        dispatch_semaphore_wait(inflight, DISPATCH_TIME_FOREVER);
        Params p = {iters, (uint32_t)n, (float)W, (float)H};
        id<MTLCommandBuffer> cb = [q commandBuffer];
        id<MTLTexture> tex = target[n % 3];
        if (isCompute) {
          id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
          [e setComputePipelineState:cps];
          [e setTexture:tex atIndex:0];
          [e setBytes:&p length:sizeof p atIndex:0];
          [e dispatchThreadgroups:MTLSizeMake((W + 15) / 16, (H + 15) / 16, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
          [e endEncoding];
        } else {
          MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
          rp.colorAttachments[0].texture = tex;
          rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
          rp.colorAttachments[0].storeAction = MTLStoreActionStore;
          id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
          [e setRenderPipelineState:rps];
          if (!isSubmit) [e setFragmentBytes:&p length:sizeof p atIndex:0];
          [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
          [e endEncoding];
        }
        [cb addCompletedHandler:^(id<MTLCommandBuffer> b) {
          [lock lock];
          if (b.error) nerr++;
          double g = b.GPUEndTime - b.GPUStartTime;
          if (g > 0 && ngpu < cap) gpu[ngpu++] = g;
          done++;
          lastDone = now_s();
          [lock unlock];
          dispatch_semaphore_signal(inflight);
        }];
        [cb commit];
        if (isSubmit) [cb waitUntilCompleted];
        double t = now_s();
        wall[n++] = t - tprev;
        tprev = t;
      }
    }
    int slots = isSubmit ? 1 : 3;   // drain, then hand the slots back: libdispatch traps on a semaphore freed below its start value
    for (int i = 0; i < slots; i++) dispatch_semaphore_wait(inflight, DISPATCH_TIME_FOREVER);
    for (int i = 0; i < slots; i++) dispatch_semaphore_signal(inflight);
    double total = lastDone - t0;
    printf("frames %ld in %.2f s = %.1f frames/s; GPU errors %ld\n", done, total, done / total, nerr);
    report("wall per frame", wall, n);
    report("GPU per frame ", gpu, ngpu);
    return nerr ? 4 : 0;
  }
}
