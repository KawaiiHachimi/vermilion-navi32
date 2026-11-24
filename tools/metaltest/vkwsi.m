// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vermilion contributors
//
// vkwsi: does this RADV present to a real macOS window? No Vulkan loader is installed on the target, so this
// opens the ICD directly (dlopen + vkGetInstanceProcAddr, as tools/vkbench does), makes an NSWindow with a
// CAMetalLayer, creates a VK_EXT_metal_surface, a FIFO swapchain, clears each frame to a colour that changes per
// frame and presents it. It prints the ICD's surface/surface-maintenance extensions so a missing macOS WSI shows up
// as a list rather than a crash.
//
// Build (any Mac, cross to x86_64):
//   xcrun clang -arch x86_64 -mmacosx-version-min=26.0 -fobjc-arc -O1 -o vkwsi tools/metaltest/vkwsi.m \
//     -I<vulkan headers include> -framework Cocoa -framework QuartzCore -framework IOSurface
// Run (on the target):
//   ./vkwsi /Library/GPUBundles/nvmtl/libvulkan_radeon.dylib [frames] [capture.png]
#define VK_USE_PLATFORM_METAL_EXT 1
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <mach/mach_time.h>

static PFN_vkGetInstanceProcAddr gipa;
#define G(inst, name) ((PFN_##name)gipa((inst), #name))
#define GD(dev, name) ((PFN_##name)gdp((dev), #name))
static PFN_vkGetDeviceProcAddr gdp;
#define CK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { printf("FAIL %s -> %d\n", #x, r_); return 1; } } while (0)

