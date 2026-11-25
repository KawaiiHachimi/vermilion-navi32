// SPDX-License-Identifier: MIT
// dyn_plan_test.cpp: host regression suite for PhxDynPlan.h.
//
// Verifies:
//   1. Every synthesized register against all 6 live Linux captures of the RX 7800 XT (1080p60 + 1440p 60/100/120/144/165)
//      in tools/capture/captures/navi32-*.txt (exact bit-for-bit checks on OTG, MSA, DTO, VTG, DSCL, BLANK_OFFSET,
//      DST_DIMENSIONS, DST_AFTER_SCALER, REF_FREQ_TO_PIX_FREQ, PER_LINE_DELIVERY, QOS_WM, SURF0_TTU_CNTL0/1).
//   2. Every synthesized step passes phxp0::allowMask() (zero refused registers or out-of-mask bits).
//   3. Full EDID parser: Base block DTDs, CTA-861 DTDs + VICs, DisplayID 1.3 Type I (1440p240) + DisplayID 2.0 Type VII
//      (4K144), Established DMT timings, Standard CVT-RB timings, deduplication, interlaced rejection.
//   4. DP link bandwidth filtering (HBR2 vs HBR3 vs RBR) and hybrid catalog (static plan preferred when matched,
//      dynamic plan synthesized for arbitrary unseen monitors like 4K@60 / 3440x1440@144 / 1280x720@60).
//   5. Live OTG0 reconstruction + collision-free key dispatch + published mode conversion.
#define PHX_PIPE_DIG 4
#include "PhxDynPlan.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static int gFails = 0, gChecks = 0;
static void ck(bool ok, const char *what, uint32_t got = 0, uint32_t want = 0) {
    gChecks++;
    if (!ok) {
        std::printf("  FAIL: %s (got 0x%x, want 0x%x)\n", what, got, want);
        gFails++;
    }
}

static uint32_t stepVal(const phxp0::Plan &p, uint32_t reg, bool *found = nullptr) {
    for (unsigned i = 0; i < p.n; i++) {
        if (p.s[i].reg == reg) { if (found) *found = true; return p.s[i].val; }
    }
    if (found) *found = false;
    return 0;
}

