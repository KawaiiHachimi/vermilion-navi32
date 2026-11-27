# RX 7800 XT graphics verification, November 26–27, 2026

The RX 7800 XT (16 GiB, PCI 1002:747e) ran macOS 26.7.3 build 25G342 and Edge 158.0.4370.33. The original desktop used 1920×1080 HiDPI at 165 Hz. The Edge comparisons used the installed driver. Metal API tests loaded each candidate in a separate process on the same NMXAccel service.

## Geekbench icon

Screen Sharing showed two Edge windows on the same desktop. In the affected window the glasses had a solid rectangular outer frame and the four bars had square corners. A clean accelerated profile reproduced both defects in Chromium's own page capture. The asset was the CSS background image `/img/parkdale/app-icon.svg` on the Geekbench homepage.

| Edge configuration | Observed result |
|---|---|
| Default, GraphiteDawnMetal, GPU raster and composition | Against the same-machine software capture, 11,995 of 65,536 pixels in the 256×256 icon differed by more than 16 in at least one RGB channel. |
| Only `--disable-gpu-rasterization` | The homepage icon matched the software capture byte for byte. GPU compositing, WebGL and WebGPU remained enabled. |
| Forced sRGB output profile, GPU raster enabled | The defect persisted in a fresh process using the saved original SVG. |
| Graphite disabled | Edge also disabled GPU rasterization in the tested configuration. This comparison cannot isolate Graphite from software fallback. |

Open [the offline test](../../tools/validation/gradient-coverage.html) in the browser under test and check the backend separately at `edge://gpu`. It draws a self-contained SVG path twice, varying the gradient stops. The legacy sRGB control preserved coverage and opacity. Display P3 stops on the normal accelerated canvas painted an uncovered point [251,229,86] over a background of [0,59,123]. The center was [253,184,60] against the expected translucent [102,110,98]. Three repetitions agreed. The readback-optimized canvas, software Edge and accelerated Apple M5 Pro passed.

The failing Graphite trace includes a four-sample BGRA8/D24S8 regular cover pipeline labelled “w/ msaa load”. Winding and cover occur in the same group of pipeline-use events. The label alone cannot establish a pass split. In isolation the extracted paint shader produced the expected opacity at 1.0 and 0.4 across eleven color-space selectors. A separate Metal test on the RX 7800 XT checked all 1,024 pixels after sampling a texture into a 4× attachment, applying a stencil-tested draw and resolving. Both separate-source and shared resolve-target cases passed with D24S8 and D32FS8. An attempted native Metal capture restarted the disposable Edge GPU helper and produced no usable capture. The command or driver state responsible for the browser defect is still open.

A temporarily installed diagnostic plugin logged the failing frame in Edge's GPU process while rendering the saved website SVG. The log contains 4× BGRA and depth/stencil pipeline binds, winding operations, and stencil comparisons. No incompatible pipeline rejection appeared. Reapplying depth/stencil state after compatible pipeline switches, using a plain multisampled pass for shaders without framebuffer fetch, and combining those two changes all produced the same number of mismatching pixels as the diagnostic control. These experiments were discarded. After each swap the original plugin was restored and verified by inode and SHA-256. Code signature verification succeeded, and WindowServer kept its PID.

## Changes exercised on hardware

