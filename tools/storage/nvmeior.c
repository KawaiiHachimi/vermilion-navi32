// SPDX-License-Identifier: MIT
// nvmeior: sample the IOReport "NVMe" group (power-state residency, Apple bandwidth-throttle tiers) twice,
// N seconds apart, and print each channel's delta (ABS=1: the absolute values of the second sample).
// Build: clang -O2 -o nvmeior nvmeior.c -framework CoreFoundation -lIOReport   Usage: ./nvmeior [seconds]
// IOReport is a private Apple library; the prototypes below are the ones powermetrics-like tools use.
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
typedef struct IOReportSubscriptionRef *IOReportSubscriptionRef;
extern CFDictionaryRef IOReportCopyChannelsInGroup(CFStringRef, CFStringRef, uint64_t, uint64_t, uint64_t);
extern IOReportSubscriptionRef IOReportCreateSubscription(void*, CFMutableDictionaryRef, CFMutableDictionaryRef*, uint64_t, CFTypeRef);
extern CFDictionaryRef IOReportCreateSamples(IOReportSubscriptionRef, CFMutableDictionaryRef, CFTypeRef);
extern CFDictionaryRef IOReportCreateSamplesDelta(CFDictionaryRef, CFDictionaryRef, CFTypeRef);
extern CFStringRef IOReportChannelGetChannelName(CFDictionaryRef);
extern CFStringRef IOReportChannelGetSubGroup(CFDictionaryRef);
extern int IOReportChannelGetFormat(CFDictionaryRef);
extern int64_t IOReportSimpleGetIntegerValue(CFDictionaryRef, int32_t*);
extern int32_t IOReportStateGetCount(CFDictionaryRef);
extern CFStringRef IOReportStateGetNameForIndex(CFDictionaryRef, int32_t);
extern int64_t IOReportStateGetResidency(CFDictionaryRef, int32_t);
static void pr(CFStringRef s){ char b[256]; if(!s){printf("-");return;} CFStringGetCString(s,b,sizeof b,kCFStringEncodingUTF8); printf("%s",b);}
int main(int argc,char**argv){
  int secs = argc>1?atoi(argv[1]):5;
  CFDictionaryRef ch = IOReportCopyChannelsInGroup(CFSTR("NVMe"), NULL, 0,0,0);
  CFMutableDictionaryRef mch = CFDictionaryCreateMutableCopy(NULL,0,ch);
  CFMutableDictionaryRef sub_ch=NULL;
  IOReportSubscriptionRef sub = IOReportCreateSubscription(NULL, mch, &sub_ch, 0, NULL);
  CFDictionaryRef a = IOReportCreateSamples(sub, sub_ch, NULL);
  sleep(secs);
  CFDictionaryRef b = IOReportCreateSamples(sub, sub_ch, NULL);
  CFDictionaryRef d = getenv("ABS") ? b : IOReportCreateSamplesDelta(a,b,NULL);
  CFArrayRef arr = CFDictionaryGetValue(d, CFSTR("IOReportChannels"));
  for (CFIndex i=0; arr && i<CFArrayGetCount(arr); i++){
    CFDictionaryRef c = CFArrayGetValueAtIndex(arr,i);
    pr(IOReportChannelGetSubGroup(c)); printf(" | "); pr(IOReportChannelGetChannelName(c));
    int f = IOReportChannelGetFormat(c);
    if (f==1) printf(" = %lld\n", IOReportSimpleGetIntegerValue(c,NULL));
    else if (f==2){ printf(":"); for(int s=0;s<IOReportStateGetCount(c);s++){ printf(" ["); pr(IOReportStateGetNameForIndex(c,s)); printf("]=%lld", IOReportStateGetResidency(c,s)); } printf("\n"); }
    else printf(" (format %d)\n", f);
  }
  return 0;
}
