// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vermilion contributors
//
// vkbench: the Vulkan twin of tools/metaltest/metalbench.m, for the Linux reference and for RADV on macOS without
// the Metal plugin and the translator in between. The same Mandelbrot loop, the same sizes, the same output lines.
//
//   vkbench compute   [seconds=20] [iterations=64] [width=2560] [height=1440]
//       the Mandelbrot loop as a compute shader writing an RGBA8 storage image (metalbench compute)
//   vkbench submit    [seconds=10] [iterations=1] [width=16] [height=16]
//       one dispatch per command buffer, each waited for with a fence (metalbench submit): the cost of a submit and
//       a fence wait (CPU bound at the defaults; more iterations or a larger size give the wait a GPU job to wait for)
//   vkbench bandwidth [seconds=10] [MiB=256]
//       copy one buffer into another, 16 bytes per thread (metalbench bandwidth): GB/s read + written, which says
//       where the buffers live
//
// compute and bandwidth keep at most 3 command buffers in flight. GPU time per frame from timestamp queries.
// The driver: on Linux the system loader (libvulkan.so.1); on macOS RADV directly
// (/Library/GPUBundles/nvmtl/libvulkan_radeon.dylib, its vk_icdGetInstanceProcAddr, as the Metal plugin does).
// VKBENCH_DRIVER=<path> picks another library (a loader or an ICD; both entry points are tried).
// Exit codes: 0 ok, 1 no Vulkan device, 3 pipeline failed, 4 GPU error, 5 a Vulkan call failed.
// Build: scripts/build/vkbench.sh (macOS) or, on Linux,
//   cc -O2 -I<Vulkan-Headers>/include -o vkbench tools/vkbench/vkbench.c -ldl -lm
#define VK_NO_PROTOTYPES 1
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vkbench_spv.h"

#ifdef __APPLE__
#define DEFAULT_DRIVER "/Library/GPUBundles/nvmtl/libvulkan_radeon.dylib"
#else
#define DEFAULT_DRIVER "libvulkan.so.1"
#endif

static PFN_vkGetInstanceProcAddr gipa;
#define F(name) static PFN_##name name;
F(vkCreateInstance) F(vkEnumeratePhysicalDevices) F(vkGetPhysicalDeviceProperties) F(vkGetPhysicalDeviceQueueFamilyProperties)
F(vkGetPhysicalDeviceMemoryProperties) F(vkCreateDevice) F(vkGetDeviceProcAddr) F(vkGetDeviceQueue) F(vkCreateImage)
F(vkGetImageMemoryRequirements) F(vkAllocateMemory) F(vkBindImageMemory) F(vkCreateImageView) F(vkCreateBuffer)
F(vkGetBufferMemoryRequirements) F(vkBindBufferMemory) F(vkCreateShaderModule) F(vkCreateDescriptorSetLayout)
F(vkCreatePipelineLayout) F(vkCreateComputePipelines) F(vkCreateDescriptorPool) F(vkAllocateDescriptorSets)
F(vkUpdateDescriptorSets) F(vkCreateCommandPool) F(vkAllocateCommandBuffers) F(vkCreateFence) F(vkCreateQueryPool)
F(vkBeginCommandBuffer) F(vkEndCommandBuffer) F(vkCmdBindPipeline) F(vkCmdBindDescriptorSets) F(vkCmdPushConstants)
F(vkCmdDispatch) F(vkCmdPipelineBarrier) F(vkCmdResetQueryPool) F(vkCmdWriteTimestamp) F(vkQueueSubmit)
F(vkWaitForFences) F(vkResetFences) F(vkGetQueryPoolResults) F(vkResetCommandBuffer) F(vkDeviceWaitIdle)
F(vkGetFenceStatus)
#undef F

