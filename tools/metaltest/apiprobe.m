// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vermilion contributors
//
// apiprobe: what each graphics API gives a normal app on this machine. Prints the Metal device's limits and feature
// flags, which renderer OpenGL (CGL) picks, the OpenCL devices, Core Image's default context and whether MetalFX
// accepts the device. Reads only; draws nothing.
// Build: xcrun clang -arch x86_64 -fobjc-arc -O1 -Wno-deprecated-declarations -o apiprobe tools/metaltest/apiprobe.m \
//        -framework Foundation -framework Metal -framework OpenGL -framework OpenCL -framework CoreImage
#define GL_SILENCE_DEPRECATION 1
#define CL_SILENCE_DEPRECATION 1
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl3.h>
#import <OpenCL/opencl.h>
#import <CoreImage/CoreImage.h>
#include <dlfcn.h>
#include <objc/message.h>
static void gl(const char *name, CGLPixelFormatAttribute *a) {
  CGLPixelFormatObj pf = NULL; GLint n = 0; CGLError e = CGLChoosePixelFormat(a, &pf, &n);
  if (e || !pf) { printf("GL %s: no pixel format (%s)\n", name, CGLErrorString(e)); return; }
  CGLContextObj c = NULL; e = CGLCreateContext(pf, NULL, &c);
  if (e || !c) { printf("GL %s: no context (%s)\n", name, CGLErrorString(e)); return; }
  CGLSetCurrentContext(c);
  printf("GL %s: vendor '%s' renderer '%s' version '%s'\n", name, glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
  CGLSetCurrentContext(NULL); CGLDestroyContext(c); CGLDestroyPixelFormat(pf);
}
int main() { @autoreleasepool {
  for (id<MTLDevice> d in MTLCopyAllDevices()) {
    printf("Metal: %s\n lowPower %d headless %d removable %d unified %d location %lu\n", d.name.UTF8String, d.isLowPower, d.isHeadless, d.isRemovable, d.hasUnifiedMemory, (unsigned long)d.location);
    printf(" recommendedMaxWorkingSetSize %llu MiB, maxBufferLength %lu MiB, currentAllocatedSize %lu MiB\n", d.recommendedMaxWorkingSetSize >> 20, (unsigned long)(d.maxBufferLength >> 20), (unsigned long)(d.currentAllocatedSize >> 20));
    printf(" families: common1 %d common2 %d common3 %d mac2 %d metal3 %d metal4 %d apple1 %d\n",
      [d supportsFamily:MTLGPUFamilyCommon1], [d supportsFamily:MTLGPUFamilyCommon2], [d supportsFamily:MTLGPUFamilyCommon3], [d supportsFamily:MTLGPUFamilyMac2], [d supportsFamily:MTLGPUFamilyMetal3], [d supportsFamily:(MTLGPUFamily)5002], [d supportsFamily:MTLGPUFamilyApple1]);
    printf(" raytracing %d (from render %d) functionPointers %d dynamicLibraries %d renderDynamicLibraries %d\n", d.supportsRaytracing, d.supportsRaytracingFromRender, d.supportsFunctionPointers, d.supportsDynamicLibraries, d.supportsRenderDynamicLibraries);
    printf(" BC %d depth24stencil8 %d 32bitMSAA %d 32bitFloatFiltering %d queryTextureLOD %d pullModelInterp %d shaderBarycentric %d\n", d.supportsBCTextureCompression, d.isDepth24Stencil8PixelFormatSupported, d.supports32BitMSAA, d.supports32BitFloatFiltering, d.supportsQueryTextureLOD, d.supportsPullModelInterpolation, d.supportsShaderBarycentricCoordinates);
    printf(" argumentBuffers tier %lu readWriteTexture tier %lu rasterOrderGroups %d programmableSamplePositions %d\n", (unsigned long)d.argumentBuffersSupport, (unsigned long)d.readWriteTextureSupport, d.rasterOrderGroupsSupported, d.programmableSamplePositionsSupported);
    printf(" sparseTileSizeInBytes %lu; maxThreadsPerThreadgroup %lux%lux%lu; samples: 2 %d 4 %d 8 %d 16 %d\n", (unsigned long)d.sparseTileSizeInBytes, (unsigned long)d.maxThreadsPerThreadgroup.width, (unsigned long)d.maxThreadsPerThreadgroup.height, (unsigned long)d.maxThreadsPerThreadgroup.depth, [d supportsTextureSampleCount:2], [d supportsTextureSampleCount:4], [d supportsTextureSampleCount:8], [d supportsTextureSampleCount:16]);
    printf(" counter sets: %s\n", [[d.counterSets valueForKey:@"name"] componentsJoinedByString:@", "].UTF8String ?: "none");
  }
  CGLPixelFormatAttribute acc[] = {kCGLPFAAccelerated, kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core, 0};
  CGLPixelFormatAttribute any[] = {kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core, 0};
  CGLPixelFormatAttribute leg[] = {0};
  gl("4.1 core, accelerated only", acc); gl("4.1 core, any renderer", any); gl("legacy, any renderer", leg);
  cl_platform_id pl[4]; cl_uint np = 0; clGetPlatformIDs(4, pl, &np);
  for (cl_uint i = 0; i < np; i++) { cl_device_id dv[8]; cl_uint nd = 0; clGetDeviceIDs(pl[i], CL_DEVICE_TYPE_ALL, 8, dv, &nd);
    for (cl_uint j = 0; j < nd; j++) { char nm[256] = ""; cl_device_type t = 0; clGetDeviceInfo(dv[j], CL_DEVICE_NAME, 256, nm, NULL); clGetDeviceInfo(dv[j], CL_DEVICE_TYPE, sizeof t, &t, NULL);
      printf("OpenCL device: '%s' type %s\n", nm, t & CL_DEVICE_TYPE_GPU ? "GPU" : t & CL_DEVICE_TYPE_CPU ? "CPU" : "other"); } }
  if (!np) printf("OpenCL: no platform\n");
  CIContext *ci = [CIContext contextWithOptions:nil];
  printf("Core Image default context: %s\n", ci.description.UTF8String);
  const char *fw[] = {"MetalFX", "MetalPerformanceShaders", "MetalPerformanceShadersGraph", "VideoToolbox"};
  for (int i = 0; i < 4; i++) { char p[256]; snprintf(p, 256, "/System/Library/Frameworks/%s.framework/%s", fw[i], fw[i]); printf("%s loads: %d\n", fw[i], dlopen(p, RTLD_LAZY) != NULL); }
  Class sc = NSClassFromString(@"MTLFXSpatialScalerDescriptor"); 
  if (sc) for (id<MTLDevice> d in MTLCopyAllDevices()) printf("MetalFX spatial supported: %d, temporal: %d\n", (int)((BOOL(*)(id,SEL,id))objc_msgSend)(sc, sel_registerName("supportsDevice:"), d), (int)((BOOL(*)(id,SEL,id))objc_msgSend)(NSClassFromString(@"MTLFXTemporalScalerDescriptor"), sel_registerName("supportsDevice:"), d));
} return 0; }
