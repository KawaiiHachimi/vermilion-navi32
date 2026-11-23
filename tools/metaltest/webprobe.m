// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vermilion contributors
//
// webprobe: what a WebKit page gets on this machine, through the same WebKit GPU process (and sandbox) Safari uses.
// Loads an inline page in an off-screen WKWebView and prints, as JSON: the WebGL 1 / WebGL 2 renderer and vendor
// (WEBGL_debug_renderer_info), a few limits, whether WebGPU gives an adapter (and its info), and how long a small WebGL
// draw loop takes (100 full-viewport draws of a 512x512 canvas, read back once), and what MSE / Media Capabilities say
// about AV1, VP9, HEVC and H.264 at the sizes YouTube serves (what a site picks its stream from). Draws only into its
// own canvas.
// Build: xcrun clang -arch x86_64 -fobjc-arc -O1 -o webprobe tools/metaltest/webprobe.m -framework Cocoa -framework WebKit
// Run:   ./webprobe [timeout seconds, default 20]
//        ./webprobe <timeout> <movie>   plays <movie> muted in a <video> element for 2 s and prints the frames WebKit
//        presented and dropped (requestVideoFrameCallback, getVideoPlaybackQuality): the media path Safari uses
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
" post('step media');try{const cs={'av1-2160p60':['video/mp4; codecs=\"av01.0.13M.10\"',3840,2160],\n"
"  'av1-1440p60':['video/mp4; codecs=\"av01.0.12M.10\"',2560,1440],'vp9-2160p60-10':['video/webm; codecs=\"vp09.02.51.10\"',3840,2160],\n"
"  'vp9-2160p60':['video/webm; codecs=\"vp09.00.51.08\"',3840,2160],'hevc-2160p60':['video/mp4; codecs=\"hvc1.2.4.L153.B0\"',3840,2160],\n"
"  'h264-1080p60':['video/mp4; codecs=\"avc1.64002A\"',1920,1080]};r.media={};\n"
"  for(const[k,[t,w,h]] of Object.entries(cs)){const e={mse:self.MediaSource?MediaSource.isTypeSupported(t):null};\n"
"   try{const i=await navigator.mediaCapabilities.decodingInfo({type:'media-source',video:{contentType:t,width:w,height:h,\n"
"    framerate:60,bitrate:20000000}});e.supported=i.supported;e.smooth=i.smooth;e.powerEfficient=i.powerEfficient;}\n"
"   catch(x){e.error=''+x;}r.media[k]=e;}}catch(e){r.media='error '+e;}\n"
" post(JSON.stringify(r));}\n"
"probe();</script></body></html>";

// <video> mode: %@ is the movie's file name, the page sits next to it (file access is limited to that folder)
static NSString *const kVideoPage = @"<!doctype html><html><body><video id=v muted playsinline src=\"%@\" "
"width=480 height=270></video><script>\n"
"const post=x=>window.webkit.messageHandlers.probe.postMessage(x);const v=document.getElementById('v');let n=0;\n"
"const cb=()=>{n++;v.requestVideoFrameCallback(cb);};v.requestVideoFrameCallback(cb);\n"
"v.onerror=()=>post(JSON.stringify({error:'media error '+(v.error&&v.error.code)}));\n"
"v.onplaying=()=>{post('step playing');setTimeout(()=>{const q=v.getVideoPlaybackQuality();\n"
" post(JSON.stringify({w:v.videoWidth,h:v.videoHeight,t:+v.currentTime.toFixed(2),callbackFrames:n,\n"
"  total:q.totalVideoFrames,dropped:q.droppedVideoFrames}));},2000);};\n"
"v.play().catch(e=>post(JSON.stringify({error:'play '+e})));</script></body></html>";

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
    NSWindow *w = [[NSWindow alloc] initWithContentRect:NSMakeRect(argc > 2 ? 64 : -4000, argc > 2 ? 64 : -4000, 512, 512)
                                              styleMask:NSWindowStyleMaskBorderless
                                                backing:NSBackingStoreBuffered defer:NO];
    WKWebView *v = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 512, 512) configuration:cfg];
    v.navigationDelegate = p;
    w.contentView = v;
    if (argc > 2) [w orderFrontRegardless]; else [w orderBack:nil];   // <video> presents frames only on screen
    if (argc > 2) {
      NSURL *movie = [NSURL fileURLWithPath:@(argv[2])].URLByStandardizingPath;
      NSURL *dir = movie.URLByDeletingLastPathComponent;
      NSURL *page = [dir URLByAppendingPathComponent:@".webprobe-video.html"];
      NSString *html = [NSString stringWithFormat:kVideoPage, movie.lastPathComponent];
      if (![html writeToURL:page atomically:YES encoding:NSUTF8StringEncoding error:nil]) {
        printf("{\"error\":\"cannot write %s\"}\n", page.path.UTF8String);
        return 1;
      }
      [v loadFileURL:page allowingReadAccessToURL:dir];
    } else {
      [v loadHTMLString:kPage baseURL:[NSURL URLWithString:@"https://localhost/"]];
    }
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
      printf("{\"error\":\"timeout after %.0f s\"}\n", timeout);
      exit(2);
    });
    [NSApp run];
  }
  return 0;
}