static double now_s(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
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
#define CK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "VK FAILED %d: %s\n", r_, #x); exit(5); } } while (0)

typedef struct { uint32_t iters, frame; float w, h; uint32_t n; } Params;

static uint32_t mem_type(const VkPhysicalDeviceMemoryProperties *mp, uint32_t bits, VkMemoryPropertyFlags want) {
  for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
    if ((bits & (1u << i)) && (mp->memoryTypes[i].propertyFlags & want) == want) return i;
  for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
    if (bits & (1u << i)) return i;
  return 0;
}

int main(int argc, char **argv) {
  const char *mode = argc > 1 ? argv[1] : "compute";
  int isSubmit = !strcmp(mode, "submit"), isBw = !strcmp(mode, "bandwidth");
  if (!isSubmit && !isBw && strcmp(mode, "compute")) {
    fprintf(stderr, "usage: vkbench compute [seconds] [iterations] [width] [height] | submit [seconds] | bandwidth [seconds] [MiB]\n");
    return 64;
  }
  double secs = argc > 2 ? atof(argv[2]) : (isSubmit || isBw ? 10 : 20);
  uint32_t iters = !isBw && argc > 3 ? (uint32_t)atoi(argv[3]) : isSubmit ? 1 : 64;
  int W = argc > 4 ? atoi(argv[4]) : 2560, H = argc > 5 ? atoi(argv[5]) : 1440;
  uint64_t bwBytes = (uint64_t)(isBw && argc > 3 ? atoi(argv[3]) : 256) << 20;
  if (isSubmit) { W = argc > 4 ? W : 16; H = argc > 5 ? H : 16; if (!iters) iters = 1; }

  const char *drv = getenv("VKBENCH_DRIVER");
  if (!drv || !*drv) drv = DEFAULT_DRIVER;
  void *lib = dlopen(drv, RTLD_NOW | RTLD_LOCAL);
  if (!lib) { fprintf(stderr, "NO VULKAN DRIVER: %s\n", dlerror()); return 1; }
  gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
  if (!gipa) gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
  if (!gipa) { fprintf(stderr, "NO VULKAN DRIVER: %s has no GetInstanceProcAddr\n", drv); return 1; }

  vkCreateInstance = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
  VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "vkbench", 1, NULL, 0, VK_API_VERSION_1_1};
  VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &ai, 0, NULL, 0, NULL};
  VkInstance inst;
  CK(vkCreateInstance(&ici, NULL, &inst));
#define I(name) name = (PFN_##name)gipa(inst, #name); if (!name) { fprintf(stderr, "missing %s\n", #name); return 5; }
  I(vkEnumeratePhysicalDevices) I(vkGetPhysicalDeviceProperties) I(vkGetPhysicalDeviceQueueFamilyProperties)
  I(vkGetPhysicalDeviceMemoryProperties) I(vkCreateDevice) I(vkGetDeviceProcAddr)
#undef I

  uint32_t np = 8; VkPhysicalDevice pds[8];
  CK(vkEnumeratePhysicalDevices(inst, &np, pds));
  VkPhysicalDevice pd = VK_NULL_HANDLE; VkPhysicalDeviceProperties props;
  for (uint32_t i = 0; i < np; i++) {   // the discrete AMD GPU (Linux may list the iGPU and llvmpipe too)
    vkGetPhysicalDeviceProperties(pds[i], &props);
    if (props.vendorID == 0x1002 && props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { pd = pds[i]; break; }
  }
  if (!pd && np) pd = pds[0];
  if (!pd) { fprintf(stderr, "NO VULKAN DEVICE\n"); return 1; }
  vkGetPhysicalDeviceProperties(pd, &props);
  uint32_t nq = 16; VkQueueFamilyProperties qf[16];
  vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, qf);
  uint32_t qfi = UINT32_MAX;
  for (uint32_t i = 0; i < nq; i++)
    if ((qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { qfi = i; break; }
  if (qfi == UINT32_MAX) { fprintf(stderr, "NO VULKAN DEVICE: no graphics+compute queue\n"); return 1; }
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(pd, &mp);
  if (isBw)
    printf("device: %s (driver %u.%u.%u); mode bandwidth, 2 x %llu MiB buffers, %.0f s\n", props.deviceName,
           VK_VERSION_MAJOR(props.driverVersion), VK_VERSION_MINOR(props.driverVersion), VK_VERSION_PATCH(props.driverVersion),
           (unsigned long long)(bwBytes >> 20), secs);
  else
    printf("device: %s (driver %u.%u.%u); mode %s, %dx%d, %u iterations, %.0f s\n", props.deviceName,
           VK_VERSION_MAJOR(props.driverVersion), VK_VERSION_MINOR(props.driverVersion), VK_VERSION_PATCH(props.driverVersion),
           mode, W, H, iters, secs);

  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, qfi, 1, &prio};
  VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qci, 0, NULL, 0, NULL, NULL};
  VkDevice dev;
  CK(vkCreateDevice(pd, &dci, NULL, &dev));