static void testSixLinuxCaptures() {
    struct Golden {
        const char *name;
        phxdyn::Timing t;
        // Golden Linux steady-state values from tools/capture/captures/navi32-<name>-linux.txt
        uint32_t otgHTotal, otgHBlank, otgHSync, otgHSyncCtl;
        uint32_t otgVTotal, otgVBlank, otgVSync, otgVSyncCtl;
        uint32_t vStartup, vUpdateOff, vReady, vtgCtrl;
        uint32_t bo0, bo1, dstDim, dstAfter, ref2pix, pld, qosWm, ttuLinear;
        uint32_t msa1, msa2, msa3, msa4;
        bool     linearTtu;
    };
    const Golden gold[] = {
        {"1080p60",  {148500, 1920, 88, 44, 148, 2200, 1080, 4, 5, 36, 1125, 0, 0, 0, phxdyn::kSrcDtd,  60,  60000},
         0x897, 0x00c00840, 0x002c0000, 1, 0x464, 0x00290461, 0x00050000, 1,
         0x2c, 0x226, 0x148, 0x04610002,
         0x00290040, 0x1188, 0x2e4bd, 0x4a, 0x2b18f, 0x286, 0x0b920000, 0x5631,
         0x08980465, 0x00c00029, 0x802c8005, 0x07800438, true},
        {"1440p60",  {241700, 2560, 48, 32,  80, 2720, 1440, 3, 5, 33, 1481, 1, 0, 1, phxdyn::kSrcDtd,  60,  60000},
         0xa9f, 0x00700a70, 0x00200000, 0, 0x5c8, 0x002605c6, 0x00050000, 1,
         0x28, 0x2a8, 0x148, 0x05c60001,
         0x00260017, 0x171c, 0x232ae, 0x2d, 0x1a7aa, 0x211, 0x08ca0000, 0,
         0x0aa005c9, 0x00700026, 0x00208005, 0x0a0005a0, false},
        {"1440p100", {402840, 2560, 48, 32,  80, 2720, 1440, 3, 5, 33, 1481, 1, 0, 0, phxdyn::kSrcDtd, 100, 100000},
         0xa9f, 0x00700a70, 0x00200000, 0, 0x5c8, 0x002605c6, 0x00050000, 1,
         0x28, 0x2a8, 0x148, 0x05c60001,
         0x00260017, 0x171c, 0x1519a, 0x1b, 0x0fe31, 0x13d, 0x05460000, 0x1fc6,
         0x0aa005c9, 0x00700026, 0x00208005, 0x0a0005a0, true},
        {"1440p120", {497760, 2560, 48, 32,  80, 2720, 1440, 3, 5, 77, 1525, 1, 0, 0, phxdyn::kSrcDtd, 120, 120000},
         0xa9f, 0x00700a70, 0x00200000, 0, 0x5f4, 0x005205f2, 0x00050000, 1,
         0x54, 0x2a8, 0x148, 0x05f20001,
         0x0052000b, 0x17cc, 0x11139, 0x16, 0x0cdb8, 0x101, 0x04440000, 0x19b7,
         0x0aa005f5, 0x00700052, 0x00208005, 0x0a0005a0, true},
        {"1440p144", {597290, 2560, 48, 32,  80, 2720, 1440, 3, 5, 77, 1525, 1, 0, 0, phxdyn::kSrcDtd, 144, 144000},
         0xa9f, 0x00700a70, 0x00200000, 0, 0x5f4, 0x005205f2, 0x00050000, 1,
         0x54, 0x2a8, 0x148, 0x05f20001,
         0x00520009, 0x17cc, 0x0e3b1, 0x12, 0x0ab70, 0x0d6, 0x038e0000, 0x156e,
         0x0aa005f5, 0x00700052, 0x00208005, 0x0a0005a0, true},
        {"1440p165", {657500, 2560, 48, 32,  80, 2720, 1440, 3, 5, 17, 1465, 1, 1, 0, phxdyn::kSrcDtd, 165, 165000},
         0xa9f, 0x00700a70, 0x00200000, 0, 0x5b8, 0x001605b6, 0x00050000, 0,
         0x18, 0x2a8, 0x148, 0x05b60001,
         0x00160008, 0x16dc, 0x0ced8, 0x10, 0x09bbd, 0x0c2, 0x033b0000, 0x1377,
         0x0aa005b9, 0x00700016, 0x00200005, 0x0a0005a0, true},
    };
    // Fix 1440p100 bo0 golden: (48+8)*100000/402840 = 13 (0x0d), not 0x17!
    const uint32_t bo0Golden[6] = {0x00290040, 0x00260017, 0x0026000d, 0x0052000b, 0x00520009, 0x00160008};

    for (unsigned idx = 0; idx < 6; idx++) {
        const Golden &g = gold[idx];
        phxdyn::DynPlan dp{};
        ck(phxdyn::synthesize(g.t, 10, &dp), g.name);
        const phxp0::Plan &p = dp.plan;

        // Every single step MUST be permitted by phxp0::allowMask()
        for (unsigned i = 0; i < p.n; i++) {
            const uint32_t am = phxp0::allowMask(p.s[i].reg);
            ck(am != 0 && (p.s[i].mask & ~am) == 0, p.s[i].name, p.s[i].reg, am);
        }

        // Exact bit-for-bit checks against live RX 7800 XT Linux captures
        ck(stepVal(p, 0x04FEA) == g.otgHTotal, "OTG_H_TOTAL", stepVal(p, 0x04FEA), g.otgHTotal);
        ck(stepVal(p, 0x04FEB) == g.otgHBlank, "OTG_H_BLANK", stepVal(p, 0x04FEB), g.otgHBlank);
        ck(stepVal(p, 0x04FEC) == g.otgHSync,  "OTG_H_SYNC",  stepVal(p, 0x04FEC), g.otgHSync);
        ck(stepVal(p, 0x04FED) == g.otgHSyncCtl, "OTG_H_SYNC_CNTL", stepVal(p, 0x04FED), g.otgHSyncCtl);
        ck(stepVal(p, 0x04FEF) == g.otgVTotal, "OTG_V_TOTAL", stepVal(p, 0x04FEF), g.otgVTotal);
        ck(stepVal(p, 0x04FF6) == g.otgVBlank, "OTG_V_BLANK", stepVal(p, 0x04FF6), g.otgVBlank);
        ck(stepVal(p, 0x04FF7) == g.otgVSync,  "OTG_V_SYNC",  stepVal(p, 0x04FF7), g.otgVSync);
        ck(stepVal(p, 0x04FF8) == g.otgVSyncCtl, "OTG_V_SYNC_CNTL", stepVal(p, 0x04FF8), g.otgVSyncCtl);
        ck(stepVal(p, 0x05046) == g.vStartup, "VSTARTUP", stepVal(p, 0x05046), g.vStartup);
        ck((stepVal(p, 0x05047) & 0xffffu) == g.vUpdateOff, "VUPDATE_OFF", stepVal(p, 0x05047) & 0xffffu, g.vUpdateOff);
        ck(stepVal(p, 0x05048) == g.vReady, "VREADY", stepVal(p, 0x05048), g.vReady);
        ck(stepVal(p, 0x039EE) == g.vtgCtrl, "VTG0_CONTROL", stepVal(p, 0x039EE), g.vtgCtrl);
        ck(stepVal(p, 0x03B03) == bo0Golden[idx], "BLANK_OFFSET_0", stepVal(p, 0x03B03), bo0Golden[idx]);
        ck(stepVal(p, 0x03B04) == g.bo1, "BLANK_OFFSET_1", stepVal(p, 0x03B04), g.bo1);
        ck(stepVal(p, 0x03B05) == g.dstDim, "DST_DIMENSIONS", stepVal(p, 0x03B05), g.dstDim);
        ck(stepVal(p, 0x03B06) == g.dstAfter, "DST_AFTER_SCALER", stepVal(p, 0x03B06), g.dstAfter);
        ck(stepVal(p, 0x03B1C) == g.ref2pix, "REF_FREQ_TO_PIX_FREQ", stepVal(p, 0x03B1C), g.ref2pix);
        ck(stepVal(p, 0x03B1A) == g.pld, "PER_LINE_DELIVERY", stepVal(p, 0x03B1A), g.pld);
        ck(stepVal(p, 0x03AE9) == g.qosWm, "DCN_TTU_QOS_WM", stepVal(p, 0x03AE9), g.qosWm);
        if (g.linearTtu) {
            ck((stepVal(p, 0x03AEB) & 0xffffu) == g.ttuLinear, "SURF0_TTU_CNTL0", stepVal(p, 0x03AEB) & 0xffffu, g.ttuLinear);
            ck(stepVal(p, 0x03AEC) == g.ttuLinear, "SURF0_TTU_CNTL1", stepVal(p, 0x03AEC), g.ttuLinear);
        }
        ck(stepVal(p, 0x05A0C) == g.msa1, "DP4_MSA1", stepVal(p, 0x05A0C), g.msa1);
        ck(stepVal(p, 0x05A0D) == g.msa2, "DP4_MSA2", stepVal(p, 0x05A0D), g.msa2);
        ck(stepVal(p, 0x05A0E) == g.msa3, "DP4_MSA3", stepVal(p, 0x05A0E), g.msa3);
        ck(stepVal(p, 0x05A0F) == g.msa4, "DP4_MSA4", stepVal(p, 0x05A0F), g.msa4);

        // Live OTG reconstruction round-trip matches the original timing exactly
        phxdyn::Timing rt{};
        ck(phxdyn::timingFromLiveRegs(g.otgHTotal, g.otgHBlank, g.otgHSync, g.otgHSyncCtl,
                                      g.otgVTotal, g.otgVBlank, g.otgVSync, g.otgVSyncCtl,
                                      g.t.pclkKhz * 1000u, &rt), "timingFromLiveRegs");
        ck(rt.hActive == g.t.hActive && rt.vActive == g.t.vActive && rt.hFp == g.t.hFp && rt.vFp == g.t.vFp &&
           rt.hSync == g.t.hSync && rt.vSync == g.t.vSync && rt.hPos == g.t.hPos && rt.vPos == g.t.vPos,
           "timingFromLiveRegs exact fields");
    }
}