int main(int argc, char **argv)
{
    @autoreleasepool {
        const char *drv = argc > 1 ? argv[1] : "/Library/GPUBundles/nvmtl/libvulkan_radeon.dylib";
        const int frames = argc > 2 ? atoi(argv[2]) : 240;
        const char *capture = argc > 3 ? argv[3] : NULL;

        void *lib = dlopen(drv, RTLD_NOW | RTLD_LOCAL);
        if (!lib) { printf("FAIL dlopen %s: %s\n", drv, dlerror()); return 1; }
        gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
        if (!gipa) gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
        if (!gipa) { printf("FAIL no vkGetInstanceProcAddr in %s\n", drv); return 1; }
        printf("icd: %s\n", drv);

        // Instance with the Metal surface extension when the ICD has it.
        PFN_vkEnumerateInstanceExtensionProperties eiep = G(NULL, vkEnumerateInstanceExtensionProperties);
        uint32_t nex = 0;
        if (eiep) eiep(NULL, &nex, NULL);
        VkExtensionProperties ex[128];
        if (nex > 128) nex = 128;
        uint32_t metalSurf = 0;
        if (eiep && nex) {
            eiep(NULL, &nex, ex);
            printf("instance extensions (%u):", nex);
            for (uint32_t i = 0; i < nex; i++) {
                if (!strcmp(ex[i].extensionName, VK_EXT_METAL_SURFACE_EXTENSION_NAME)) metalSurf = 1;
                if (!strncmp(ex[i].extensionName, "VK_KHR_surface", 14) || !strncmp(ex[i].extensionName, "VK_EXT_metal", 12))
                    printf(" %s", ex[i].extensionName);
            }
            printf("\n");
        }
        if (!metalSurf) { printf("FAIL the ICD does not offer %s: no macOS window surface\n", VK_EXT_METAL_SURFACE_EXTENSION_NAME); return 1; }

        const char *iext[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_METAL_SURFACE_EXTENSION_NAME};
        VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "vkwsi", .apiVersion = VK_API_VERSION_1_3};
        VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
                                    .enabledExtensionCount = 2, .ppEnabledExtensionNames = iext};
        VkInstance inst = VK_NULL_HANDLE;
        CK(G(NULL, vkCreateInstance)(&ici, NULL, &inst));
        gdp = G(inst, vkGetDeviceProcAddr);

        // Window + layer, as any macOS app would make one.
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        NSWindow *win = [[NSWindow alloc] initWithContentRect:NSMakeRect(200, 200, 640, 360)
                                                    styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        win.title = @"vkwsi";
        CAMetalLayer *layer = [CAMetalLayer layer];
        win.contentView.wantsLayer = YES;
        win.contentView.layer = layer;
        layer.contentsScale = win.backingScaleFactor;
        [win orderFrontRegardless];
        [NSApp finishLaunching];

        uint32_t n = 1;
        VkPhysicalDevice pd;
        VkResult er = G(inst, vkEnumeratePhysicalDevices)(inst, &n, &pd);
        if ((er != VK_SUCCESS && er != VK_INCOMPLETE) || !n) { printf("FAIL no physical device (%d)\n", er); return 1; }
        VkPhysicalDeviceProperties pp;
        G(inst, vkGetPhysicalDeviceProperties)(pd, &pp);
        printf("device: %s\n", pp.deviceName);

        VkMetalSurfaceCreateInfoEXT sci = {VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT, .pLayer = layer};
        VkSurfaceKHR surf = VK_NULL_HANDLE;
        CK(G(inst, vkCreateMetalSurfaceEXT)(inst, &sci, NULL, &surf));

        uint32_t nq = 0;
        G(inst, vkGetPhysicalDeviceQueueFamilyProperties)(pd, &nq, NULL);
        VkQueueFamilyProperties qp[16];
        if (nq > 16) nq = 16;
        G(inst, vkGetPhysicalDeviceQueueFamilyProperties)(pd, &nq, qp);
        uint32_t qf = UINT32_MAX;
        for (uint32_t i = 0; i < nq && qf == UINT32_MAX; i++) {
            VkBool32 ok = VK_FALSE;
            G(inst, vkGetPhysicalDeviceSurfaceSupportKHR)(pd, i, surf, &ok);
            if (ok && (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) qf = i;
        }
        if (qf == UINT32_MAX) { printf("FAIL no graphics queue that can present to the layer\n"); return 1; }
        printf("present queue family: %u\n", qf);

        const float prio = 1;
        VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = qf, .queueCount = 1, .pQueuePriorities = &prio};
        const char *dext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
                                  .enabledExtensionCount = 1, .ppEnabledExtensionNames = dext};
        VkDevice dev = VK_NULL_HANDLE;
        CK(G(inst, vkCreateDevice)(pd, &dci, NULL, &dev));
        VkQueue q;
        G(inst, vkGetDeviceQueue)(dev, qf, 0, &q);

        VkSurfaceCapabilitiesKHR caps;
        CK(G(inst, vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(pd, surf, &caps));
        printf("surface: %ux%u, %u..%u images, currentTransform %u\n", caps.currentExtent.width, caps.currentExtent.height,
               caps.minImageCount, caps.maxImageCount, caps.currentTransform);

        VkSwapchainCreateInfoKHR swci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        swci.surface = surf;
        swci.minImageCount = caps.minImageCount + 1;
        if (caps.maxImageCount && swci.minImageCount > caps.maxImageCount) swci.minImageCount = caps.maxImageCount;
        swci.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
        swci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        swci.imageExtent = caps.currentExtent;
        swci.imageArrayLayers = 1;
        swci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swci.preTransform = caps.currentTransform;
        swci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swci.clipped = VK_TRUE;
        VkSwapchainKHR sc = VK_NULL_HANDLE;
        CK(GD(dev, vkCreateSwapchainKHR)(dev, &swci, NULL, &sc));

        uint32_t ni = 0;
        GD(dev, vkGetSwapchainImagesKHR)(dev, sc, &ni, NULL);
        VkImage img[8];
        if (ni > 8) ni = 8;
        GD(dev, vkGetSwapchainImagesKHR)(dev, sc, &ni, img);
        printf("swapchain: %u images, %ux%u, FIFO\n", ni, swci.imageExtent.width, swci.imageExtent.height);

        VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = qf};
        VkCommandPool pool;
        CK(GD(dev, vkCreateCommandPool)(dev, &pci, NULL, &pool));
        VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
        VkCommandBuffer cb;
        CK(GD(dev, vkAllocateCommandBuffers)(dev, &cai, &cb));
        VkSemaphoreCreateInfo smci = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore acquired, done;
        CK(GD(dev, vkCreateSemaphore)(dev, &smci, NULL, &acquired));
        CK(GD(dev, vkCreateSemaphore)(dev, &smci, NULL, &done));
        VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence;
        CK(GD(dev, vkCreateFence)(dev, &fci, NULL, &fence));

        const double t0 = (double)clock() / CLOCKS_PER_SEC;
        int presented = 0;
        for (int f = 0; f < frames; f++) {
            uint32_t idx = 0;
            VkResult ar = GD(dev, vkAcquireNextImageKHR)(dev, sc, UINT64_MAX, acquired, VK_NULL_HANDLE, &idx);
            if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) { printf("FAIL acquire -> %d at frame %d\n", ar, f); break; }
            VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
            CK(GD(dev, vkBeginCommandBuffer)(cb, &bi));
            VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = img[idx];
            b.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            GD(dev, vkCmdPipelineBarrier)(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
            const float last = (f == frames - 1);
            VkClearColorValue col = {{ last ? 0.125f : (float)(f % 60) / 60.0f, 0.5f, 0.875f, 1.0f }};
            GD(dev, vkCmdClearColorImage)(cb, img[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &col, 1, &b.subresourceRange);
            b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = 0;
            GD(dev, vkCmdPipelineBarrier)(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &b);
            CK(GD(dev, vkEndCommandBuffer)(cb));
            VkPipelineStageFlags wait = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &acquired,
                               .pWaitDstStageMask = &wait, .commandBufferCount = 1, .pCommandBuffers = &cb,
                               .signalSemaphoreCount = 1, .pSignalSemaphores = &done};
            CK(GD(dev, vkQueueSubmit)(q, 1, &si, fence));
            CK(GD(dev, vkWaitForFences)(dev, 1, &fence, VK_TRUE, UINT64_MAX));
            CK(GD(dev, vkResetFences)(dev, 1, &fence));
            VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &done,
                                   .swapchainCount = 1, .pSwapchains = &sc, .pImageIndices = &idx};
            VkResult pr = GD(dev, vkQueuePresentKHR)(q, &pi);
            if (pr != VK_SUCCESS && pr != VK_SUBOPTIMAL_KHR) { printf("FAIL present -> %d at frame %d\n", pr, f); break; }
            presented++;
            if (last && capture) {
                system([[NSString stringWithFormat:@"screencapture -x %s", capture] UTF8String]);
                printf("captured %s\n", capture);
                sleep(2);
            }
        }
        const double dt = (double)clock() / CLOCKS_PER_SEC - t0;
        printf("presented %d/%d frames in %.2f s (%.1f fps)\n", presented, frames, dt, presented / (dt > 0 ? dt : 1));
        printf(presented == frames ? "WSI OK\n" : "WSI INCOMPLETE\n");
        return presented == frames ? 0 : 1;
    }
}
