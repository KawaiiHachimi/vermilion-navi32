// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vermilion contributors
//
// wgsamples: runs the WebGPU samples (webgpu.github.io/webgpu-samples) one after another in an on-screen WKWebView
// (Safari's WebKit GPU process) and prints one JSON line per sample: the page's frame rate (requestAnimationFrame
// callbacks per second over the last part of the run), the first console errors, uncaptured WebGPU errors, device-lost
// and unhandled rejections, and a capture of the window (PNG) for checking the picture by eye. Needs network access.
// Build: xcrun clang -arch x86_64 -fobjc-arc -O1 -o wgsamples tools/metaltest/wgsamples.m -framework Cocoa -framework WebKit
// Run:   ./wgsamples <out dir> [seconds per sample, default 6] [sample ...]   (no names: every sample in the list below)
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

static NSArray<NSString *> *allSamples(void) {
  // the samples deployed on webgpu.github.io (the gh-pages branch, 2026-11-23); main has more that are not deployed
  return [@"a-buffer animometer bitonicSort blending cameras computeBoids cornell cubemap deferredRendering fractalCube "
           "gameOfLife generateMipmap helloTriangle helloTriangleMSAA imageBlur instancedCube multipleCanvases normalMap "
           "occlusionQuery packedIntegerDotProduct particles points primitivePicking renderBundles resizeCanvas "
           "resizeObserverHDDPI reversedZ rotatingCube samplerParameters shadowMapping skinnedMesh stencilMask "
           "textRenderingMsdf texturedCube timestampQuery transparentCanvas twoCubes videoUploading "
           "videoUploadingWebCodecs volumeRenderingTexture3D wireframe worker workloadSimulator"
          componentsSeparatedByString:@" "];
}

// Injected before the page's own scripts: forwards errors and warnings to the host and counts animation frames.
static NSString *const kHook = @"(()=>{const post=(k,m)=>{try{webkit.messageHandlers.s.postMessage(k+' '+m)}catch(e){}};\n"
"const oe=console.error.bind(console);console.error=(...a)=>{post('error',a.map(String).join(' '));oe(...a)};\n"
"const ow=console.warn.bind(console);console.warn=(...a)=>{post('warn',a.map(String).join(' '));ow(...a)};\n"
"addEventListener('error',e=>post('error','onerror '+e.message));\n"
"addEventListener('unhandledrejection',e=>post('error','rejection '+(e.reason&&(e.reason.message||e.reason))));\n"
"if(self.GPUAdapter){const rd=GPUAdapter.prototype.requestDevice;GPUAdapter.prototype.requestDevice=async function(...a){\n"
" const d=await rd.apply(this,a);d.addEventListener('uncapturederror',e=>post('error','gpu '+e.error.message));\n"
" d.lost.then(i=>post('error','device lost '+i.reason+' '+i.message));return d;};}\n"
"let n=0;const f=()=>{n++;requestAnimationFrame(f)};requestAnimationFrame(f);self.__wgFrames=()=>n;})();";

// The hook's frame count, -1 when the hook did not run (the page failed to load).
static NSString *const kFrames = @"self.__wgFrames?__wgFrames():-1";

static NSString *jsonString(NSString *s) {
  NSData *d = [NSJSONSerialization dataWithJSONObject:@[ s ?: @"" ] options:0 error:nil];
  NSString *a = [[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding];
  return [a substringWithRange:NSMakeRange(1, a.length - 2)];
}

@interface Runner : NSObject <WKScriptMessageHandler>
@property NSWindow *window;
@property WKWebView *view;
@property NSArray<NSString *> *names;
@property NSString *outDir;
@property double seconds;
@property NSUInteger index;
@property NSMutableArray<NSString *> *errors;
@property NSUInteger warnings;
@end

@implementation Runner
- (void)userContentController:(WKUserContentController *)c didReceiveScriptMessage:(WKScriptMessage *)m {
  NSString *s = [m.body description];
  if ([s hasPrefix:@"warn "]) { self.warnings++; return; }
  if (s.length > 300) s = [s substringToIndex:300];
  if (self.errors.count < 4 && ![self.errors containsObject:s]) [self.errors addObject:s];
}

- (void)next {
  if (self.index >= self.names.count) exit(0);
  NSString *name = self.names[self.index++];
  self.errors = [NSMutableArray new];
  self.warnings = 0;
  WKWebViewConfiguration *cfg = [WKWebViewConfiguration new];
  [cfg.userContentController addScriptMessageHandler:self name:@"s"];
  [cfg.userContentController addUserScript:[[WKUserScript alloc] initWithSource:kHook
                                                                  injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                               forMainFrameOnly:YES]];
  self.view = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:cfg];
  self.window.contentView = self.view;
  NSString *url = [NSString stringWithFormat:@"https://webgpu.github.io/webgpu-samples/samples/%@/", name];
  [self.view loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:url]]];
  const double t0 = self.seconds / 3, t1 = self.seconds;
  __block double f0 = -1;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(t0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
    [self.view evaluateJavaScript:kFrames completionHandler:^(id r, NSError *e) {
      f0 = r ? [r doubleValue] : -1;
    }];
  });
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(t1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
    [self.view evaluateJavaScript:kFrames completionHandler:^(id r, NSError *e) {
      const double f1 = r ? [r doubleValue] : -1;
      const double fps = (f0 >= 0 && f1 >= f0) ? (f1 - f0) / (t1 - t0) : -1;
      // the window's real pixels (a WKWebView snapshot leaves WebGPU canvases empty)
      NSString *png = [self.outDir stringByAppendingPathComponent:[name stringByAppendingString:@".png"]];
      NSTask *t = [NSTask launchedTaskWithLaunchPath:@"/usr/sbin/screencapture"
          arguments:@[ @"-x", @"-o", [NSString stringWithFormat:@"-l%ld", (long)self.window.windowNumber], png ]];
      [t waitUntilExit];
      NSMutableArray *errs = [NSMutableArray new];
      for (NSString *x in self.errors) [errs addObject:jsonString(x)];
      printf("{\"sample\":\"%s\",\"fps\":%.1f,\"warnings\":%lu,\"errors\":[%s]}\n", name.UTF8String, fps,
             (unsigned long)self.warnings, [errs componentsJoinedByString:@","].UTF8String);
      fflush(stdout);
      [self next];
    }];
  });
}
@end

int main(int argc, char **argv) {
  @autoreleasepool {
    if (argc < 2) {
      fprintf(stderr, "usage: wgsamples <out dir> [seconds per sample] [sample ...]\n");
      return 2;
    }
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    Runner *r = [Runner new];
    r.outDir = @(argv[1]);
    [[NSFileManager defaultManager] createDirectoryAtPath:r.outDir withIntermediateDirectories:YES attributes:nil error:nil];
    r.seconds = argc > 2 ? atof(argv[2]) : 6;
    NSMutableArray *names = [NSMutableArray new];
    for (int i = 3; i < argc; i++) [names addObject:@(argv[i])];
    r.names = names.count ? names : allSamples();
    r.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(64, 64, 800, 600) styleMask:NSWindowStyleMaskBorderless
                                             backing:NSBackingStoreBuffered defer:NO];
    [r.window orderFrontRegardless];   // WebKit throttles animation frames in windows that are not on screen
    dispatch_async(dispatch_get_main_queue(), ^{ [r next]; });
    [NSApp run];
  }
  return 0;
}