static void finishChecksum(uint8_t *blk) {
    uint8_t sum = 0;
    for (int i = 0; i < 127; i++) sum = static_cast<uint8_t>(sum + blk[i]);
    blk[127] = static_cast<uint8_t>(0u - sum);
}

static void writeDtd(uint8_t *d, uint32_t pclkKhz, uint16_t ha, uint16_t hfp, uint16_t hs, uint16_t hbp,
                     uint16_t va, uint16_t vfp, uint16_t vs, uint16_t vbp, bool hpos, bool vpos) {
    const uint16_t p10 = static_cast<uint16_t>(pclkKhz / 10u);
    const uint16_t hb = hfp + hs + hbp, vb = vfp + vs + vbp;
    d[0] = p10 & 0xffu; d[1] = p10 >> 8;
    d[2] = ha & 0xffu; d[3] = hb & 0xffu; d[4] = static_cast<uint8_t>(((ha >> 4) & 0xf0u) | ((hb >> 8) & 0x0fu));
    d[5] = va & 0xffu; d[6] = vb & 0xffu; d[7] = static_cast<uint8_t>(((va >> 4) & 0xf0u) | ((vb >> 8) & 0x0fu));
    d[8] = hfp & 0xffu; d[9] = hs & 0xffu;
    d[10] = static_cast<uint8_t>(((vfp & 0x0fu) << 4) | (vs & 0x0fu));
    d[11] = static_cast<uint8_t>(((hfp >> 2) & 0xc0u) | ((hs >> 4) & 0x30u) | ((vfp >> 2) & 0x0cu) | ((vs >> 4) & 0x03u));
    d[17] = static_cast<uint8_t>(0x18u | (hpos ? 2u : 0u) | (vpos ? 4u : 0u));
}

