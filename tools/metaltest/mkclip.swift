// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vermilion contributors
//
// mkclip: writes a short HEVC test movie with Apple's encoder, so decode tests (vtdec_test, QuickTime, Safari) have
// a known input on a machine without sample media. Moving colour bars plus a little noise, so P / B frames carry
// real residuals.
// Usage: swift tools/metaltest/mkclip.swift <out.mov> [width height frames main|main10]   (default 1920 1080 120 main)
import AVFoundation
import CoreVideo
import VideoToolbox

let a = CommandLine.arguments
guard a.count >= 2 else { print("usage: mkclip <out.mov> [width height frames main|main10]"); exit(2) }
let url = URL(fileURLWithPath: a[1])
let w = a.count > 2 ? Int(a[2])! : 1920, h = a.count > 3 ? Int(a[3])! : 1080, n = a.count > 4 ? Int(a[4])! : 120
let ten = a.count > 5 && a[5] == "main10"
try? FileManager.default.removeItem(at: url)
let writer = try AVAssetWriter(outputURL: url, fileType: .mov)
var props: [String: Any] = [AVVideoAverageBitRateKey: 8_000_000]
if ten { props[AVVideoProfileLevelKey] = kVTProfileLevel_HEVC_Main10_AutoLevel as String }
let input = AVAssetWriterInput(mediaType: .video, outputSettings: [
    AVVideoCodecKey: AVVideoCodecType.hevc, AVVideoWidthKey: w, AVVideoHeightKey: h,
    AVVideoCompressionPropertiesKey: props])
let fmt = ten ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
let adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: input, sourcePixelBufferAttributes: [
    kCVPixelBufferPixelFormatTypeKey as String: fmt, kCVPixelBufferWidthKey as String: w,
    kCVPixelBufferHeightKey as String: h, kCVPixelBufferIOSurfacePropertiesKey as String: [:]])
writer.add(input)
writer.startWriting()
writer.startSession(atSourceTime: .zero)
var seed: UInt32 = 1
for f in 0..<n {
    while !input.isReadyForMoreMediaData { usleep(1000) }
    var pb: CVPixelBuffer?
    CVPixelBufferPoolCreatePixelBuffer(nil, adaptor.pixelBufferPool!, &pb)
    let p = pb!
    CVPixelBufferLockBaseAddress(p, [])
    for plane in 0..<2 {
        let base = CVPixelBufferGetBaseAddressOfPlane(p, plane)!
        let stride = CVPixelBufferGetBytesPerRowOfPlane(p, plane)
        let ph = CVPixelBufferGetHeightOfPlane(p, plane)
        let pw = CVPixelBufferGetWidthOfPlane(p, plane) * (plane == 0 ? 1 : 2)   // CbCr: two samples per pixel
        for y in 0..<ph {
            for x in 0..<pw {
                seed = seed &* 1103515245 &+ 12345
                let noise = Int((seed >> 16) & 7)
                let bar = ((x + f * 8) / max(pw / 8, 1)) % 8
                var v: Int
                if plane == 0 { v = 32 + bar * 24 + (y * 32 / ph) + noise }
                else { v = 128 + (x & 1 == 0 ? (bar - 4) * 20 : (3 - bar) * 20) + noise }
                v = min(max(v, 16), 240)
                if ten {
                    base.advanced(by: y * stride).assumingMemoryBound(to: UInt16.self)[x] = UInt16(v << 8)
                } else {
                    base.advanced(by: y * stride).assumingMemoryBound(to: UInt8.self)[x] = UInt8(v)
                }
            }
        }
    }
    CVPixelBufferUnlockBaseAddress(p, [])
    adaptor.append(p, withPresentationTime: CMTime(value: CMTimeValue(f), timescale: 30))
}
input.markAsFinished()
let done = DispatchSemaphore(value: 0)
writer.finishWriting { done.signal() }
done.wait()
print(writer.status == .completed ? "wrote \(url.path): \(w)x\(h) \(n) frames \(ten ? "Main10" : "Main")" : "failed: \(String(describing: writer.error))")
