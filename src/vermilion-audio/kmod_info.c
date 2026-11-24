// SPDX-License-Identifier: MIT
//
//  kmod_info.c: XNU kmod glue for VermilionAudio.kext (_start/_stop come from libkmod.a; the real entry points are
//  Lilu's plugin_start.cpp).
//
#include <mach/mach_types.h>
#include <libkern/OSKextLib.h>

extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);
extern kern_return_t VermilionAudio_kern_start(kmod_info_t *, void *);
extern kern_return_t VermilionAudio_kern_stop(kmod_info_t *, void *);

__attribute__((visibility("default")))
KMOD_EXPLICIT_DECL(org.vermilion.audio, "1.6.0", _start, _stop)

__private_extern__ kmod_start_func_t *_realmain = VermilionAudio_kern_start;
__private_extern__ kmod_stop_func_t  *_antimain = VermilionAudio_kern_stop;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
