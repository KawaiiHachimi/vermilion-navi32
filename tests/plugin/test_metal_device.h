// SPDX-License-Identifier: MIT
#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#import <Metal/Metal.h>
#include <dlfcn.h>
#include <objc/runtime.h>

@protocol ProbeDeviceInit <MTLDevice>
- (instancetype)initWithAcceleratorPort:(mach_port_t)port;
@end

static id<MTLDevice> openDevice(const char *path)
{
    if (!path) return MTLCreateSystemDefaultDevice();
    if (!dlopen(path, RTLD_NOW | RTLD_LOCAL)) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return nil;
    }
    Class cls = NSClassFromString(@"NVMTLDevice");
    // Metal can load an installed class of the same name. Require the intended executable.
    const char *loaded = cls ? class_getImageName(cls) : NULL;
    char expected[PATH_MAX], actual[PATH_MAX];
    if (!loaded || !realpath(path, expected) || !realpath(loaded, actual) || strcmp(expected, actual)) {
        fprintf(stderr, "requested plugin %s, resolved class in %s\n", path, loaded ?: "(none)");
        return nil;
    }
    io_service_t accel = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("NMXAccel"));
    if (!accel) return nil;
    id<MTLDevice> dev = [(id<ProbeDeviceInit>)[cls alloc] initWithAcceleratorPort:accel];
    IOObjectRelease(accel);
    return dev;
}
