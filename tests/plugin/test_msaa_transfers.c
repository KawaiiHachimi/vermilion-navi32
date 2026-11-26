// SPDX-License-Identifier: MIT
// Host-only refusal test. Build for the plugin's architecture:
// xcrun clang -arch x86_64 -I src/metal-plugin tests/plugin/test_msaa_transfers.c -o /tmp/test_msaa_transfers
// /tmp/test_msaa_transfers build/NVMTLDriver
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <unistd.h>
#include <stdio.h>
#include "nvmtl_vk.h"

// A regression in a staging transfer could try to initialize Vulkan. Stop as soon as the loader
// or ICD is mapped, before it can create a device or submit work with the fixture's handles.
static void stop_at_vulkan_load(const struct mach_header *image, intptr_t slide)
{
    (void)slide;
    Dl_info info = {0};
    if (dladdr(image, &info) && info.dli_fname && strstr(info.dli_fname, "/libvulkan")) {
        static const char message[] = "a multisample transfer reached Vulkan initialization\n";
        write(STDERR_FILENO, message, sizeof message - 1);
        _exit(1);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    _dyld_register_func_for_add_image(stop_at_vulkan_load);
    void *plugin = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!plugin) { fprintf(stderr, "%s\n", dlerror()); return 1; }

    // Exercise each exported production entry without opening a GPU or allocating staging memory.
    // No Vulkan commands can run in this process, even if the entry accidentally gets past validation.
#define LOAD(name) __typeof__(&nvmtl_vk_##name) name = dlsym(plugin, "nvmtl_vk_" #name); \
    if (!name) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    LOAD(image_write)
    LOAD(image_read)
    LOAD(image_write_region_level_layer)
    LOAD(image_read_region_level_layer)
    LOAD(cmd_copy_buffer_to_image_level_layer)
    LOAD(cmd_copy_image_to_buffer_level_layer)
#undef LOAD

    nvk_queue queue = {0};
    nvk_cmdbuf command = {0};
    nvk_buffer buffer = {.size = 4};
    uint32_t pixel = 0xa52f1381;
    nvk_image image = {.img = &pixel, .w = 1, .h = 1, .bpp = 4};
    for (unsigned samples = 2; samples <= 8; samples *= 2) {
        image.samples = samples;
        if (image_write(&queue, &image, &pixel, 4) != -1 ||
            image_read(&queue, &image, &pixel, 4) != -1 ||
            image_write_region_level_layer(&queue, &image, &pixel, 4, 0, 0, 1, 1, 0, 0) != -1 ||
            image_read_region_level_layer(&queue, &image, &pixel, 4, 0, 0, 1, 1, 0, 0) != -1 ||
            cmd_copy_buffer_to_image_level_layer(&command, &buffer, 0, 4, &image, 0, 0, 1, 1, 0, 0) != -1 ||
            cmd_copy_image_to_buffer_level_layer(&command, &image, 0, 0, 1, 1, &buffer, 0, 4, 0, 0) != -1 ||
            image.layout != 0 || pixel != 0xa52f1381) {
            fprintf(stderr, "%ux: accepted a buffer transfer or modified its inputs\n", samples);
            return 1;
        }
        printf("%ux: all six buffer/image entry points refused\n", samples);
    }
    return 0;
}