| Change | RX 7800 XT evidence |
|---|---|
| `cc88524` guards buffer/image transfers in both directions | Six entry points refused 2×, 4× and 8× inputs before Vulkan initialization. The positive test checked all 91 BGRA pixels and row padding after a pitched 1× transfer and after a legal 4× resolve. Adapted from [NullMoth 71aa2a2](https://github.com/nullmoth/nvidia-macos-driver/commit/71aa2a255ea724c52b97acf6c14f5749ea8c9a26). |
| `0dfd713` limits timestamp writes on hot shader cache entries | Installed/candidate/candidate/installed measurements for 5,000 cached Metal function specializations were 84.4, 54.8, 56.2 and 85.5 ms. All four runs verified 4,096 GPU output words. An earlier same-process test observed the timestamp advance after the 60-second interval. This measures CPU-side shader setup. Adapted from [XTX 678faf6](https://github.com/ownback/vermilion-xtx/commit/678faf6f266bb63c76dc975b5dcb0da5b1a8e334). |
| `1d09714` completes initialization of odd-length buffers and orders managed writes after clearing | Checked private and managed buffers from 1 byte to 1 MiB + 3, including the first CPU edit and four concurrent queues. The diagnostic build passed all four combinations of submission prologue on/off and synchronous managed upload on/off. Three fresh processes loading the combined release build each passed 37 buffer checks and both multisample suites. |

The installed driver also completed the offscreen render, submit and compute benchmarks with zero reported GPU errors. The display transaction failure, surface GPU timeout and page-flip timeout counters were all zero after the combined release tests. The combined release build also completed 268,981 offscreen render frames in 20 seconds, 257,361 compute frames in 20 seconds and 129,936 submissions in 10 seconds. All three workloads reported zero GPU errors. The host regression runner passed all 17 suites.

## Reference review as of November 27

| Project / head | Decision for Navi 32 |
|---|---|
| [Vermilion 9a91901](https://github.com/leolivm/vermilion/commit/9a91901bf822c39ac71698f04180bcc793ae559f) | The existing branch already carries the Chromium translator change and ports the b126/b127 recovery logic. |
| [XTX 45372b9](https://github.com/ownback/vermilion-xtx/commit/45372b9a5c0b684e7b933255cc5adabb827701da) | Since the first review, #53 enables direct scan-out by default, #54 switches to hardware device info across kext and RADV ABI 1.13, and #55 changes daemon defaults. Importing #54 requires coordinated kernel and Mesa changes that preserve this card's GC 11.0.3 layout and measured harvest. Direct scan-out changes screenshot freshness; keep that experiment separate from the current visual comparison. |
| [XTX IOSurface readback](https://github.com/ownback/vermilion-xtx/commit/27a6ac7cd60bde6470a77daf4551d68b510c3236) | Candidate `e146ecc` is on `navi32-readback-guard`. It preserved pixels and row padding across 32 frames per build, and reports a destination-copy failure. The 1.44 MB packed copy took about 206 µs versus 61 µs with memcpy. Padded full command/wait times also increased. Held out of the default branch. |
| [NullMoth 45040e9](https://github.com/nullmoth/nvidia-macos-driver/commit/45040e97f705e7de684f8dac45ff71ade72a31ba) | Used its single-sample transfer requirement. The NVIDIA-specific recovery and staging work requires a separate applicability test on RADV. |
| [RedNavi 764980e](https://github.com/Helemen7/RedNavi/commit/764980eddca2) | Its four-boot Metal validation is a reference for repeatability. Its documented RX 7600 results use a different chip and accelerator attachment sequence. |
| [RX4Darwin 9d9eecb](https://github.com/RX4Darwin/AMDRadeonRDNA4.kext/commit/9d9eecb19320) | Its 0.4.0 per-sample shader work is relevant when a failing case involves sample inputs. The successful 4× resolve on this card provides a control. |
| [Cobalt64 d1f7bad](https://github.com/Almosst-DEV/Cobalt64/commit/d1f7bad8408a), [nullarc 3497d87](https://github.com/luuumi-dev/nullarc/commit/3497d87b229e), [RadeArc fa1eb34](https://github.com/Kreytus/RadeArc/commit/fa1eb34a1e58) | The respective tips document IOAccel layout tooling, DCN mapping/MPS reproduction, and SCDC readback. None supplies a measured fix for this SVG. |

Mesa RADV also landed a [depth/stencil HTILE copy fix](https://gitlab.freedesktop.org/mesa/mesa/-/commit/839ef9d5e1843681e6fde4347fa604a66a1e397a) on its SDMA transfer path. This Metal plugin submits to a graphics+compute queue, and the Darwin WSI patch disables private SDMA blits. The upstream change therefore does not target the observed browser draw. A separate [ACO LDS spill ordering fix](https://gitlab.freedesktop.org/mesa/mesa/-/commit/7918498b88e647f66cf1253c78bd3db6a7652e49) would require evidence of spills in the affected shader before attributing the icon to it.