#define D(name) name = (PFN_##name)vkGetDeviceProcAddr(dev, #name); if (!name) { fprintf(stderr, "missing %s\n", #name); return 5; }
  D(vkGetDeviceQueue) D(vkCreateImage) D(vkGetImageMemoryRequirements) D(vkAllocateMemory) D(vkBindImageMemory)
  D(vkCreateImageView) D(vkCreateBuffer) D(vkGetBufferMemoryRequirements) D(vkBindBufferMemory) D(vkCreateShaderModule)
  D(vkCreateDescriptorSetLayout) D(vkCreatePipelineLayout) D(vkCreateComputePipelines) D(vkCreateDescriptorPool)
  D(vkAllocateDescriptorSets) D(vkUpdateDescriptorSets) D(vkCreateCommandPool) D(vkAllocateCommandBuffers)
  D(vkCreateFence) D(vkCreateQueryPool) D(vkBeginCommandBuffer) D(vkEndCommandBuffer) D(vkCmdBindPipeline)
  D(vkCmdBindDescriptorSets) D(vkCmdPushConstants) D(vkCmdDispatch) D(vkCmdPipelineBarrier) D(vkCmdResetQueryPool)
  D(vkCmdWriteTimestamp) D(vkQueueSubmit) D(vkWaitForFences) D(vkResetFences) D(vkGetQueryPoolResults)
  D(vkResetCommandBuffer) D(vkDeviceWaitIdle) D(vkGetFenceStatus)
