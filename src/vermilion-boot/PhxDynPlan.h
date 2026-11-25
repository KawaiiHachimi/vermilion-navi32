// SPDX-License-Identifier: MIT
// PhxDynPlan.h: freestanding EDID/CTA-861/DisplayID parser + DCN 3.2.0 dynamic pipe-0 plan synthesizer.
//
// Kernel-free (stdint + string + snprintf) so the same header runs in VermilionBoot (phx_pipe0.cpp) and in the host
// regression test (tests/kext/dyn_plan_test.cpp, checked against all 6 live Linux captures of the RX 7800 XT).
//
// Goal: remove the per-monitor static .inc bottleneck so ANY connected DisplayPort monitor can expose every progressive
// mode from its EDID that fits the currently trained DP link, while:
//   1. Preferring verified Linux-captured static plans whenever a mode matches one exactly;
//   2. Synthesizing a complete phxp0::Plan (strictly within phxp0::kAllowP0) for every other EDID mode;
//   3. Running every switch through PhxPipe0's existing snapshot -> shell -> 1 s verify -> automatic revert safety net.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#ifndef KERNEL
#include <stdio.h>
#endif
#include "PhxPipe0.h"
#include "PhxP0Modes.h"

namespace phxdyn {

constexpr unsigned kMaxModes = phxp0pub::kMaxModes;   // 32
constexpr unsigned kMaxSteps = 48;
// Unambiguous catalog-index key passed in phxp0pub::Mode.key -> phx_pipe0_setmode.target_hz (avoids collisions when two
// resolutions share a height or refresh rate, e.g. 2560x1080@60 vs 1920x1080@60).
constexpr uint32_t kKeyIdxBase = 0x40000000u;

struct Timing {
    uint32_t pclkKhz;
    uint16_t hActive, hFp, hSync, hBp, hTotal;
    uint16_t vActive, vFp, vSync, vBp, vTotal;
    uint8_t  hPos, vPos;       // 1 = positive sync polarity
    uint8_t  preferred;        // 1 = block-0 first DTD
    uint8_t  source;           // 1 DTD, 2 DisplayID, 3 CTA VIC, 4 DMT, 5 CVT-RB, 6 live OTG
    uint16_t refreshHz;        // rounded integer Hz
    uint32_t milliHz;          // exact mHz = pclkKhz * 1e6 / (hTotal * vTotal)
};

enum : uint8_t {
    kSrcDtd = 1, kSrcDid = 2, kSrcVic = 3, kSrcDmt = 4, kSrcCvt = 5, kSrcLive = 6
};

struct LinkCap {
    uint8_t  lanes;            // 1, 2, or 4 (0 = unknown)
    uint8_t  rateCode;         // DPCD 0x0100: 0x06 RBR, 0x0A HBR, 0x14 HBR2, 0x1E HBR3 (0 = unknown)
    uint8_t  bpc;              // 8 or 10 (default 8 if unknown)
    uint64_t barLen;           // BAR0 aperture bytes (0 = unchecked)
    uint32_t fallbackMaxPclk;  // used when lanes/rateCode unknown (kHz)
};

// A synthesized plan owns its step array, DAL clock floors, and label string so phxp0::Plan pointers remain valid.
struct DynPlan {
    phx_s2_step steps[kMaxSteps];
    uint32_t    dalMin[2];
    char        name[24];
    phxp0::Plan plan;
    Timing      timing;
    bool        isStatic;
    const phxp0::Plan *staticPlan;
};

struct Catalog {
    unsigned n;
    DynPlan  entries[kMaxModes];
    const phxp0::Plan *ptrs[kMaxModes];   // parallel pointer array for phxp0::identify / macSet
    uint32_t maxLinkKbps;                 // effective DP link data budget (kbps) used for filtering
    uint32_t parsedTotal;                 // total EDID modes before link/geometry filtering
    uint32_t filteredOut;                 // modes skipped (bandwidth, size, or capacity)
};

// ---- Helpers --------------------------------------------------------------------------------------------------------

inline uint32_t milliHzOf(uint32_t pclkKhz, uint32_t hTotal, uint32_t vTotal) {
    const uint64_t tot = static_cast<uint64_t>(hTotal) * vTotal;
    if (!tot || !pclkKhz) return 0;
    return static_cast<uint32_t>((static_cast<uint64_t>(pclkKhz) * 1000000ull + tot / 2) / tot);
}

inline uint16_t refreshOf(uint32_t milliHz) {
    return static_cast<uint16_t>((milliHz + 500u) / 1000u);
}

inline uint32_t pitchPixels(uint32_t width) {
    return (width + 255u) & ~255u;   // matches phxhead::modeRowBytes & Linux HUBPREQ0 pitch alignment
}

inline bool blockChecksumOk(const uint8_t *b) {
    uint8_t s = 0;
    for (int i = 0; i < 128; i++) s = static_cast<uint8_t>(s + b[i]);
    return s == 0;
}

// Effective DP 8b/10b link data rate in kbps (2% framing/FEC margin), 0 if unknown.
inline uint32_t dpLinkDataKbps(uint8_t lanes, uint8_t rateCode) {
    if (lanes != 1 && lanes != 2 && lanes != 4) return 0;
    uint32_t symKbps = 0;
    switch (rateCode) {
        case 0x06: symKbps = 1620000u; break;   // RBR  1.62 Gbps
        case 0x0A: symKbps = 2700000u; break;   // HBR  2.70 Gbps
        case 0x14: symKbps = 5400000u; break;   // HBR2 5.40 Gbps
        case 0x1E: symKbps = 8100000u; break;   // HBR3 8.10 Gbps
        default: return 0;
    }
    // 8b/10b encoding gives 80% payload; keep 2% margin for TU/MSA/audio overhead.
    const uint64_t payload = static_cast<uint64_t>(lanes) * symKbps * 8ull / 10ull;
    return static_cast<uint32_t>(payload * 98ull / 100ull);
}

inline uint32_t modeRequiredKbps(uint32_t pclkKhz, uint8_t bpc) {
    const uint32_t bpp = (bpc == 10 ? 30u : 24u);
    return static_cast<uint32_t>(static_cast<uint64_t>(pclkKhz) * bpp);
}

inline bool timingValid(const Timing &t) {
    if (t.hActive < 640 || t.hActive > 4096 || t.vActive < 480 || t.vActive > 2304) return false;
    if (t.hTotal <= t.hActive || t.vTotal <= t.vActive) return false;
    if (!t.hSync || !t.vSync || !t.pclkKhz || t.pclkKhz < 25000u || t.pclkKhz > 1200000u) return false;
    const uint32_t vBlank = t.vTotal - t.vActive;
    if (vBlank < 16 || vBlank > 1023) return false;   // OTG_VSTARTUP_PARAM is 10-bit; prefetch needs >= 16 lines
    if (t.hTotal - t.hActive < static_cast<uint32_t>(t.hFp) + t.hSync) return false;
    if (vBlank < static_cast<uint32_t>(t.vFp) + t.vSync) return false;
    if (t.refreshHz < 24 || t.refreshHz > 360) return false;
    return true;
}

// Choose the highest bpc (10 or 8) that fits `cap`'s DP link bandwidth, or 0 if even 8 bpc exceeds the link.
inline uint8_t selectBpc(const Timing &t, const LinkCap &cap) {
    if (!timingValid(t)) return 0;
    const uint8_t prefBpc = cap.bpc == 10 ? 10 : 8;
    const uint32_t linkKbps = dpLinkDataKbps(cap.lanes, cap.rateCode);
    uint8_t chosen = prefBpc;
    if (linkKbps) {
        if (modeRequiredKbps(t.pclkKhz, chosen) > linkKbps) {
            if (chosen == 10 && modeRequiredKbps(t.pclkKhz, 8) <= linkKbps) chosen = 8;
            else return 0;
        }
    } else if (cap.fallbackMaxPclk && t.pclkKhz > cap.fallbackMaxPclk) {
        return 0;
    }
    if (cap.barLen) {
        // Console surface A starts at offset 0 and must stay below flip buffer B (16 MiB) for single-buffer safety,
        // or within BAR0 when larger (PhxBringup's scanout slots start at 24 MiB / 256 MiB).
        const uint64_t bytes = static_cast<uint64_t>(pitchPixels(t.hActive)) * 4ull * t.vActive;
        if (bytes > cap.barLen) return 0;
    }
    return chosen;
}

inline bool fitsLink(const Timing &t, const LinkCap &cap) {
    return selectBpc(t, cap) != 0;
}

// Inspect a static plan's output bpc (10 if FMT0_FMT_BIT_DEPTH_CONTROL == 0xd100 or DP_PIXEL_FORMAT == 10 bpc, else 8).
inline uint8_t staticPlanBpc(const phxp0::Plan &p) {
    for (unsigned i = 0; i < p.n; i++) {
        if (p.s[i].reg == 0x04D01) return (p.s[i].val == 0x0000d100u) ? 10 : 8;
    }
    return 8;
}

// ---- VESA CVT Reduced Blanking v1 (VESA CVT 1.2) --------------------------------------------------------------------

inline bool cvtRb(unsigned ha, unsigned va, unsigned hz, Timing *out) {
    if (ha < 640 || ha > 4096 || va < 480 || va > 2304 || hz < 24 || hz > 240) return false;
    const uint64_t hActive = ha & ~7u;
    const uint64_t hBlank = 160;
    const uint64_t hTotal = hActive + hBlank;
    const uint64_t hFp = 48, hSync = 32, hBp = 80;
    // Period estimate (us * 1000 -> ns): (1e9 / hz - 460000) / va
    const uint64_t fieldNs = 1000000000ull / hz;
    if (fieldNs <= 460000ull) return false;
    const uint64_t hPeriodNs = (fieldNs - 460000ull) / va;
    if (!hPeriodNs) return false;
    uint64_t vbiLines = (460000ull + hPeriodNs - 1) / hPeriodNs;
    const uint64_t vFp = 3;
    const uint64_t vSync = (ha * 9 == va * 16) ? 5 : (ha * 10 == va * 16) ? 6 : (ha * 3 == va * 4) ? 4 : 7;
    if (vbiLines < vFp + vSync + 6) vbiLines = vFp + vSync + 6;
    if (vbiLines < 16) vbiLines = 16;
    const uint64_t vTotal = va + vbiLines;
    const uint64_t vBp = vbiLines - vFp - vSync;
    // Pixel clock rounded down to 250 kHz step as CVT-RB specifies.
    const uint64_t pclkKhz = ((hTotal * vTotal * hz) / 1000ull / 250ull) * 250ull;
    if (!pclkKhz || pclkKhz > 1200000ull) return false;
    memset(out, 0, sizeof(*out));
    out->pclkKhz = static_cast<uint32_t>(pclkKhz);
    out->hActive = static_cast<uint16_t>(hActive);
    out->hFp = static_cast<uint16_t>(hFp); out->hSync = static_cast<uint16_t>(hSync);
    out->hBp = static_cast<uint16_t>(hBp); out->hTotal = static_cast<uint16_t>(hTotal);
    out->vActive = static_cast<uint16_t>(va);
    out->vFp = static_cast<uint16_t>(vFp); out->vSync = static_cast<uint16_t>(vSync);
    out->vBp = static_cast<uint16_t>(vBp); out->vTotal = static_cast<uint16_t>(vTotal);
    out->hPos = 1; out->vPos = 0;   // CVT-RB standard polarity: H+ V-
    out->source = kSrcCvt;
    out->milliHz = milliHzOf(out->pclkKhz, out->hTotal, out->vTotal);
    out->refreshHz = refreshOf(out->milliHz);
    return timingValid(*out);
}

// ---- EDID / CTA-861 / DisplayID 1.x & 2.0 parser --------------------------------------------------------------------

namespace detail {

struct StdRow {
    uint8_t  vic;
    uint32_t pclkKhz;
    uint16_t ha, hfp, hs, hbp, va, vfp, vs, vbp;
    uint8_t  hpos, vpos;
};

static const StdRow kCtaVics[] = {
    {1,   25175,  640,   16,  96,  48,  480, 10,  2, 33, 0, 0},
    {2,   27000,  720,   16,  62,  60,  480,  9,  6, 30, 0, 0},
    {3,   27000,  720,   16,  62,  60,  480,  9,  6, 30, 0, 0},
    {4,   74250, 1280,  110,  40, 220,  720,  5,  5, 20, 1, 1},
    {16, 148500, 1920,   88,  44, 148, 1080,  4,  5, 36, 1, 1},
    {17,  27000,  720,   12,  64,  68,  576,  5,  5, 39, 0, 0},
    {18,  27000,  720,   12,  64,  68,  576,  5,  5, 39, 0, 0},
    {19,  74250, 1280,  440,  40, 220,  720,  5,  5, 20, 1, 1},
    {31, 148500, 1920,  528,  44, 148, 1080,  4,  5, 36, 1, 1},
    {32,  74250, 1920,  638,  44, 148, 1080,  4,  5, 36, 1, 1},
    {33,  74250, 1920,  528,  44, 148, 1080,  4,  5, 36, 1, 1},
    {34,  74250, 1920,   88,  44, 148, 1080,  4,  5, 36, 1, 1},
    {41, 148500, 1280,  440,  40, 220,  720,  5,  5, 20, 1, 1},
    {47, 148500, 1280,  110,  40, 220,  720,  5,  5, 20, 1, 1},
    {63, 297000, 1920,   88,  44, 148, 1080,  4,  5, 36, 1, 1},
    {64, 297000, 1920,  528,  44, 148, 1080,  4,  5, 36, 1, 1},
    {93, 297000, 3840, 1276,  88, 296, 2160,  8, 10, 72, 1, 1},
    {94, 297000, 3840, 1056,  88, 296, 2160,  8, 10, 72, 1, 1},
    {95, 297000, 3840,  176,  88, 296, 2160,  8, 10, 72, 1, 1},
    {96, 594000, 3840, 1056,  88, 296, 2160,  8, 10, 72, 1, 1},
    {97, 594000, 3840,  176,  88, 296, 2160,  8, 10, 72, 1, 1},
    {117, 1188000, 3840, 1056, 88, 296, 2160, 8, 10, 72, 1, 1},
    {118, 1188000, 3840,  176, 88, 296, 2160, 8, 10, 72, 1, 1},
};

// Established timings (bytes 35-36 of EDID block 0): bit masks -> exact VESA DMT progressive timings.
struct EstRow { uint8_t byteIdx, bitMask; StdRow r; };
static const EstRow kEst[] = {
    {35, 0x20, {0,  25175,  640, 16,  96,  48,  480, 10, 2, 33, 0, 0}},   // 640x480@60
    {35, 0x04, {0,  31500,  640, 16,  64, 120,  480,  1, 3, 16, 0, 0}},   // 640x480@75
    {35, 0x01, {0,  40000,  800, 40, 128,  88,  600,  1, 4, 23, 1, 1}},   // 800x600@60
    {36, 0x40, {0,  49500,  800, 16,  80, 160,  600,  1, 3, 21, 1, 1}},   // 800x600@75
    {36, 0x08, {0,  65000, 1024, 24, 136, 160,  768,  3, 6, 29, 0, 0}},   // 1024x768@60
    {36, 0x02, {0,  78750, 1024, 16,  96, 176,  768,  1, 3, 28, 1, 1}},   // 1024x768@75
    {36, 0x01, {0, 135000, 1280, 48, 112, 248, 1024,  1, 3, 38, 1, 1}},   // 1280x1024@75
};

inline void fromRow(const StdRow &r, uint8_t src, Timing *t) {
    memset(t, 0, sizeof(*t));
    t->pclkKhz = r.pclkKhz;
    t->hActive = r.ha; t->hFp = r.hfp; t->hSync = r.hs; t->hBp = r.hbp;
    t->hTotal = static_cast<uint16_t>(r.ha + r.hfp + r.hs + r.hbp);
    t->vActive = r.va; t->vFp = r.vfp; t->vSync = r.vs; t->vBp = r.vbp;
    t->vTotal = static_cast<uint16_t>(r.va + r.vfp + r.vs + r.vbp);
    t->hPos = r.hpos; t->vPos = r.vpos; t->source = src;
    t->milliHz = milliHzOf(t->pclkKhz, t->hTotal, t->vTotal);
    t->refreshHz = refreshOf(t->milliHz);
}

inline bool parseDtd(const uint8_t *d, bool pref, Timing *t) {
    const uint32_t pclk = (static_cast<uint32_t>(d[0]) | (static_cast<uint32_t>(d[1]) << 8)) * 10u;
    if (!pclk || (d[17] & 0x80u)) return false;   // monitor descriptor or interlaced
    const uint16_t ha = static_cast<uint16_t>(d[2] | ((d[4] & 0xF0u) << 4));
    const uint16_t hb = static_cast<uint16_t>(d[3] | ((d[4] & 0x0Fu) << 8));
    const uint16_t va = static_cast<uint16_t>(d[5] | ((d[7] & 0xF0u) << 4));
    const uint16_t vb = static_cast<uint16_t>(d[6] | ((d[7] & 0x0Fu) << 8));
    const uint16_t hfp = static_cast<uint16_t>(d[8] | ((d[11] & 0xC0u) << 2));
    const uint16_t hs  = static_cast<uint16_t>(d[9] | ((d[11] & 0x30u) << 4));
    const uint16_t vfp = static_cast<uint16_t>((d[10] >> 4) | ((d[11] & 0x0Cu) << 2));
    const uint16_t vs  = static_cast<uint16_t>((d[10] & 0x0Fu) | ((d[11] & 0x03u) << 4));
    if (hb < hfp + hs || vb < vfp + vs) return false;
    memset(t, 0, sizeof(*t));
    t->pclkKhz = pclk;
    t->hActive = ha; t->hFp = hfp; t->hSync = hs; t->hBp = static_cast<uint16_t>(hb - hfp - hs);
    t->hTotal = static_cast<uint16_t>(ha + hb);
    t->vActive = va; t->vFp = vfp; t->vSync = vs; t->vBp = static_cast<uint16_t>(vb - vfp - vs);
    t->vTotal = static_cast<uint16_t>(va + vb);
    t->hPos = (d[17] >> 1) & 1u;
    t->vPos = (d[17] >> 2) & 1u;
    t->preferred = pref ? 1 : 0;
    t->source = kSrcDtd;
    t->milliHz = milliHzOf(t->pclkKhz, t->hTotal, t->vTotal);
    t->refreshHz = refreshOf(t->milliHz);
    return timingValid(*t);
}

// DisplayID Type I (tag 0x03, 10 kHz clock) or Type VII (tag 0x22, 1 kHz clock), 20 bytes per descriptor.
inline bool parseDisplayId20(const uint8_t *p, bool type7, Timing *t) {
    const uint32_t rawClk = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16);
    const uint32_t pclk = type7 ? (rawClk + 1u) : (rawClk + 1u) * 10u;
    if (p[3] & 0x10u) return false;   // interlaced
    const uint32_t ha  = (static_cast<uint32_t>(p[4])  | (static_cast<uint32_t>(p[5])  << 8)) + 1u;
    const uint32_t hb  = (static_cast<uint32_t>(p[6])  | (static_cast<uint32_t>(p[7])  << 8)) + 1u;
    const uint32_t hfp = (static_cast<uint32_t>(p[8])  | ((static_cast<uint32_t>(p[9]) & 0x7Fu) << 8)) + 1u;
    const uint32_t hs  = (static_cast<uint32_t>(p[10]) | (static_cast<uint32_t>(p[11]) << 8)) + 1u;
    const uint32_t va  = (static_cast<uint32_t>(p[12]) | (static_cast<uint32_t>(p[13]) << 8)) + 1u;
    const uint32_t vb  = (static_cast<uint32_t>(p[14]) | (static_cast<uint32_t>(p[15]) << 8)) + 1u;
    const uint32_t vfp = (static_cast<uint32_t>(p[16]) | ((static_cast<uint32_t>(p[17]) & 0x7Fu) << 8)) + 1u;
    const uint32_t vs  = (static_cast<uint32_t>(p[18]) | (static_cast<uint32_t>(p[19]) << 8)) + 1u;
    if (hb < hfp + hs || vb < vfp + vs) return false;
    memset(t, 0, sizeof(*t));
    t->pclkKhz = pclk;
    t->hActive = static_cast<uint16_t>(ha); t->hFp = static_cast<uint16_t>(hfp);
    t->hSync = static_cast<uint16_t>(hs);   t->hBp = static_cast<uint16_t>(hb - hfp - hs);
    t->hTotal = static_cast<uint16_t>(ha + hb);
    t->vActive = static_cast<uint16_t>(va); t->vFp = static_cast<uint16_t>(vfp);
    t->vSync = static_cast<uint16_t>(vs);   t->vBp = static_cast<uint16_t>(vb - vfp - vs);
    t->vTotal = static_cast<uint16_t>(va + vb);
    t->hPos = (p[9]  >> 7) & 1u;
    t->vPos = (p[17] >> 7) & 1u;
    t->preferred = (p[3] >> 7) & 1u;
    t->source = kSrcDid;
    t->milliHz = milliHzOf(t->pclkKhz, t->hTotal, t->vTotal);
    t->refreshHz = refreshOf(t->milliHz);
    return timingValid(*t);
}

inline void addUnique(Timing *out, unsigned *n, unsigned maxOut, const Timing &t) {
    if (!timingValid(t)) return;
    for (unsigned i = 0; i < *n; i++) {
        if (out[i].hActive == t.hActive && out[i].vActive == t.vActive && out[i].refreshHz == t.refreshHz) return;
    }
    if (*n < maxOut) out[(*n)++] = t;
}

}  // namespace detail

