// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vermilion contributors
//
// vkwindow: a Vulkan window on macOS through whatever Vulkan driver the loader finds: an NSWindow with a
// CAMetalLayer, VK_EXT_metal_surface, a swapchain (FIFO), <frames> frames cleared to a colour that changes every frame,
// then one last frame in a fixed colour (0x20, 0x80, 0xE0) that stays on screen while the window is captured with
// screencapture, so a run shows whether presented pixels reach the display. Prints the driver, the swapchain and the
// frame rate.
// Build: xcrun clang -arch x86_64 -fobjc-arc -O1 -o vkwindow tools/metaltest/vkwindow.m -I<Vulkan-Headers>/include \
//        -L<loader dir> -lvulkan -Wl,-rpath,<loader dir> -framework Cocoa -framework QuartzCore
// Run:   VK_DRIVER_FILES=<radeon_icd.json> ./vkwindow [frames, default 240] [capture.png]
#define VK_USE_PLATFORM_METAL_EXT 1
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include <vulkan/vulkan.h>
#include <mach/mach_time.h>

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { printf("FAIL %s -> %d\n", #x, r_); exit(1); } } while (0)

static double now_ms(void)
{
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    return (double)mach_absolute_time() * tb.numer / tb.denom / 1e6;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        const int frames = argc > 1 ? atoi(argv[1]) : 240;
        const char *capture = argc > 2 ? argv[2] : NULL;
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        NSWindow *win = [[NSWindow alloc] initWithContentRect:NSMakeRect(200, 200, 640, 360)
                                                    styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        win.title = @"vkwindow";
        CAMetalLayer *layer = [CAMetalLayer layer];
        win.contentView.wantsLayer = YES;
        win.contentView.layer = layer;
        layer.contentsScale = win.backingScaleFactor;
        [win orderFrontRegardless];
        [NSApp finishLaunching];

        const char *iext[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_METAL_SURFACE_EXTENSION_NAME};
        VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "vkwindow", .apiVersion = VK_API_VERSION_1_3};
        VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
                                    .enabledExtensionCount = 2, .ppEnabledExtensionNames = iext};
        VkInstance inst;
        CHECK(vkCreateInstance(&ici, NULL, &inst));
        uint32_t n = 1;
        VkPhysicalDevice pd;
        VkResult er = vkEnumeratePhysicalDevices(inst, &n, &pd);
        if ((er != VK_SUCCESS && er != VK_INCOMPLETE) || !n) { printf("FAIL no physical device (%d)\n", er); return 1; }
        VkPhysicalDeviceProperties pp;
        vkGetPhysicalDeviceProperties(pd, &pp);
        printf("device: %s (driver version 0x%x)\n", pp.deviceName, pp.driverVersion);

        VkMetalSurfaceCreateInfoEXT sci = {VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT, .pLayer = layer};
        VkSurfaceKHR surf;
        CHECK(vkCreateMetalSurfaceEXT(inst, &sci, NULL, &surf));
        uint32_t qf = UINT32_MAX, nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, NULL);
        VkQueueFamilyProperties qp[16];
        nq = nq > 16 ? 16 : nq;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, qp);
        for (uint32_t i = 0; i < nq && qf == UINT32_MAX; i++) {
            VkBool32 ok = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surf, &ok);
            if (ok && (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) qf = i;
        }
        if (qf == UINT32_MAX) { printf("FAIL no graphics queue that can present\n"); return 1; }
        const float prio = 1;
        VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = qf, .queueCount = 1, .pQueuePriorities = &prio};
        const char *dext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
                                  .enabledExtensionCount = 1, .ppEnabledExtensionNames = dext};
        VkDevice dev;
        CHECK(vkCreateDevice(pd, &dci, NULL, &dev));
        VkQueue q;
        vkGetDeviceQueue(dev, qf, 0, &q);

        VkSurfaceCapabilitiesKHR caps;
        CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surf, &caps));
        uint32_t nf = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surf, &nf, NULL);
        VkSurfaceFormatKHR fmts[32];
        nf = nf > 32 ? 32 : nf;
        vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surf, &nf, fmts);
        VkSurfaceFormatKHR fmt = fmts[0];
        for (uint32_t i = 0; i < nf; i++)
            if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) fmt = fmts[i];
        VkExtent2D ext = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : (VkExtent2D){1280, 720};
        printf("surface: %u formats (using %d), extent %ux%u, images %u..%u, usage 0x%x\n", nf, fmt.format, ext.width,
               ext.height, caps.minImageCount, caps.maxImageCount, caps.supportedUsageFlags);
        if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) { printf("FAIL swapchain images cannot be cleared\n"); return 1; }
        VkSwapchainCreateInfoKHR swci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = surf,
            .minImageCount = caps.minImageCount < 3 && (!caps.maxImageCount || caps.maxImageCount >= 3) ? 3 : caps.minImageCount,
            .imageFormat = fmt.format, .imageColorSpace = fmt.colorSpace, .imageExtent = ext, .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .preTransform = caps.currentTransform, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE};
        VkSwapchainKHR sc;
        CHECK(vkCreateSwapchainKHR(dev, &swci, NULL, &sc));
        uint32_t ni = 0;
        vkGetSwapchainImagesKHR(dev, sc, &ni, NULL);
        enum { kMaxImages = 8 };
        VkImage imgs[kMaxImages];
        ni = ni > kMaxImages ? kMaxImages : ni;
        vkGetSwapchainImagesKHR(dev, sc, &ni, imgs);
        printf("swapchain: %u images\n", ni);

        VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                       .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = qf};
        VkCommandPool pool;
        CHECK(vkCreateCommandPool(dev, &pci, NULL, &pool));
        enum { kInFlight = 2 };
        VkCommandBuffer cb[kInFlight];
        VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                                           .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = kInFlight};
        CHECK(vkAllocateCommandBuffers(dev, &cai, cb));
        VkSemaphore acq[kInFlight], done[kMaxImages];   // done: one per swapchain image
        VkFence fence[kInFlight];
        VkSemaphoreCreateInfo semi = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
        for (int i = 0; i < kInFlight; i++) {
            CHECK(vkCreateSemaphore(dev, &semi, NULL, &acq[i]));
            CHECK(vkCreateFence(dev, &fci, NULL, &fence[i]));
        }
        for (uint32_t i = 0; i < ni; i++) CHECK(vkCreateSemaphore(dev, &semi, NULL, &done[i]));

        const int total = frames + 1;
        double t0 = 0;
        for (int f = 0; f < total; f++) {
            if (f == 1) t0 = now_ms();   // the first frame includes setup
            const int s = f % kInFlight;
            CHECK(vkWaitForFences(dev, 1, &fence[s], VK_TRUE, UINT64_MAX));
            CHECK(vkResetFences(dev, 1, &fence[s]));
            uint32_t idx;
            VkResult ar = vkAcquireNextImageKHR(dev, sc, UINT64_MAX, acq[s], VK_NULL_HANDLE, &idx);
            if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) { printf("FAIL acquire frame %d -> %d\n", f, ar); return 1; }
            VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
            CHECK(vkBeginCommandBuffer(cb[s], &bi));
            VkImageSubresourceRange rng = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkImageMemoryBarrier b1 = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = imgs[idx], .subresourceRange = rng};
            vkCmdPipelineBarrier(cb[s], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b1);
            const float k = (float)(f % 120) / 119.0f;
            VkClearColorValue c = f == frames ? (VkClearColorValue){{0x20 / 255.0f, 0x80 / 255.0f, 0xE0 / 255.0f, 1}}
                                              : (VkClearColorValue){{k, 1 - k, 0.5f, 1}};
            vkCmdClearColorImage(cb[s], imgs[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &c, 1, &rng);
            VkImageMemoryBarrier b2 = b1;
            b2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b2.dstAccessMask = 0;
            b2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b2.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            vkCmdPipelineBarrier(cb[s], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &b2);
            CHECK(vkEndCommandBuffer(cb[s]));
            const VkPipelineStageFlags ws = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &acq[s],
                .pWaitDstStageMask = &ws, .commandBufferCount = 1, .pCommandBuffers = &cb[s],
                .signalSemaphoreCount = 1, .pSignalSemaphores = &done[idx]};
            CHECK(vkQueueSubmit(q, 1, &si, fence[s]));
            VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &done[idx],
                                   .swapchainCount = 1, .pSwapchains = &sc, .pImageIndices = &idx};
            VkResult pr = vkQueuePresentKHR(q, &pi);
            if (pr != VK_SUCCESS && pr != VK_SUBOPTIMAL_KHR) { printf("FAIL present frame %d -> %d\n", f, pr); return 1; }
            NSEvent *e;
            while ((e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:nil inMode:NSDefaultRunLoopMode dequeue:YES]))
                [NSApp sendEvent:e];
        }
        CHECK(vkDeviceWaitIdle(dev));
        const double ms = now_ms() - t0;
        printf("presented %d frames in %.1f ms: %.1f fps (FIFO)\n", frames, ms, frames ? frames * 1000.0 / ms : 0);
        if (capture) {
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.5]];
            char cmd[1200];
            snprintf(cmd, sizeof cmd, "/usr/sbin/screencapture -x -o -l %ld '%s'", (long)win.windowNumber, capture);
            printf("capture: %s -> rc %d (expect colour 20 80 e0)\n", capture, system(cmd));
        }
        vkDestroySwapchainKHR(dev, sc, NULL);
        vkDestroyDevice(dev, NULL);
        vkDestroySurfaceKHR(inst, surf, NULL);
        vkDestroyInstance(inst, NULL);
    }
    return 0;
}