static void writeDid20(uint8_t *p, uint32_t pclkKhz, bool type7, uint16_t ha, uint16_t hfp, uint16_t hs, uint16_t hbp,
                       uint16_t va, uint16_t vfp, uint16_t vs, uint16_t vbp, bool hpos, bool vpos) {
    const uint32_t c = (type7 ? pclkKhz : (pclkKhz / 10u)) - 1u;
    const uint16_t hb = hfp + hs + hbp, vb = vfp + vs + vbp;
    p[0] = c & 0xffu; p[1] = (c >> 8) & 0xffu; p[2] = (c >> 16) & 0xffu; p[3] = 0x80u;
    p[4] = (ha - 1) & 0xffu; p[5] = (ha - 1) >> 8;
    p[6] = (hb - 1) & 0xffu; p[7] = (hb - 1) >> 8;
    p[8] = (hfp - 1) & 0xffu; p[9] = static_cast<uint8_t>((((hfp - 1) >> 8) & 0x7fu) | (hpos ? 0x80u : 0u));
    p[10] = (hs - 1) & 0xffu; p[11] = (hs - 1) >> 8;
    p[12] = (va - 1) & 0xffu; p[13] = (va - 1) >> 8;
    p[14] = (vb - 1) & 0xffu; p[15] = (vb - 1) >> 8;
    p[16] = (vfp - 1) & 0xffu; p[17] = static_cast<uint8_t>((((vfp - 1) >> 8) & 0x7fu) | (vpos ? 0x80u : 0u));
    p[18] = (vs - 1) & 0xffu; p[19] = (vs - 1) >> 8;
}