#undef D
  VkQueue q;
  vkGetDeviceQueue(dev, qfi, 0, &q);

  enum { SLOTS = 3 };
  // the storage images (one per slot, like metalbench's 3 targets) or the two buffers
  VkImage img[SLOTS] = {0}; VkImageView view[SLOTS] = {0};
  VkBuffer buf[2] = {0};
  if (!isBw) {
    for (int s = 0; s < SLOTS; s++) {
      VkImageCreateInfo ii = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                              {(uint32_t)W, (uint32_t)H, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
                              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL,
                              VK_IMAGE_LAYOUT_UNDEFINED};
      CK(vkCreateImage(dev, &ii, NULL, &img[s]));
      VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, img[s], &mr);
      VkMemoryAllocateInfo ma = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size,
                                 mem_type(&mp, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
      VkDeviceMemory m; CK(vkAllocateMemory(dev, &ma, NULL, &m));
      CK(vkBindImageMemory(dev, img[s], m, 0));
      VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, img[s], VK_IMAGE_VIEW_TYPE_2D,
                                  VK_FORMAT_R8G8B8A8_UNORM, {0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
      CK(vkCreateImageView(dev, &vi, NULL, &view[s]));
    }
  } else {
    for (int b = 0; b < 2; b++) {
      VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, bwBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                               VK_SHARING_MODE_EXCLUSIVE, 0, NULL};
      CK(vkCreateBuffer(dev, &bi, NULL, &buf[b]));
      VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev, buf[b], &mr);
      uint32_t t = mem_type(&mp, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      if (b == 0) printf("buffer memory type %u: flags 0x%x, heap %u (%llu MiB, flags 0x%x)\n", t,
                         mp.memoryTypes[t].propertyFlags, mp.memoryTypes[t].heapIndex,
                         (unsigned long long)(mp.memoryHeaps[mp.memoryTypes[t].heapIndex].size >> 20),
                         mp.memoryHeaps[mp.memoryTypes[t].heapIndex].flags);
      VkMemoryAllocateInfo ma = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, t};
      VkDeviceMemory m; CK(vkAllocateMemory(dev, &ma, NULL, &m));
      CK(vkBindBufferMemory(dev, buf[b], m, 0));
    }
  }

  VkDescriptorSetLayoutBinding lb[3] = {
      {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
      {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}};
  VkDescriptorSetLayoutCreateInfo lci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0,
                                         isBw ? 2u : 1u, isBw ? lb + 1 : lb};
  VkDescriptorSetLayout dsl; CK(vkCreateDescriptorSetLayout(dev, &lci, NULL, &dsl));
  VkPushConstantRange pcr = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Params)};
  VkPipelineLayoutCreateInfo plci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &dsl, 1, &pcr};
  VkPipelineLayout pl; CK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
  VkShaderModuleCreateInfo smci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0,
                                   isBw ? sizeof spv_stream : sizeof spv_mandel, isBw ? spv_stream : spv_mandel};
  VkShaderModule sm; CK(vkCreateShaderModule(dev, &smci, NULL, &sm));
  VkComputePipelineCreateInfo cpci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, NULL, 0,
                                      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
                                       VK_SHADER_STAGE_COMPUTE_BIT, sm, "main", NULL}, pl, VK_NULL_HANDLE, -1};
  VkPipeline pipe;
  if (vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipe) != VK_SUCCESS) {
    fprintf(stderr, "PIPELINE FAILED\n"); return 3;
  }

  VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, SLOTS}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2}};
  VkDescriptorPoolCreateInfo dpci = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, SLOTS, 2, ps};
  VkDescriptorPool dp; CK(vkCreateDescriptorPool(dev, &dpci, NULL, &dp));
  VkDescriptorSet ds[SLOTS];
  int nsets = isBw ? 1 : SLOTS;
  for (int s = 0; s < nsets; s++) {
    VkDescriptorSetAllocateInfo dsai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, dp, 1, &dsl};
    CK(vkAllocateDescriptorSets(dev, &dsai, &ds[s]));
    if (!isBw) {
      VkDescriptorImageInfo dii = {VK_NULL_HANDLE, view[s], VK_IMAGE_LAYOUT_GENERAL};
      VkWriteDescriptorSet w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ds[s], 0, 0, 1,
                                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dii, NULL, NULL};
      vkUpdateDescriptorSets(dev, 1, &w, 0, NULL);
    } else {
      VkDescriptorBufferInfo dbi[2] = {{buf[0], 0, VK_WHOLE_SIZE}, {buf[1], 0, VK_WHOLE_SIZE}};
      VkWriteDescriptorSet w[2] = {
          {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ds[s], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &dbi[0], NULL},
          {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ds[s], 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &dbi[1], NULL}};
      vkUpdateDescriptorSets(dev, 2, w, 0, NULL);
    }
  }

  VkCommandPoolCreateInfo cpi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL,
                                 VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, qfi};
  VkCommandPool pool; CK(vkCreateCommandPool(dev, &cpi, NULL, &pool));
  VkCommandBuffer cb[SLOTS];
  VkCommandBufferAllocateInfo cbai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool,
                                      VK_COMMAND_BUFFER_LEVEL_PRIMARY, SLOTS};
  CK(vkAllocateCommandBuffers(dev, &cbai, cb));
  VkFence fence[SLOTS];
  VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT};
  for (int s = 0; s < SLOTS; s++) CK(vkCreateFence(dev, &fci, NULL, &fence[s]));
  int haveTs = props.limits.timestampComputeAndGraphics && qf[qfi].timestampValidBits;
  VkQueryPool qp = VK_NULL_HANDLE;
  if (haveTs) {
    VkQueryPoolCreateInfo qpci = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, NULL, 0, VK_QUERY_TYPE_TIMESTAMP, 2 * SLOTS, 0};
    CK(vkCreateQueryPool(dev, &qpci, NULL, &qp));
  }
  double tsPeriod = props.limits.timestampPeriod * 1e-9;   // seconds per tick

  int slots = isSubmit ? 1 : SLOTS;
  long cap = 4000000, n = 0, done = 0;
  double *wall = malloc(cap * sizeof(double)), *gpu = malloc(cap * sizeof(double));
  long ngpu = 0;
  int pending[SLOTS] = {0};
  uint32_t nvec = (uint32_t)(bwBytes / 16);
  double lastDone = 0;

  // harvest a slot: wait for its fence, take its GPU time
  #define HARVEST(s) do { \
      CK(vkWaitForFences(dev, 1, &fence[s], VK_TRUE, UINT64_MAX)); \
      if (pending[s]) { \
        if (haveTs) { uint64_t t2[2]; \
          if (vkGetQueryPoolResults(dev, qp, 2 * (s), 2, sizeof t2, t2, 8, VK_QUERY_RESULT_64_BIT) == VK_SUCCESS && \
              t2[1] > t2[0] && ngpu < cap) gpu[ngpu++] = (double)(t2[1] - t2[0]) * tsPeriod; } \
        pending[s] = 0; done++; lastDone = now_s(); } } while (0)

  double t0 = now_s(), tprev = t0;
  while (now_s() - t0 < secs && n < cap) {
    int s = (int)(n % slots);
    HARVEST(s);
    CK(vkResetFences(dev, 1, &fence[s]));
    CK(vkResetCommandBuffer(cb[s], 0));
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
                                   VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL};
    CK(vkBeginCommandBuffer(cb[s], &bi));
    if (haveTs) {
      vkCmdResetQueryPool(cb[s], qp, 2 * s, 2);
      vkCmdWriteTimestamp(cb[s], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, qp, 2 * s);
    }
    if (!isBw) {   // UNDEFINED -> GENERAL each frame (contents not kept, like metalbench's DontCare)
      VkImageMemoryBarrier ib = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0, VK_ACCESS_SHADER_WRITE_BIT,
                                 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED,
                                 VK_QUEUE_FAMILY_IGNORED, img[s], {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
      vkCmdPipelineBarrier(cb[s], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL,
                           0, NULL, 1, &ib);
    }
    Params p = {iters, (uint32_t)n, (float)W, (float)H, nvec};
    vkCmdBindPipeline(cb[s], VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vkCmdBindDescriptorSets(cb[s], VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds[isBw ? 0 : s], 0, NULL);
    vkCmdPushConstants(cb[s], pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof p, &p);
    if (isBw) vkCmdDispatch(cb[s], (nvec + 255) / 256, 1, 1);
    else vkCmdDispatch(cb[s], (W + 15) / 16, (H + 15) / 16, 1);
    if (haveTs) vkCmdWriteTimestamp(cb[s], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, 2 * s + 1);
    CK(vkEndCommandBuffer(cb[s]));
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cb[s], 0, NULL};
    CK(vkQueueSubmit(q, 1, &si, fence[s]));
    pending[s] = 1;
    if (isSubmit) HARVEST(s);
    double t = now_s();
    wall[n++] = t - tprev;
    tprev = t;
  }
  for (int s = 0; s < slots; s++) HARVEST(s);
  CK(vkDeviceWaitIdle(dev));
  double total = lastDone - t0;
  printf("frames %ld in %.2f s = %.1f frames/s; GPU errors 0\n", done, total, done / total);
  report("wall per frame", wall, n);
  report("GPU per frame ", gpu, ngpu);
  if (isBw && ngpu) {   // report() sorted gpu[]
    printf("bandwidth GB/s (read + write, by GPU time): median %.1f best %.1f\n",
           2.0 * bwBytes / gpu[ngpu / 2] / 1e9, 2.0 * bwBytes / gpu[0] / 1e9);
  }
  return 0;
}
