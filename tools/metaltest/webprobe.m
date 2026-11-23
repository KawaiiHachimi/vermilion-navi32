// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vermilion contributors
//
// webprobe: what a WebKit page gets on this machine, through the same WebKit GPU process (and sandbox) Safari uses.
// Loads an inline page in an off-screen WKWebView and prints, as JSON: the WebGL 1 / WebGL 2 renderer and vendor
// (WEBGL_debug_renderer_info), a few limits, whether WebGPU gives an adapter (and its info), and how long a small WebGL
// draw loop takes (100 full-viewport draws of a 512x512 canvas, read back once). Draws only into its own canvas.
// Build: xcrun clang -arch x86_64 -fobjc-arc -O1 -o webprobe tools/metaltest/webprobe.m -framework Cocoa -framework WebKit
// Run:   ./webprobe [timeout seconds, default 20]
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

static NSString *const kPage = @"<!doctype html><html><body><canvas id=c width=512 height=512></canvas><script>\n"
"const post=x=>window.webkit.messageHandlers.probe.postMessage(x);\n"
"async function probe(){const r={};post('step start');\n"
" for(const k of ['webgl','webgl2']){try{const cv=document.createElement('canvas');const g=cv.getContext(k);\n"
"  if(!g){r[k]=null;continue;}const d=g.getExtension('WEBGL_debug_renderer_info');\n"
"  r[k]={lost:g.isContextLost(),vendor:d?g.getParameter(d.UNMASKED_VENDOR_WEBGL):g.getParameter(g.VENDOR),\n"
"   renderer:d?g.getParameter(d.UNMASKED_RENDERER_WEBGL):g.getParameter(g.RENDERER),\n"
"   version:g.getParameter(g.VERSION),maxTexture:g.getParameter(g.MAX_TEXTURE_SIZE),\n"
"   extensions:(g.getSupportedExtensions()||[]).length};}catch(e){r[k]='error '+e;}}\n"
" post('step webgpu');try{if(!navigator.gpu){r.webgpu='navigator.gpu missing';}else{const a=await navigator.gpu.requestAdapter();\n"
"  if(!a){r.webgpu='no adapter';}else{const i=a.info||{};r.webgpu={vendor:i.vendor,architecture:i.architecture,\n"
"   device:i.device,description:i.description,features:[...a.features].length,\n"
"   maxBufferSize:a.limits.maxBufferSize};}}}catch(e){r.webgpu='error '+e;}\n"
" post('step draw');try{const g=document.getElementById('c').getContext('webgl2');\n"
"  const vs='#version 300 es\\nin vec2 p;void main(){gl_Position=vec4(p,0.,1.);}';\n"
"  const fs='#version 300 es\\nprecision highp float;out vec4 o;uniform float t;void main(){float a=0.;"
"   for(int i=0;i<64;i++)a+=sin(gl_FragCoord.x*0.01+float(i)+t)*cos(gl_FragCoord.y*0.01);o=vec4(a*0.01,0.5,0.5,1.);}';\n"
"  const sh=(ty,s)=>{const x=g.createShader(ty);g.shaderSource(x,s);g.compileShader(x);return x;};\n"
"  const pr=g.createProgram();g.attachShader(pr,sh(g.VERTEX_SHADER,vs));g.attachShader(pr,sh(g.FRAGMENT_SHADER,fs));\n"
"  g.linkProgram(pr);g.useProgram(pr);const b=g.createBuffer();g.bindBuffer(g.ARRAY_BUFFER,b);\n"
"  g.bufferData(g.ARRAY_BUFFER,new Float32Array([-1,-1,3,-1,-1,3]),g.STATIC_DRAW);g.enableVertexAttribArray(0);\n"
"  g.vertexAttribPointer(0,2,g.FLOAT,false,0,0);const u=g.getUniformLocation(pr,'t');const px=new Uint8Array(4);\n"
"  g.drawArrays(g.TRIANGLES,0,3);g.readPixels(0,0,1,1,g.RGBA,g.UNSIGNED_BYTE,px);\n"
"  const t0=performance.now();for(let i=0;i<100;i++){g.uniform1f(u,i);g.drawArrays(g.TRIANGLES,0,3);}\n"
"  g.readPixels(0,0,1,1,g.RGBA,g.UNSIGNED_BYTE,px);r.draw100ms=+(performance.now()-t0).toFixed(2);r.pixel=[...px];}\n"
" catch(e){r.draw='error '+e;}\n"
" post(JSON.stringify(r));}\n"
"probe();</script></body></html>";

@interface Probe : NSObject <WKScriptMessageHandler, WKNavigationDelegate>
@end
@implementation Probe
- (void)userContentController:(WKUserContentController *)c didReceiveScriptMessage:(WKScriptMessage *)m {
  NSString *s = [m.body description];
  if ([s hasPrefix:@"step "]) {   // progress, so a timeout says where it stopped
    fprintf(stderr, "webprobe: %s\n", s.UTF8String);
    return;
  }
  printf("%s\n", s.UTF8String);
  exit(0);
}
- (void)webView:(WKWebView *)v didFinishNavigation:(WKNavigation *)n {
  fprintf(stderr, "webprobe: page loaded\n");
}
- (void)webView:(WKWebView *)v didFailProvisionalNavigation:(WKNavigation *)n withError:(NSError *)e {
  fprintf(stderr, "webprobe: load failed: %s\n", e.description.UTF8String);
}
- (void)webViewWebContentProcessDidTerminate:(WKWebView *)v {
  fprintf(stderr, "webprobe: web content process terminated\n");
}
@end

int main(int argc, char **argv) {
  @autoreleasepool {
    const double timeout = argc > 1 ? atof(argv[1]) : 20;
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    Probe *p = [Probe new];
    WKWebViewConfiguration *cfg = [WKWebViewConfiguration new];
    [cfg.userContentController addScriptMessageHandler:p name:@"probe"];
    NSWindow *w = [[NSWindow alloc] initWithContentRect:NSMakeRect(-4000, -4000, 512, 512)
                                              styleMask:NSWindowStyleMaskBorderless
                                                backing:NSBackingStoreBuffered defer:NO];
    WKWebView *v = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 512, 512) configuration:cfg];
    v.navigationDelegate = p;
    w.contentView = v;
    [w orderBack:nil];
    [v loadHTMLString:kPage baseURL:[NSURL URLWithString:@"https://localhost/"]];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
      printf("{\"error\":\"timeout after %.0f s\"}\n", timeout);
      exit(2);
    });
    [NSApp run];
  }
  return 0;
}