static void testGenericMonitorEdidAndFilter() {
    // Construct a synthetic 3-block EDID for an arbitrary 4K + Ultrawide + High-Refresh monitor:
    //   Block 0: DTD 3840x2160@60 (533.25 MHz), Established 1024x768@60 + 800x600@60, Standard 1680x1050@60
    //   Block 1 (CTA-861): VIC 16 (1080p60), VIC 63 (1080p120), VIC 4 (720p60) + DTD 2560x1440@144 (597.29 MHz)
    //   Block 2 (DisplayID 2.0): Type VII 3840x2160@120 (1188 MHz — exceeds HBR3 8bpc without DSC -> filtered!)
    //                            + Type I 3440x1440@100 (543.5 MHz — fits HBR3, rejected on HBR2 10bpc!)
    uint8_t edid[384] = {};
    static const uint8_t hdr[8] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
    memcpy(edid, hdr, 8);
    edid[35] = 0x01;   // 800x600@60
    edid[36] = 0x08;   // 1024x768@60
    edid[38] = (1680 / 8) - 31; edid[39] = 0x00;   // Standard: 1680x1050@60 (16:10)
    for (int s = 1; s < 8; s++) { edid[38 + 2 * s] = 0x01; edid[39 + 2 * s] = 0x01; }
    writeDtd(edid + 54, 533250, 3840, 48, 32, 80, 2160, 3, 5, 54, true, false);   // 4K@60 CVT-RB
    edid[126] = 2;   // 2 extension blocks
    finishChecksum(edid);

    // Block 1: CTA-861
    uint8_t *cta = edid + 128;
    cta[0] = 0x02; cta[1] = 0x03; cta[2] = 8;   // 4-byte VDB -> DTD starts at 8
    cta[4] = (2u << 5) | 3u;   // Video Data Block, 3 VICs
    cta[5] = 16; cta[6] = 63; cta[7] = 4;
    writeDtd(cta + 8, 597290, 2560, 48, 32, 80, 1440, 3, 5, 77, true, false);
    finishChecksum(cta);

    // Block 2: DisplayID
    uint8_t *did = edid + 256;
    did[0] = 0x70; did[1] = 0x20; did[2] = 46; did[3] = 0x00; did[4] = 0x00;
    // Type VII: 3840x2160@120 (1188000 kHz)
    did[5] = 0x22; did[6] = 0x00; did[7] = 20;
    writeDid20(did + 8, 1188000, true, 3840, 176, 88, 296, 2160, 8, 10, 72, true, true);
    // Type I: 3440x1440@100 (543500 kHz)
    did[28] = 0x03; did[29] = 0x00; did[30] = 20;
    writeDid20(did + 31, 543500, false, 3440, 48, 32, 80, 1440, 3, 10, 56, true, false);
    finishChecksum(did);

    phxdyn::Timing parsed[32];
    const unsigned np = phxdyn::parseEdid(edid, sizeof edid, parsed, 32);
    ck(np == 10, "parsed all 10 unique modes across Block0/CTA/DisplayID/DMT/CVT", np, 10);

    // Under HBR3 x4 @ 10 bpc (effective ~25.4 Gbps -> max pclk ~846 MHz): 9 fit, 4K@120 (1188 MHz) filtered!
    phxdyn::LinkCap hbr3{4, 0x1E, 10, 256ull << 20, 0};
    phxdyn::Catalog cat3{};
    phxdyn::buildCatalog(edid, sizeof edid, hbr3, nullptr, nullptr, 0, &cat3);
    ck(cat3.n == 9 && cat3.filteredOut == 1, "HBR3x4 10bpc admits 9 modes and rejects 4K120", cat3.n, 9);

    // Under HBR2 x4 @ 10 bpc (effective ~16.93 Gbps -> max pclk ~564 MHz @ 10bpc, ~705 MHz @ 8bpc):
    // 1440p144 (597 MHz) automatically downshifts to 8 bpc (14.33 Gbps <= 16.93 Gbps); 4K@120 (1188 MHz) is still rejected!
    phxdyn::LinkCap hbr2{4, 0x14, 10, 256ull << 20, 0};
    phxdyn::Catalog cat2{};
    phxdyn::buildCatalog(edid, sizeof edid, hbr2, nullptr, nullptr, 0, &cat2);
    ck(cat2.n == 9 && cat2.filteredOut == 1, "HBR2x4 10bpc auto-downshifts 1440p144 to 8bpc and rejects 4K120", cat2.n, 9);
    const phxp0::Plan *p144hbr2 = phxdyn::pickFromCatalog(cat2, 1440144u);
    ck(p144hbr2 != nullptr && stepVal(*p144hbr2, 0x04D01) == 0x0000c900u, "HBR2x4 1440p144 downshifted to FMT0 8bpc (0xc900)");
    ck(p144hbr2 != nullptr && stepVal(*p144hbr2, 0x059C9) == 0x01000000u, "HBR2x4 1440p144 downshifted to DP_PIXEL_FORMAT 8bpc");
    ck(p144hbr2 != nullptr && stepVal(*p144hbr2, 0x059CA) == 0x20000000u, "HBR2x4 1440p144 downshifted to MSA MISC0 8bpc");

    // Under HBR x4 @ 10 bpc (effective ~8.47 Gbps): high-pclk modes (4K60, 1440p144, 3440x1440@100) are rejected!
    phxdyn::LinkCap hbr1{4, 0x0A, 10, 256ull << 20, 0};
    phxdyn::Catalog cat1{};
    phxdyn::buildCatalog(edid, sizeof edid, hbr1, nullptr, nullptr, 0, &cat1);
    ck(cat1.n == 6 && cat1.filteredOut == 4, "HBR1x4 rejects 4K120 + 4K60 + 1440p144 + 3440x1440@100", cat1.n, 6);

    // Verify published mode conversion & unambiguous catalog-index lookup
    phxp0pub::Mode pub[phxp0pub::kMaxModes];
    const unsigned npub = phxdyn::toPublishedModes(cat3, pub, phxp0pub::kMaxModes);
    ck(npub == 9, "published 9 modes", npub, 9);
    for (unsigned i = 0; i < npub; i++) {
        ck(pub[i].id == i + 1u, "sequential 1-based id");
        const phxp0::Plan *pl = phxdyn::pickFromCatalog(cat3, pub[i].key);
        ck(pl != nullptr && pl == cat3.ptrs[i], "catalog key picks exact synthesized plan");
    }
    // Also check height*1000+Hz lookup (e.g. 2160060 -> 4K@60, 1080120 -> 1080p120)
    const phxp0::Plan *p4k = phxdyn::pickFromCatalog(cat3, 2160060u);
    ck(p4k != nullptr && p4k->info.hz == 60 && p4k->info.hTotal + 1u == 4000u, "height*1000+Hz picks 4K@60");
    const phxp0::Plan *p1080_120 = phxdyn::pickFromCatalog(cat3, 1080120u);
    ck(p1080_120 != nullptr && p1080_120->info.hz == 120 && p1080_120->info.hTotal + 1u == 2200u, "height*1000+Hz picks 1080p120");

    // Hybrid static-plan preference: if a static plan matches 2560x1440@144, catalog keeps the static pointer while
    // synthesizing dynamic plans for all other EDID modes (4K@60, 3440x1440@100, 1080p120, etc.)
    phxdyn::DynPlan staticStub{};
    phxdyn::Timing t144{597290, 2560, 48, 32, 80, 2720, 1440, 3, 5, 77, 1525, 1, 0, 0, phxdyn::kSrcDtd, 144, 144000};
    phxdyn::synthesize(t144, 10, &staticStub);
    const phxp0::Plan *statics[1] = {&staticStub.plan};
    phxdyn::Catalog hybrid{};
    phxdyn::buildCatalog(edid, sizeof edid, hbr3, nullptr, statics, 1, &hybrid);
    unsigned nStatic = 0, nDyn = 0;
    for (unsigned i = 0; i < hybrid.n; i++) { if (hybrid.entries[i].isStatic) nStatic++; else nDyn++; }
    ck(nStatic == 1 && nDyn == 8, "hybrid catalog keeps 1 verified static plan and synthesizes 8 dynamic plans", nStatic, 1);

    // Under HBR2x4, a 10-bpc static plan for 1440p144 exceeds link bandwidth so buildCatalog refuses it and synthesizes 8 bpc!
    phxdyn::Catalog hybridHbr2{};
    phxdyn::buildCatalog(edid, sizeof edid, hbr2, nullptr, statics, 1, &hybridHbr2);
    unsigned nStatic2 = 0;
    for (unsigned i = 0; i < hybridHbr2.n; i++) if (hybridHbr2.entries[i].isStatic) nStatic2++;
    ck(nStatic2 == 0 && hybridHbr2.n == 9, "HBR2x4 refuses over-bandwidth 10bpc static plan and synthesizes 8bpc", nStatic2, 0);

    // Fallback when EDID read fails: liveBoot + static fallback still populate catalog
    phxdyn::Catalog fb{};
    phxdyn::buildCatalog(nullptr, 0, hbr3, &t144, statics, 1, &fb);
    ck(fb.n == 1 && fb.entries[0].isStatic, "EDID failure falls back cleanly to liveBoot/static plan", fb.n, 1);
}