// Parse all progressive modes from an EDID (128..512 bytes) in priority order:
//   block-0 DTDs -> CTA-861 DTDs -> DisplayID Type I/VII -> CTA-861 VICs -> Established DMT -> Standard CVT-RB.
// Returns the number of unique valid modes written to `out`.
inline unsigned parseEdid(const uint8_t *edid, size_t len, Timing *out, unsigned maxOut) {
    if (!edid || len < 128 || !out || !maxOut) return 0;
    static const uint8_t kHdr[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    if (memcmp(edid, kHdr, 8) != 0 || !blockChecksumOk(edid)) return 0;
    unsigned n = 0;

    // 1. Block 0 DTDs (4 x 18 bytes at offset 54)
    for (int d = 0; d < 4; d++) {
        Timing t{};
        if (detail::parseDtd(edid + 54 + 18 * d, d == 0, &t)) detail::addUnique(out, &n, maxOut, t);
    }

    size_t blocks = static_cast<size_t>(edid[126]) + 1;
    if (blocks * 128 > len) blocks = len / 128;
    if (blocks > 4) blocks = 4;

    // 2. CTA-861 DTDs & DisplayID Type I/VII detailed timings (highest precision for high-refresh panels)
    for (size_t b = 1; b < blocks; b++) {
        const uint8_t *blk = edid + b * 128;
        if (!blockChecksumOk(blk)) continue;
        if (blk[0] == 0x02) {   // CTA-861
            const uint8_t dtdStart = blk[2];
            if (dtdStart >= 4 && dtdStart <= 127) {
                for (size_t off = dtdStart; off + 18 <= 127; off += 18) {
                    if (!blk[off] && !blk[off + 1]) break;
                    Timing t{};
                    if (detail::parseDtd(blk + off, false, &t)) detail::addUnique(out, &n, maxOut, t);
                }
            }
        } else if (blk[0] == 0x70) {   // DisplayID 1.x / 2.0 extension block
            // Section starts at byte 1: [1] version, [2] byte count, [3] product type, [4] ext count, [5..] data blocks
            const size_t secLen = blk[2];
            const size_t secEnd = (5 + secLen <= 127) ? (5 + secLen) : 127;
            for (size_t pos = 5; pos + 3 <= secEnd;) {
                const uint8_t tag = blk[pos];
                const uint8_t dlen = blk[pos + 2];
                if (pos + 3 + dlen > secEnd) break;
                if (tag == 0x03 || tag == 0x22) {   // Type I or Type VII detailed timing
                    for (size_t k = 0; k + 20 <= dlen; k += 20) {
                        Timing t{};
                        if (detail::parseDisplayId20(blk + pos + 3 + k, tag == 0x22, &t))
                            detail::addUnique(out, &n, maxOut, t);
                    }
                }
                if (!dlen && !tag) break;
                pos += 3u + dlen;
            }
        }
    }

    // 3. CTA-861 Video Data Block VICs
    for (size_t b = 1; b < blocks; b++) {
        const uint8_t *blk = edid + b * 128;
        if (!blockChecksumOk(blk) || blk[0] != 0x02) continue;
        uint8_t dtdStart = blk[2];
        if (dtdStart < 4 || dtdStart > 127) dtdStart = 127;
        for (size_t i = 4; i < dtdStart;) {
            const uint8_t tag = blk[i] >> 5, dlen = blk[i] & 0x1Fu;
            if (i + 1 + dlen > dtdStart) break;
            if (tag == 2) {   // Video Data Block
                for (size_t k = 0; k < dlen; k++) {
                    const uint8_t raw = blk[i + 1 + k];
                    const uint8_t vic = (raw >= 129 && raw <= 192) ? static_cast<uint8_t>(raw & 0x7Fu) : raw;
                    for (const auto &r : detail::kCtaVics) {
                        if (r.vic == vic) {
                            Timing t{};
                            detail::fromRow(r, kSrcVic, &t);
                            detail::addUnique(out, &n, maxOut, t);
                            break;
                        }
                    }
                }
            }
            i += 1u + dlen;
        }
    }

    // 4. Block 0 Established Timings
    for (const auto &e : detail::kEst) {
        if (edid[e.byteIdx] & e.bitMask) {
            Timing t{};
            detail::fromRow(e.r, kSrcDmt, &t);
            detail::addUnique(out, &n, maxOut, t);
        }
    }

    // 5. Block 0 Standard Timings (8 x 2 bytes at offset 38) -> CVT-RB
    for (int s = 0; s < 8; s++) {
        const uint8_t b0 = edid[38 + 2 * s], b1 = edid[39 + 2 * s];
        if (b0 == 0x01 && b1 == 0x01) continue;
        if (b0 == 0x00 && b1 == 0x00) continue;
        const unsigned ha = (static_cast<unsigned>(b0) + 31u) * 8u;
        const unsigned ar = b1 >> 6;
        const unsigned va = ar == 0 ? (ha * 10u / 16u) : ar == 1 ? (ha * 3u / 4u)
                          : ar == 2 ? (ha * 4u / 5u)   : (ha * 9u / 16u);
        const unsigned hz = (b1 & 0x3Fu) + 60u;
        Timing t{};
        if (cvtRb(ha, va, hz, &t)) detail::addUnique(out, &n, maxOut, t);
    }

    return n;
}

// ---- DCN 3.2.0 DENTIST divider math (Linux dc/clk_mgr dentist_get_divider_from_did) ---------------------------------

inline uint32_t dentistDidFromUnits(uint32_t units) {
    if (units <= 8)   return 0x08u;
    if (units < 64)   return 0x08u + (units - 8u);
    if (units < 128)  return 0x40u + (units - 64u) / 2u;
    if (units < 248)  return 0x60u + (units - 128u) / 4u;
    return 0x7Eu;
}

// Compute DENTIST_DISPCLK_CNTL divider value (bits 30:24 DISPCLK, 14:8 DPP0, 6:0 DPP1) for pclkKhz.
// DCN 3.2 DENTIST VCO = 4.3 GHz, divider in quarter-steps, +1.75% headroom as Linux amdgpu uses.
inline uint32_t dentistForPclk(uint32_t pclkKhz, uint32_t *didOut = nullptr) {
    // target = pclk * 1.0175; units = floor(4 * 4300000 / target) so actual DISPCLK >= target
    const uint64_t targetKhz = (static_cast<uint64_t>(pclkKhz) * 10175ull + 9999ull) / 10000ull;
    const uint64_t vcoQuarterKhz = 4ull * 4300000ull;
    const uint32_t units = targetKhz ? static_cast<uint32_t>(vcoQuarterKhz / targetKhz) : 0x7Eu;
    const uint32_t did = dentistDidFromUnits(units) & 0x7Fu;
    if (didOut) *didOut = did;
    return (did << 24) | (did << 8) | did;
}

// ---- DCN 3.2.0 Dynamic Plan Synthesizer -----------------------------------------------------------------------------
// Generates a complete phxp0::Plan for ANY valid Timing, using exact OTG/MSA/DTO/HUBP/DENTIST formulas verified against
// all 6 real RX 7800 XT Linux captures (1080p60 + 1440p 60/100/120/144/165). Every step is strictly within phxp0::kAllowP0.
// digShift = phxp0::kPipeDigShift (compiled build DIG; phx_pipe0.cpp relocates to the live lit DIG at I/O time).
inline bool synthesize(const Timing &t, uint8_t bpc, DynPlan *out, int digShift = phxp0::kPipeDigShift) {
    if (!out || !timingValid(t)) return false;
    memset(out, 0, sizeof(*out));
    out->timing = t;
    out->isStatic = false;
    out->staticPlan = nullptr;

    const uint32_t ha = t.hActive, ht = t.hTotal, hfp = t.hFp, hs = t.hSync, hbp = t.hBp;
    const uint32_t va = t.vActive, vt = t.vTotal, vfp = t.vFp, vs = t.vSync, vbp = t.vBp;
    const uint32_t hb = hs + hbp;
    const uint32_t vb = vs + vbp;
    const uint32_t vBlankTot = vt - va;
    const uint32_t pclk = t.pclkKhz;
    const uint8_t  depth = (bpc == 10) ? 10 : 8;

    // Exact OTG / VTG / DSCL / MSA fields
    const uint32_t hBlankReg = (ht - hfp) | (hb << 16);
    const uint32_t vBlankReg = (vt - vfp) | (vb << 16);
    const uint32_t vStartup  = vBlankTot - 1u;
    const uint32_t vUpdateW  = (ha <= 1280u) ? 182u : 247u;
    const uint32_t vUpdate   = (vUpdateW << 16) | (ht / 4u);
    const uint32_t vReady    = (ha <= 1280u) ? 236u : 328u;
    const uint32_t vtgCtrl   = ((vt - vfp) << 16) | (vfp > 2u ? (vfp - 2u) : 1u);

    // Exact DML / HUBPREQ0 formulas (refclk = 100 MHz, verified 6/6 bit-exact on Navi 32 Linux captures)
    const uint32_t bo0Lo     = static_cast<uint32_t>(static_cast<uint64_t>(hfp + 8u) * 100000ull / pclk);
    const uint32_t bo0       = (vb << 16) | (bo0Lo & 0x1fffu);
    const uint32_t bo1       = ((vt - vfp + 1u) << 2) & 0x3ffffu;
    const uint32_t dstDim    = static_cast<uint32_t>(static_cast<uint64_t>(ht) * 12800000ull / pclk) & 0x1fffffu;
    uint32_t dstAfter        = static_cast<uint32_t>(10989000ull / pclk);
    if (dstAfter < 4u) dstAfter = 4u;
    if (dstAfter > 0x1fffu) dstAfter = 0x1fffu;
    const uint32_t ref2pix   = static_cast<uint32_t>(26214400000ull / pclk) & 0x1fffffu;
    uint32_t perLine         = static_cast<uint32_t>(static_cast<uint64_t>(ha) * 50000ull / pclk);
    if (perLine < 1u) perLine = 1u;
    if (perLine > 0x1fffu) perLine = 0x1fffu;

    // Prefetch lines (u6.2 fixed point, ~64 us prefetch window clamped safely inside vblank) + VRATIO_PREFETCH = 1.0 (0x080000)
    uint32_t dstYPref = static_cast<uint32_t>((256ull * pclk + (static_cast<uint64_t>(ht) * 500ull)) / (static_cast<uint64_t>(ht) * 1000ull));
    const uint32_t maxPref = vBlankTot > 6u ? (vBlankTot - 4u) * 4u : 16u;
    if (dstYPref < 16u) dstYPref = 16u;
    if (dstYPref > maxPref) dstYPref = maxPref;
    if (dstYPref > 252u) dstYPref = 252u;
    const uint32_t prefetch  = (dstYPref << 24) | 0x080000u;

    // TTU watermarks (linear surface, refclk = 100 MHz)
    uint32_t qosWm = static_cast<uint32_t>(static_cast<uint64_t>(ht) * 200000ull / pclk);
    if (qosWm > 0x3fffu) qosWm = 0x3fffu;
    uint32_t ttu = static_cast<uint32_t>(3276800000ull / pclk);
    if (ttu < 0x200u) ttu = 0x200u;
    if (ttu > 0xffffu) ttu = 0xffffu;
    // MIN_TTU_VBLANK: conservative fraction of remaining vblank time after prefetch, in 100 MHz refclk cycles
    const uint32_t remLines = vBlankTot > (dstYPref / 4u + 5u) ? (vBlankTot - dstYPref / 4u - 5u) : 4u;
    uint32_t minTtuVblank = static_cast<uint32_t>((static_cast<uint64_t>(remLines) * ht * 50000ull) / pclk);
    if (minTtuVblank < 0x0400u) minTtuVblank = 0x0400u;
    if (minTtuVblank > 0x7fffu) minTtuVblank = 0x7fffu;

    // DISPCLK/DPPCLK divider and DALSMC hard minimum floor
    uint32_t did = 0;
    const uint32_t dentistVal = dentistForPclk(pclk, &did);

    // Geometry registers (HUBP0 viewport + DPG dimensions + HUBPREQ0 surface pitch)
    const uint32_t viewport = ha | (va << 16);
    const uint32_t dpgDims  = (ha << 16) | va;
    const uint32_t pitchReg = pitchPixels(ha) - 1u;

    // DP MSA registers (at compiled build DIG; phx_pipe0.cpp relocates to live DIG)
    const uint32_t msaBase = static_cast<uint32_t>(0x0570C + digShift);
    const uint32_t dpBase  = static_cast<uint32_t>(0x056C8 + digShift);
    const uint32_t msa1 = (ht << 16) | vt;
    const uint32_t msa2 = (hb << 16) | vb;
    const uint32_t msa3 = ((hs | (t.hPos ? 0u : 0x8000u)) << 16) | (vs | (t.vPos ? 0u : 0x8000u));
    const uint32_t msa4 = (ha << 16) | va;

    unsigned n = 0;
    auto push = [&](uint8_t stage, uint16_t pad, uint32_t reg, uint32_t val, uint32_t mask, const char *nm) {
        if (n >= kMaxSteps) return;
        phx_s2_step &s = out->steps[n++];
        s.op = PHX_S2_W; s.stage = stage; s.pad = pad;
        s.reg = reg; s.val = val; s.mask = mask; s.name = nm;
    };

    // Group 1: clocks & DTO (GOP 1-ppc convention: full pixel clock in Hz).
    // Only high-pclk modes (> 250 MHz) emit a DENTIST step (matching 1080p60n32 / 1440p60n32): raising DISPCLK via DAL
    // before the initial round-trip on a <= 250 MHz boot mode would otherwise disagree with a static DENTIST step.
    const bool needDal = pclk > 250000u;
    if (needDal) push(1, 0, 0x00124, dentistVal, 0x7f007f7fu, "DENTIST_DISPCLK_CNTL");
    push(1, 0, 0x00141, pclk * 1000u, 0xffffffffu, "DP_DTO0_PHASE");

    // Group 2: OTG0 timing
    push(2, 4, 0x04FEA, ht - 1u, 0x00007fffu, "OTG0_OTG_H_TOTAL");
    push(2, 4, 0x04FEC, hs << 16, 0x7fff7fffu, "OTG0_OTG_H_SYNC_A");
    push(2, 4, 0x04FEB, hBlankReg, 0x7fff7fffu, "OTG0_OTG_H_BLANK_START_END");
    push(2, 4, 0x04FED, t.hPos ? 0u : 1u, 0x00030001u, "OTG0_OTG_H_SYNC_A_CNTL");
    push(2, 4, 0x04FEF, vt - 1u, 0x00007fffu, "OTG0_OTG_V_TOTAL");
    push(2, 4, 0x04FF7, vs << 16, 0x7fff7fffu, "OTG0_OTG_V_SYNC_A");
    push(2, 4, 0x04FF6, vBlankReg, 0x7fff7fffu, "OTG0_OTG_V_BLANK_START_END");
    push(2, 4, 0x04FF8, t.vPos ? 0u : 1u, 0x00000101u, "OTG0_OTG_V_SYNC_A_CNTL");

    // Group 3: frame position & bit depth
    push(3, 6, 0x05046, vStartup, 0x000003ffu, "OTG0_OTG_VSTARTUP_PARAM");
    push(3, 6, 0x05047, vUpdate, 0x03ffffffu, "OTG0_OTG_VUPDATE_PARAM");
    push(3, 4, 0x05048, vReady, 0x0000ffffu, "OTG0_OTG_VREADY_PARAM");
    push(3, 6, 0x039EE, vtgCtrl, 0x7fff7fffu, "VTG0_CONTROL");
    push(3, 6, 0x041CF, hBlankReg, 0x3fff3fffu, "DSCL0_OTG_H_BLANK");
    push(3, 6, 0x041D0, vBlankReg, 0x3fff3fffu, "DSCL0_OTG_V_BLANK");
    push(3, 4, 0x041D2, viewport, 0xffffffffu, "DSCL0_RECOUT_SIZE");
    push(3, 4, 0x041D3, viewport, 0xffffffffu, "DSCL0_MPC_SIZE");
    push(3, 4, 0x04D01, depth == 10 ? 0x0000d100u : 0x0000c900u, 0xffffffffu, "FMT0_FMT_BIT_DEPTH_CONTROL");

    // Group 4: HUBPREQ0 DLG/TTU + geometry
    push(4, 6, 0x03B03, bo0, 0x7fff1fffu, "HUBPREQ0_BLANK_OFFSET_0");
    push(4, 6, 0x03B04, bo1, 0x0003ffffu, "HUBPREQ0_BLANK_OFFSET_1");
    push(4, 6, 0x03B05, dstDim, 0x001fffffu, "HUBPREQ0_DST_DIMENSIONS");
    push(4, 4, 0x03B06, dstAfter, 0x00071fffu, "HUBPREQ0_DST_AFTER_SCALER");
    push(4, 6, 0x03B1C, ref2pix, 0x001fffffu, "HUBPREQ0_REF_FREQ_TO_PIX_FREQ");
    push(4, 6, 0x03B1A, perLine, 0x1fff1fffu, "HUBPREQ0_PER_LINE_DELIVERY");
    push(4, 4, 0x03B19, perLine, 0x1fff1fffu, "HUBPREQ0_PER_LINE_DELIVERY_PRE");
    push(4, 4, 0x03B07, prefetch, 0xff3fffffu, "HUBPREQ0_PREFETCH_SETTINGS");
    push(4, 4, 0x03B09, 0x00000201u, 0x00003f7fu, "HUBPREQ0_VBLANK_PARAMETERS_0");
    push(4, 6, 0x03AE9, qosWm << 16, 0x3fff3fffu, "HUBPREQ0_DCN_TTU_QOS_WM");
    push(4, 4, 0x03AEA, 0xe0000000u | minTtuVblank, 0xfbffffffu, "HUBPREQ0_DCN_GLOBAL_TTU_CNTL");
    push(4, 4, 0x03AEB, 0x08000000u | ttu, 0x1f7fffffu, "HUBPREQ0_DCN_SURF0_TTU_CNTL0");
    push(4, 4, 0x03AEC, ttu, 0x007fffffu, "HUBPREQ0_DCN_SURF0_TTU_CNTL1");
    push(4, 0, 0x03AAA, viewport, 0xffffffffu, "HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION");
    push(4, 0, 0x03AAE, viewport, 0xffffffffu, "HUBP0_DCSURF_SEC_VIEWPORT_DIMENSION");
    push(4, 0, 0x04D16, dpgDims, 0xffffffffu, "DPG0_DPG_DIMENSIONS");
    push(4, 0, 0x03AC7, pitchReg, 0xffffffffu, "HUBPREQ0_DCSURF_SURFACE_PITCH");

    // Group 5: DP MSA & colour depth
    push(5, 4, msaBase + 0u, msa1, 0xffffffffu, "DP_MSA_TIMING_PARAM1");
    push(5, 4, msaBase + 1u, msa2, 0xffffffffu, "DP_MSA_TIMING_PARAM2");
    push(5, 4, msaBase + 2u, msa3, 0xffffffffu, "DP_MSA_TIMING_PARAM3");
    push(5, 4, msaBase + 3u, msa4, 0xffffffffu, "DP_MSA_TIMING_PARAM4");
    push(5, 0, dpBase + 1u, depth == 10 ? 0x02000000u : 0x01000000u, 0x07000000u, "DP_PIXEL_FORMAT");
    push(5, 0, dpBase + 2u, depth == 10 ? 0x40000000u : 0x20000000u, 0xff000000u, "DP_MSA_COLORIMETRY");

    snprintf(out->name, sizeof out->name, "dyn%ux%u@%u", ha, va, t.refreshHz);

    // DALSMC display-clock floors (PPCLK_DISPCLK=8, PPCLK_DPPCLK=9) for high pixel clocks (> 250 MHz).
    if (needDal) {
        const uint32_t floorMhz = ((pclk + 9999u) / 10000u) * 10u;
        out->dalMin[0] = (8u << 16) | (floorMhz & 0xffffu);
        out->dalMin[1] = (9u << 16) | (floorMhz & 0xffffu);
    }

    out->plan.s = out->steps;
    out->plan.n = n;
    out->plan.info = phx_p0_info{
        t.refreshHz, ht - 1u, vt - 1u, pclk,
        needDal ? did : 0u,
        0u,   // GOP 1-ppc DTO convention
        1u,   // ready
        0u,   // todo = 0
        0u, 0u,
        out->name
    };
    out->plan.detMin = needDal ? 4u : 0u;
    out->plan.dalMin = needDal ? out->dalMin : nullptr;
    out->plan.opts   = phxp0::kOptStickyClear;
    return true;
}

// Reconstruct a Timing from a live OTG0 + DP_DTO0_PHASE snapshot so the boot mode is always representable.
inline bool timingFromLiveRegs(uint32_t hTotalReg, uint32_t hBlankReg, uint32_t hSyncReg, uint32_t hSyncCntl,
                               uint32_t vTotalReg, uint32_t vBlankReg, uint32_t vSyncReg, uint32_t vSyncCntl,
                               uint32_t dtoPhaseHz, Timing *out) {
    const uint32_t ht = (hTotalReg & 0x7fffu) + 1u;
    const uint32_t vt = (vTotalReg & 0x7fffu) + 1u;
    const uint32_t hBlankStart = hBlankReg & 0x7fffu;
    const uint32_t hBlankEnd   = (hBlankReg >> 16) & 0x7fffu;
    const uint32_t vBlankStart = vBlankReg & 0x7fffu;
    const uint32_t vBlankEnd   = (vBlankReg >> 16) & 0x7fffu;
    const uint32_t hs = (hSyncReg >> 16) & 0x7fffu;
    const uint32_t vs = (vSyncReg >> 16) & 0x7fffu;
    if (hBlankStart <= hBlankEnd || vBlankStart <= vBlankEnd) return false;
    const uint32_t ha = hBlankStart - hBlankEnd;
    const uint32_t va = vBlankStart - vBlankEnd;
    if (ht <= hBlankStart || vt <= vBlankStart || hBlankEnd < hs || vBlankEnd < vs) return false;
    memset(out, 0, sizeof(*out));
    out->pclkKhz = (dtoPhaseHz + 500u) / 1000u;
    out->hActive = static_cast<uint16_t>(ha);
    out->hFp     = static_cast<uint16_t>(ht - hBlankStart);
    out->hSync   = static_cast<uint16_t>(hs);
    out->hBp     = static_cast<uint16_t>(hBlankEnd - hs);
    out->hTotal  = static_cast<uint16_t>(ht);
    out->vActive = static_cast<uint16_t>(va);
    out->vFp     = static_cast<uint16_t>(vt - vBlankStart);
    out->vSync   = static_cast<uint16_t>(vs);
    out->vBp     = static_cast<uint16_t>(vBlankEnd - vs);
    out->vTotal  = static_cast<uint16_t>(vt);
    out->hPos    = (hSyncCntl & 1u) ? 0u : 1u;
    out->vPos    = (vSyncCntl & 1u) ? 0u : 1u;
    out->source  = kSrcLive;
    out->milliHz = milliHzOf(out->pclkKhz, out->hTotal, out->vTotal);
    out->refreshHz = refreshOf(out->milliHz);
    return timingValid(*out);
}

// Match a Timing against a verified static plan (exact OTG totals + refresh within 1 Hz).
inline const phxp0::Plan *matchStatic(const Timing &t, const phxp0::Plan *const *statics, unsigned nStatics) {
    if (!statics || !nStatics) return nullptr;
    for (unsigned i = 0; i < nStatics; i++) {
        const phxp0::Plan *p = statics[i];
        if (!p || !p->info.ready) continue;
        if (p->info.hTotal + 1u != t.hTotal || p->info.vTotal + 1u != t.vTotal) continue;
        const uint32_t dh = p->info.hz > t.refreshHz ? p->info.hz - t.refreshHz : t.refreshHz - p->info.hz;
        if (dh <= 1u) return p;
    }
    return nullptr;
}

// Extract Timing from a static plan's OTG/viewport registers (for publishing static-only fallback rows).
inline bool timingFromStaticPlan(const phxp0::Plan &p, Timing *out) {
    auto get = [&](uint32_t reg, uint32_t dflt) -> uint32_t {
        for (unsigned i = 0; i < p.n; i++) if (p.s[i].reg == reg) return p.s[i].val;
        return dflt;
    };
    return timingFromLiveRegs(
        get(0x04FEA, p.info.hTotal), get(0x04FEB, 0), get(0x04FEC, 0), get(0x04FED, 0),
        get(0x04FEF, p.info.vTotal), get(0x04FF6, 0), get(0x04FF7, 0), get(0x04FF8, 0),
        p.info.pclkKhz * 1000u, out);
}

// Build a hybrid Catalog from an EDID + live boot timing + optional verified static plans.
// Modes matching a static plan keep that exact static plan; all other EDID modes that fit `cap` get synthesized plans.
inline unsigned buildCatalog(const uint8_t *edid, size_t edidLen, const LinkCap &cap,
                             const Timing *liveBoot,
                             const phxp0::Plan *const *statics, unsigned nStatics,
                             Catalog *cat, int digShift = phxp0::kPipeDigShift) {
    memset(cat, 0, sizeof(*cat));
    cat->maxLinkKbps = dpLinkDataKbps(cap.lanes, cap.rateCode);
    const uint8_t bpc = cap.bpc == 10 ? 10 : 8;

    Timing parsed[kMaxModes * 2];
    const unsigned np = (edid && edidLen >= 128) ? parseEdid(edid, edidLen, parsed, kMaxModes * 2) : 0;
    cat->parsedTotal = np;

    auto appendTiming = [&](const Timing &t, bool forceAccept) {
        if (cat->n >= kMaxModes) { cat->filteredOut++; return; }
        for (unsigned i = 0; i < cat->n; i++) {
            const Timing &e = cat->entries[i].timing;
            if (e.hActive == t.hActive && e.vActive == t.vActive && e.refreshHz == t.refreshHz) return;
        }
        const uint8_t chosenBpc = selectBpc(t, cap);
        if (!forceAccept && !chosenBpc) { cat->filteredOut++; return; }
        const uint8_t modeBpc = chosenBpc ? chosenBpc : bpc;
        DynPlan &slot = cat->entries[cat->n];
        const phxp0::Plan *sp = matchStatic(t, statics, nStatics);
        // Never select a 10-bpc static plan when the live DP link only has bandwidth for 8 bpc.
        if (sp && cat->maxLinkKbps && modeRequiredKbps(t.pclkKhz, staticPlanBpc(*sp)) > cat->maxLinkKbps) sp = nullptr;
        if (sp) {
            memset(&slot, 0, sizeof(slot));
            slot.timing = t;
            slot.isStatic = true;
            slot.staticPlan = sp;
            slot.plan = *sp;
            cat->ptrs[cat->n] = sp;
            cat->n++;
        } else if (synthesize(t, modeBpc, &slot, digShift)) {
            cat->ptrs[cat->n] = &slot.plan;
            cat->n++;
        } else {
            cat->filteredOut++;
        }
    };

    // 1. Add all EDID modes that fit the link
    for (unsigned i = 0; i < np; i++) appendTiming(parsed[i], false);

    // 2. Ensure the live boot mode is always present (even if EDID omitted it or EDID read failed)
    if (liveBoot && timingValid(*liveBoot)) appendTiming(*liveBoot, true);

    // 3. If EDID yielded nothing, fall back cleanly to the compiled static plans
    if (!cat->n && statics && nStatics) {
        for (unsigned i = 0; i < nStatics && cat->n < kMaxModes; i++) {
            if (!statics[i] || !statics[i]->info.ready) continue;
            Timing t{};
            if (timingFromStaticPlan(*statics[i], &t)) appendTiming(t, true);
        }
    }

    // Re-link pointers inside cat->entries (in case struct was moved)
    for (unsigned i = 0; i < cat->n; i++) {
        if (!cat->entries[i].isStatic) {
            cat->entries[i].plan.s = cat->entries[i].steps;
            cat->entries[i].plan.info.name = cat->entries[i].name;
            cat->entries[i].plan.dalMin = cat->entries[i].dalMin[0] ? cat->entries[i].dalMin : nullptr;
            cat->ptrs[i] = &cat->entries[i].plan;
        } else {
            cat->ptrs[i] = cat->entries[i].staticPlan;
        }
    }
    return cat->n;
}

// Convert a Catalog into the phxp0pub::Mode array published to PhxAMDFB.
inline unsigned toPublishedModes(const Catalog &cat, phxp0pub::Mode *out, unsigned maxOut) {
    unsigned n = 0;
    for (unsigned i = 0; i < cat.n && n < maxOut; i++) {
        const Timing &t = cat.entries[i].timing;
        phxp0pub::Mode &m = out[n];
        memset(&m, 0, sizeof(m));
        m.id         = n + 1u;
        m.w          = t.hActive;
        m.h          = t.vActive;
        m.milliHz    = t.refreshHz * 1000u;
        m.pclkHz     = t.pclkKhz;
        m.hTotal     = t.hTotal;
        m.vTotal     = t.vTotal;
        m.hBlank     = t.hTotal - t.hActive;
        m.hSyncOff   = t.hFp;
        m.hSyncWidth = t.hSync;
        m.vBlank     = t.vTotal - t.vActive;
        m.vSyncOff   = t.vFp;
        m.vSyncWidth = t.vSync;
        m.hPos       = t.hPos;
        m.vPos       = t.vPos;
        m.key        = kKeyIdxBase + i;
        n++;
    }
    return n;
}

// Resolve a target_hz key from phxPipe0SetMode against a Catalog:
//   1. kKeyIdxBase + idx -> exact catalog entry (unambiguous for arbitrary resolutions)
//   2. height * 1000 + hz -> first catalog entry with matching height and refresh
//   3. bare hz (< 1000) -> catalog entry matching boot/preferred height + hz, else first matching hz
inline const phxp0::Plan *pickFromCatalog(const Catalog &cat, uint32_t key, uint32_t prefH = 0) {
    if (key >= kKeyIdxBase) {
        const uint32_t idx = key - kKeyIdxBase;
        return idx < cat.n ? cat.ptrs[idx] : nullptr;
    }
    const uint32_t wantH  = key >= 1000u ? (key / 1000u) : 0u;
    const uint32_t wantHz = key >= 1000u ? (key % 1000u) : key;
    const phxp0::Plan *fallback = nullptr;
    for (unsigned i = 0; i < cat.n; i++) {
        const Timing &t = cat.entries[i].timing;
        if (t.refreshHz != wantHz) continue;
        if (wantH && t.vActive != wantH) continue;
        if (!fallback) fallback = cat.ptrs[i];
        if (wantH || !prefH || t.vActive == prefH) return cat.ptrs[i];
    }
    return fallback;
}

}  // namespace phxdyn