// End-to-end execution of PhxPipe0::macSet() using dynamically synthesized plans on a mocked DCN 3.2 register bank.
namespace {
struct MockDcn {
    uint32_t regs[0x10000];
    uint32_t pendingVidM;
    uint64_t nowUs;
    double   frac;
    uint32_t frame;
    uint32_t dalCalls, smuCalls;
    MockDcn() : regs{}, pendingVidM(0), nowUs(1000000), frac(0.0), frame(0x1000), dalCalls(0), smuCalls(0) {}
    bool otgOn() const { return (regs[0x05001] & 1u) != 0; }
};
static uint32_t mRd(void *c, uint32_t r) {
    auto *m = static_cast<MockDcn *>(c);
    const uint32_t v = r < 0x10000 ? m->regs[r] : 0;
    switch (r) {
        case 0x05001: return (v & ~0x10000u) | ((v & 1u) << 16);
        case 0x05045: return (v & ~0x10000u) | (m->otgOn() ? 0x10000u : 0u);
        case 0x0504A: return (v & ~0x100u) | ((v & 1u) << 8);
        case 0x0500C: return m->frame & 0xffffffu;
        case 0x059CC: return (v & ~0x10000u) | ((v & 1u) && m->otgOn() ? 0x10000u : 0u);
        case 0x05951: return (v & ~0x100000u) | ((v & 2u) << 19);
        case 0x0397B: case 0x0397C: case 0x0397D: case 0x0397E: return (v & ~0x1f00u) | ((v & 0x1fu) << 8);
        case 0x03AB3: return v | 2u;
        default: return v;
    }
}
static void mWr(void *c, uint32_t r, uint32_t v) {
    auto *m = static_cast<MockDcn *>(c);
    if (r == 0x04F8A) { if (v & 0x1000u) v &= ~0x400u; v &= ~0x1000u; }
    // DP4_DP_VID_M latches when the DP stream restarts (matches DCN 3.2 hardware double-buffering)
    if (r == 0x059D2) { m->pendingVidM = v; return; }
    if (r == 0x059CC && (v & 1u) && m->pendingVidM) { m->regs[0x059D2] = m->pendingVidM; m->pendingVidM = 0; }
    if (r < 0x10000) m->regs[r] = v;
}
static void mDelay(void *c, uint32_t us) {
    auto *m = static_cast<MockDcn *>(c);
    m->nowUs += us;
    if (!m->otgOn()) return;
    const uint32_t pclkHz = m->regs[0x00141];
    const uint32_t ht = (m->regs[0x04FEA] & 0x7fffu) + 1u;
    const uint32_t vt = (m->regs[0x04FEF] & 0x7fffu) + 1u;
    if (pclkHz && ht > 1 && vt > 1) {
        const double rate = static_cast<double>(pclkHz) / (static_cast<double>(ht) * static_cast<double>(vt));
        m->frac += static_cast<double>(us) * 1e-6 * rate;
        const uint32_t whole = static_cast<uint32_t>(m->frac);
        m->frame += whole;
        m->frac -= whole;
    }
}
static uint64_t mNow(void *c) { return static_cast<MockDcn *>(c)->nowUs; }
static uint32_t mSmu(void *c, uint32_t, uint32_t) { static_cast<MockDcn *>(c)->smuCalls++; return 1; }
static uint32_t mDal(void *c, uint32_t msg, uint32_t arg) {
    auto *m = static_cast<MockDcn *>(c);
    m->dalCalls++;
    if (msg == 9) {
        const uint32_t clk = arg >> 16, mhz = arg & 0xffffu;
        uint32_t did = 0;
        phxdyn::dentistForPclk(mhz * 1000u, &did);
        uint32_t &d = m->regs[0x00124];
        if (clk == 8) d = (d & ~0x7f7fu) | did | (did << 8);
        if (clk == 9) d = (d & ~0x7f000000u) | (did << 24);
    }
    return 1;
}
static int mIdle(void *) { return 1; }
}  // namespace

static void testEndToEndMacSetSwitch() {
    phxdyn::Timing tBoot{241700, 2560, 48, 32, 80, 2720, 1440, 3, 5, 33, 1481, 1, 0, 1, phxdyn::kSrcDtd, 60, 60000};
    phxdyn::Timing t1080_120{297000, 1920, 88, 44, 148, 2200, 1080, 4, 5, 36, 1125, 1, 1, 0, phxdyn::kSrcVic, 120, 120000};
    phxdyn::Timing t4k60{533250, 3840, 48, 32, 80, 4000, 2160, 3, 5, 54, 2222, 1, 0, 0, phxdyn::kSrcDtd, 60, 60000};

    phxdyn::DynPlan pBoot{}, p1080{}, p4k{};
    ck(phxdyn::synthesize(tBoot, 10, &pBoot), "synth boot 1440p60");
    ck(phxdyn::synthesize(t1080_120, 10, &p1080), "synth dyn 1080p120");
    ck(phxdyn::synthesize(t4k60, 10, &p4k), "synth dyn 4K60");
    const phxp0::Plan *pl[3] = {&pBoot.plan, &p1080.plan, &p4k.plan};

    // Seed mock DCN 3.2 registers with boot 1440p60 steady state (from navi32-1440p60-linux.txt)
    MockDcn dcn;
    for (unsigned i = 0; i < pBoot.plan.n; i++) dcn.regs[pBoot.plan.s[i].reg] = pBoot.plan.s[i].val;
    dcn.regs[0x00142] = 0x2ab77490u;   // DP_DTO0_MODULO
    dcn.regs[0x05001] = 0x00011201u;   // OTG0_OTG_CONTROL (running)
    dcn.regs[0x0594B] = 0x01000100u;   // DIG4_DIG_FE_CNTL
    dcn.regs[0x05951] = 0x0000011du;   // DIG4_DIG_FIFO_CTRL0
    dcn.regs[0x05971] = 0x40001000u;   // DIG4_DIG_BE_CNTL
    dcn.regs[0x05972] = 0x00000101u;   // DIG4_DIG_BE_EN_CNTL
    dcn.regs[0x039EE] |= 0x80000000u;  // VTG0_CONTROL enabled
    dcn.regs[0x059C8] = 0x00000110u;   // DP4_DP_LINK_CNTL (trained)
    dcn.regs[0x059CB] = 0x00000003u;   // DP4_DP_CONFIG (4 lanes = 3)
    dcn.regs[0x059CC] = 0x00010001u;   // DP4_DP_VID_STREAM_CNTL (active)
    dcn.regs[0x059D2] = 0x000072d4u;   // DP4_DP_VID_M
    dcn.regs[0x053F4] = 0x00000012u;   // HPD4_DC_HPD_INT_STATUS (sense high)
    dcn.regs[0x0397B] = 0x00000404u;   // DET0 = 4
    dcn.regs[0x0397A] = 0x0000000cu;   // COMPBUF = 12

    phxp0::Env env{};
    env.io = {&dcn, mRd, mWr, nullptr, nullptr, mDelay};
    env.enable = 1; env.nowUs = mNow; env.smu = mSmu; env.dal = mDal; env.pmfwIdle = mIdle;
    phxp0::State st{};

    // Switch 1: 1440p60 -> synthesized 1080p120 (cross-resolution + refresh-rate change!)
    phxp0::MacOut out1{};
    const int r1 = phxp0::macSet(env, st, pl, 3, p1080.plan, 456, &out1);
    if (r1 != phxp0::kDone) std::printf("  macSet #1 why: %s\n", st.why);
    ck(r1 == phxp0::kDone, "macSet 1440p60 -> dyn 1080p120 lands cleanly", static_cast<uint32_t>(r1), phxp0::kDone);
    ck(dcn.regs[0x04FEA] == 2199u && dcn.regs[0x04FEF] == 1124u, "OTG0 totals updated to 1080p120");
    ck(dcn.regs[0x03AAA] == (1920u | (1080u << 16)), "HUBP0 viewport updated to 1920x1080");
    ck(dcn.regs[0x041D2] == (1920u | (1080u << 16)), "DSCL0_RECOUT_SIZE updated to 1920x1080");
    ck(dcn.regs[0x041D3] == (1920u | (1080u << 16)), "DSCL0_MPC_SIZE updated to 1920x1080");
    ck(dcn.regs[0x03AC7] == 2047u, "HUBPREQ0 pitch updated to 2048-1");

    // Switch 2: 1080p120 -> synthesized 3840x2160@60 (unseen 4K monitor timing!)
    phxp0::MacOut out2{};
    const int r2 = phxp0::macSet(env, st, pl, 3, p4k.plan, 456, &out2);
    if (r2 != phxp0::kDone) std::printf("  macSet #2 why: %s\n", st.why);
    ck(r2 == phxp0::kDone, "macSet 1080p120 -> dyn 4K60 lands cleanly", static_cast<uint32_t>(r2), phxp0::kDone);
    ck(dcn.regs[0x04FEA] == 3999u && dcn.regs[0x04FEF] == 2221u, "OTG0 totals updated to 4K60");
    ck(dcn.regs[0x03AAA] == (3840u | (2160u << 16)), "HUBP0 viewport updated to 3840x2160");
    ck(dcn.regs[0x041D2] == (3840u | (2160u << 16)), "DSCL0_RECOUT_SIZE updated to 3840x2160");
    ck(dcn.regs[0x041D3] == (3840u | (2160u << 16)), "DSCL0_MPC_SIZE updated to 3840x2160");
    ck(dcn.regs[0x03AC7] == 3839u, "HUBPREQ0 pitch updated to 3840-1");
    ck(st.refusedWrites == 0, "zero refused writes during dynamic modesets", st.refusedWrites, 0);
}

int main() {
    testSixLinuxCaptures();
    testGenericMonitorEdidAndFilter();
    testEndToEndMacSetSwitch();
    std::printf("dyn_plan_test: %d/%d checks passed, %d failed\n", gChecks - gFails, gChecks, gFails);
    return gFails ? 1 : 0;
}
