// SPDX-License-Identifier: MIT
// p0odm_test.cpp: host test of the replay engine (src/vermilion-boot/PhxP0Odm.h + every generated plan: the entry to 240,
// the 1440p rates, the other resolutions and the HDR modes to and from Linux's 144), of Adaptive Sync's range and double
// buffer (vrrRegs, vrrWriteOn, vrrWriteOff) and of the underflow sticky handling, against a model
// of the XTX machine: pipe 0 at the GOP's 1440p144 (macOS read 2026-11-20), pipe 1 dark, status bits that follow their
// enables (OTG busy / clock on, ODM input clock, power-gating FSM, update lock, HUBP outstanding requests, DIG FIFO reset,
// stream status), a frame counter from the DTO and the totals (two pixels per clock when DP_PIXEL_FORMAT bit 30 is set),
// a DMUB ring running TRANSMITTER_CONTROL on a model PHY, a DP sink (the LG's caps: HBR3 in the extended block, FEC, DSC),
// and the DAL mailbox.
//   c++ -std=c++17 -Wall -Wextra -Werror -I src/vermilion-boot -o /tmp/p0odm_test tests/kext/p0odm_test.cpp && /tmp/p0odm_test
#include "PhxP0Odm.h"
#include "phx_p0odm_1440p240.inc"
#include "phx_p0odm_1440p60_to.inc"
#include "phx_p0odm_1440p60_from.inc"
#include "phx_p0odm_1440p120_to.inc"
#include "phx_p0odm_1440p120_from.inc"
#include "phx_p0odm_1440p240_to.inc"
#include "phx_p0odm_1440p240_from.inc"
#include "phx_p0odm_1080p60_to.inc"
#include "phx_p0odm_1080p60_from.inc"
#include "phx_p0odm_1080p120_to.inc"
#include "phx_p0odm_1080p120_from.inc"
#include "phx_p0odm_1024p75_to.inc"
#include "phx_p0odm_1024p75_from.inc"
#include "phx_p0odm_768p60_to.inc"
#include "phx_p0odm_768p60_from.inc"
#include "phx_p0odm_600p60_to.inc"
#include "phx_p0odm_600p60_from.inc"
#include "phx_p0odm_480p60_to.inc"
#include "phx_p0odm_480p60_from.inc"
#include "phx_p0odm_480p59_to.inc"
#include "phx_p0odm_480p59_from.inc"
// capture v4 (2026-11-21): Linux's SDR 144 hub -> HDR at each 1440p rate and back (two mode sets each, b145)
#include "phx_p0odm_1440p60hdr_to.inc"
#include "phx_p0odm_1440p60hdr_from.inc"
#include "phx_p0odm_1440p120hdr_to.inc"
#include "phx_p0odm_1440p120hdr_from.inc"
#include "phx_p0odm_1440p144hdr_to.inc"
#include "phx_p0odm_1440p144hdr_from.inc"
#include "phx_p0odm_1440p240hdr_to.inc"
#include "phx_p0odm_1440p240hdr_from.inc"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <vector>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define N(a) (sizeof a / sizeof a[0])

using namespace phxodm;
#define PL(sym, hz, v, rate, lanes) {sym, N(sym), #sym, hz, v, rate, lanes}
#define BK(sym) PL(sym, 144, 0x5eb, 0x14, 4)
static const Plan kEntry = PL(kPhxP0odm_1440p240, 240, 0x652, 0x1e, 4);
static const Plan k60To = PL(kPhxP0odm_1440p60_to, 60, 0x5c8, 0x0a, 4), k60From = BK(kPhxP0odm_1440p60_from);
static const Plan k120To = PL(kPhxP0odm_1440p120_to, 120, 0x5f4, 0x14, 4), k120From = BK(kPhxP0odm_1440p120_from);
static const Plan k240To = PL(kPhxP0odm_1440p240_to, 240, 0x652, 0x1e, 4), k240From = BK(kPhxP0odm_1440p240_from);
static const Plan kA = PL(kPhxP0odm_1080p60_to, 60, 0x464, 0x06, 4), kAf = BK(kPhxP0odm_1080p60_from);
static const Plan kB = PL(kPhxP0odm_1080p120_to, 120, 0x464, 0x0a, 4), kBf = BK(kPhxP0odm_1080p120_from);
static const Plan kC = PL(kPhxP0odm_1024p75_to, 75, 0x429, 0x06, 4), kCf = BK(kPhxP0odm_1024p75_from);
static const Plan kD = PL(kPhxP0odm_768p60_to, 60, 0x325, 0x06, 2), kDf = BK(kPhxP0odm_768p60_from);
static const Plan kE = PL(kPhxP0odm_600p60_to, 60, 0x273, 0x06, 1), kEf = BK(kPhxP0odm_600p60_from);
static const Plan kF = PL(kPhxP0odm_480p60_to, 60, 0x20c, 0x06, 1), kFf = BK(kPhxP0odm_480p60_from);
static const Plan kG = PL(kPhxP0odm_480p59_to, 60, 0x20c, 0x06, 1), kGf = BK(kPhxP0odm_480p59_from);
static const Plan kH60 = PL(kPhxP0odm_1440p60hdr_to, 60, 0x5c8, 0x0a, 4), kH60f = BK(kPhxP0odm_1440p60hdr_from);
static const Plan kH120 = PL(kPhxP0odm_1440p120hdr_to, 120, 0x5f4, 0x14, 4), kH120f = BK(kPhxP0odm_1440p120hdr_from);
static const Plan kH144 = PL(kPhxP0odm_1440p144hdr_to, 144, 0x5eb, 0x1e, 4), kH144f = BK(kPhxP0odm_1440p144hdr_from);
static const Plan kH240 = PL(kPhxP0odm_1440p240hdr_to, 240, 0x652, 0x1e, 4), kH240f = BK(kPhxP0odm_1440p240hdr_from);
// = phx_p0odm.cpp's set
static const Mode kModes[] = {
    {60, 2560, 1440, 60, 0xa9f, 0x5c8, 0, 0x0a, 4, &k60To, &k60From}, {120, 2560, 1440, 120, 0xa9f, 0x5f4, 0, 0x14, 4, &k120To, &k120From},
    {144, 2560, 1440, 144, 0xa9f, 0x5eb, 0, 0x14, 4, nullptr, nullptr}, {240, 2560, 1440, 240, 0xa9f, 0x652, 1, 0x1e, 4, &k240To, &k240From},
    {1080060, 1920, 1080, 60, 0x897, 0x464, 0, 0x06, 4, &kA, &kAf}, {1080120, 1920, 1080, 120, 0x897, 0x464, 0, 0x0a, 4, &kB, &kBf},
    {1024075, 1280, 1024, 75, 0x697, 0x429, 0, 0x06, 4, &kC, &kCf}, {768060, 1024, 768, 60, 0x53f, 0x325, 0, 0x06, 2, &kD, &kDf},
    {600060, 800, 600, 60, 0x41f, 0x273, 0, 0x06, 1, &kE, &kEf}, {480060, 640, 480, 60, 0x31f, 0x20c, 0, 0x06, 1, &kF, &kFf},
    {480059, 640, 480, 60, 0x31f, 0x20c, 0, 0x06, 1, &kG, &kGf},
    {1440060, 2560, 1440, 60, 0xa9f, 0x5c8, 0, 0x0a, 4, &kH60, &kH60f}, {1440120, 2560, 1440, 120, 0xa9f, 0x5f4, 0, 0x14, 4, &kH120, &kH120f},
    {1440144, 2560, 1440, 144, 0xa9f, 0x5eb, 0, 0x1e, 4, &kH144, &kH144f}, {1440240, 2560, 1440, 240, 0xa9f, 0x652, 1, 0x1e, 4, &kH240, &kH240f},
};
static const Set kSet = {kModes, N(kModes), 2, &kEntry, 3};
constexpr uint64_t kFb = 0x8000000000ull, kRing = 0x1000000;

struct Hw {
    std::map<uint32_t, uint32_t> r;
    std::map<uint64_t, uint32_t> vram;
    uint64_t t = 0;
    double frac = 0;
    uint32_t frames = 0x1000;
    std::vector<std::pair<uint32_t, uint32_t>> writes;
    std::vector<std::vector<uint32_t>> cmds;
    std::vector<uint32_t> dal;
    uint8_t dpcd[0x3000] = {};
    std::vector<std::pair<uint32_t, uint8_t>> dpcdW;
    uint32_t phyRate = 54000;
    bool failHbr3 = false;
    uint32_t failRate = 0;          // link training at this symbol clock fails
    unsigned dalNak = 0;            // the next n DAL messages answer 0xfe
    uint32_t touchOutsideAt = 0;   // after this many writes, OTG2 turns on by itself (a foreign change)
    // b150: HUBPn underflow status (bits 30:28) is sticky: only HUBP_UNDERFLOW_CLEAR (bit 31) clears it (XTX machine 2026-11-22)
    uint32_t uf[2] = {0, 0};
    unsigned ufAtWrite = 0;         // HUBP0's sticky comes up at this write (an underflow during a switch)
    bool ufLive = false;            // ...and comes back after every clear (a live underflow)
    // b150: OTG0's DRR double buffer: V_TOTAL_MIN / MAX / CONTROL latch at a frame start while the mode is not 0
    uint32_t drrLatched[3] = {0, 0, 0};
    bool drrPending = false;
    // b136: DET / compressed-buffer segments (20 in all, as the box: DET 9 + 9 + compbuf 2, or DET 4 + compbuf 16). A new
    // size applies at the next frame (or at once with OTG0 off), never under the OTG0 lock; a DET gives segments back only
    // while its HUBP is powered and clocked; a grow takes what is free (the compressed buffer flags CONFIG_ERROR when short).
    uint32_t detCur[4] = {4, 0, 0, 0}, compCur = 16;
    bool compErr = false, noDetModel = false;
    bool hubpLive(unsigned n) {
        if (n == 0) return true;
        if (n == 1) return !(r[0x03542] & 0x100) && (r[0x03B90] & 1);
        return false;
    }
    void applyDet() {
        if (r[0x0504A] & 1) return;
        for (unsigned n = 0; n < 4; n++) {
            const uint32_t want = r[0x0397B + n] & 0x1f;
            if (want < detCur[n] && hubpLive(n)) detCur[n] = want;
        }
        uint32_t used = compCur; for (unsigned n = 0; n < 4; n++) used += detCur[n];
        for (unsigned n = 0; n < 4; n++) {
            const uint32_t want = r[0x0397B + n] & 0x1f;
            if (want > detCur[n] && want - detCur[n] <= 20 - used) { used += want - detCur[n]; detCur[n] = want; }
        }
        const uint32_t cw = r[0x0397A] & 0x1f;
        if (cw < compCur) compCur = cw;
        else if (cw > compCur) { const uint32_t add = std::min(cw - compCur, 20 - used); compCur += add; compErr = compCur != cw; }
    }
    bool otgOn() { return r[0x05001] & 1; }
    void advance(uint32_t us) {
        t += us;
        if (!otgOn()) { applyDet(); return; }
        const uint32_t div = r[0x0012F] & 7u;   // OTG0 field: 6 = the GOP's 1 ppc (DTO = pclk), 2 = 2 ppc (DTO = pclk / 2)
        const double pclk = (double)r[0x00141] * (div == 2 ? 2 : 1);
        const double rate = pclk / ((double)((r[0x04FEA] & 0x7fff) + 1) * ((r[0x04FEF] & 0x7fff) + 1));
        frac += us * 1e-6 * rate;
        const double whole = std::floor(frac);
        frames += static_cast<uint32_t>(whole);
        frac -= whole;
        if (whole >= 1) applyDet();
        if (whole >= 1 && drrPending) { drrLatched[0] = r[0x04FF0]; drrLatched[1] = r[0x04FF1]; drrLatched[2] = r[0x04FF3]; drrPending = false; }
    }
    bool linkUp() {
        return phyRate && phyRate == dpcd[0x100] * 2700u && ((dpcd[0x101] & 0x1f) == 4 || (dpcd[0x101] & 0x1f) == 2 || (dpcd[0x101] & 0x1f) == 1) && !(failHbr3 && phyRate == 81000) && phyRate != failRate;
    }
    uint32_t rd(uint32_t a) {
        const uint32_t v = r[a];
        for (uint32_t cfg : {0x03540u, 0x03542u, 0x03549u, 0x0354Bu, 0x0354Du, 0x0354Fu})   // DOMAIN0/1/16-19_PG_STATUS = CONFIG + 1
            if (a == cfg + 1) return (r[cfg] & 0x100) ? 0x80000000u : 0u;
        switch (a) {
        case 0x05001: return (v & ~0x10000u) | ((v & 1u) << 16);
        case 0x05045: return (v & ~0x10100u) | ((v & 1u) ? 0x100u : 0u) | (otgOn() ? 0x10000u : 0u);
        case 0x04F8F: return (v & ~0x4u) | ((v & 2u) ? 4u : 0u);
        case 0x0504A: return (v & ~0x100u) | ((v & 1u) << 8);
        case 0x03AB3: case 0x03B8F: return v | 2u | uf[a == 0x03B8F];
        case 0x0501B: return (v & ~0x10u) | (drrPending ? 0x10u : 0u);
        case 0x0500C: return frames & 0xffffff;
        case 0x0397B: case 0x0397C: case 0x0397D: case 0x0397E:
            if (noDetModel) return (v & ~0x1f00u) | ((v & 0x1f) << 8);
            return (v & ~0x1f00u) | (detCur[a - 0x0397B] << 8);
        case 0x0397A:
            if (noDetModel) return (v & ~0x80001f00u) | ((v & 0x1f) << 8);
            return (v & ~0x80001f00u) | (compCur << 8) | (compErr ? 0x80000000u : 0u);
        case 0x056CC: return (v & ~0x10000u) | ((v & 1u) && otgOn() && phyRate && (r[0x056C8] & 0x10) ? 0x10000u : 0u);
        case 0x05651: return (v & ~0x100000u) | ((v & 2u) && phyRate ? 0x100000u : 0u);   // RESET_DONE needs the symbol clock (box b131)
        case 0x056D2: return phyRate ? static_cast<uint32_t>(std::lround(0x8000 * (double)r[0x00141] / (phyRate * 10000.0))) : 0;
        default: return v;
        }
    }
    void wr(uint32_t a, uint32_t v) {
        writes.push_back({a, v});
        if (touchOutsideAt && writes.size() == touchOutsideAt) r[0x05101] = 1;
        if (ufAtWrite && writes.size() == ufAtWrite) uf[0] = 0x10000000u;
        if (a == 0x03AB3 || a == 0x03B8F) {
            if (v & 0x80000000u) uf[a == 0x03B8F] = ufLive ? 0x10000000u : 0;
            v &= ~0x70000000u;
        }
        if (a >= 0x04FF0 && a <= 0x04FF3 && a != 0x04FF2) {
            if (r[0x0501B] & 0x03000000u) drrPending = true;
            else drrLatched[a == 0x04FF0 ? 0 : a == 0x04FF1 ? 1 : 2] = v;
        }
        r[a] = v;
        if (a == phxdmub::kInbox1Wptr) {
            const uint64_t slot = kRing + r[phxdmub::kInbox1Rptr];
            std::vector<uint32_t> c(16);
            for (unsigned i = 0; i < 16; i++) c[i] = vram[slot + 4 * i];
            cmds.push_back(c);
            if (c[0] == phxdpl::kTxHdr) {
                const uint32_t act = (c[1] >> 8) & 0xff;
                if (act == phxdpl::kTxDisable) { phyRate = 0; r[0x05672] &= ~1u; }
                if (act == phxdpl::kTxEnable) { phyRate = c[2]; r[0x05672] |= 1u; }
            }
            r[phxdmub::kInbox1Rptr] = v;
        }
    }
    int auxRd(uint32_t a, uint8_t *b, unsigned n) {
        for (unsigned i = 0; i < n; i++) {
            const uint32_t x = a + i;
            uint8_t val = dpcd[x];
            const bool up = linkUp(), tps1 = (dpcd[0x102] & 0xf) == 1, eq = (dpcd[0x102] & 0xf) == 7;
            if (x == 0x202 || x == 0x203) val = up ? (tps1 ? 0x11 : 0x77) : 0x00;
            if (x == 0x204) val = up && (eq || !(dpcd[0x102] & 0xf)) ? 0x81 : 0x80;
            if (x == 0x206 || x == 0x207) val = 0;
            b[i] = val;
        }
        return 0;
    }
    int auxWr(uint32_t a, const uint8_t *b, unsigned n) {
        for (unsigned i = 0; i < n; i++) { dpcd[a + i] = b[i]; dpcdW.push_back({a + i, b[i]}); }
        return 0;
    }
};

static void gop(Hw &h) {
    const std::pair<uint32_t, uint32_t> v[] = {
        {0x0564B, 0x01000000}, {0x05651, 0x0000001d}, {0x05671, 0x20000200}, {0x05672, 0x00000101}, {0x056C8, 0x00000110},
        {0x056C9, 0x01000000}, {0x056CA, 0x20000000}, {0x056CB, 0x00000003}, {0x056CC, 0x00000201}, {0x056CD, 0x3f000000},
        {0x056D0, 0x00000100}, {0x056D1, 0x00008000}, {0x056D3, 0x10102000}, {0x056DD, 0x7fffff00}, {0x056DE, 0x0101ff10},
        {0x05001, 0x00011201}, {0x05045, 0x00010101}, {0x04FEA, 0x00000a9f}, {0x04FEF, 0x000005eb}, {0x00140, 0x00021090},
        {0x00141, 0x2363e7f0}, {0x00142, 0x2ab7772b}, {0x0012F, 0x00000db6}, {0x00124, 0x1c191c1c}, {0x09002, 0x00000000},
        {0x05081, 0x00000200}, {0x03AB3, 0x000f0000}, {0x0397B, 0x00000404}, {0x0397A, 0x00041010}, {0x04F8A, 0x00000000}, {0x04F8B, 0xfff00000},
        {0x03B8F, 0x00000000}, {0x03B90, 0x00500000}, {0x09017, 0x0000000f}, {0x03542, 0x00000100},
        // pipe 0's macOS surface / colour (what pipe 1 must copy)
        {0x03AA5, 0x00000008}, {0x03AA7, 0x00000000}, {0x03AA6, 0x00000000}, {0x03AC7, 0x09ff09ff}, {0x03ADA, 0x00000000},
        {0x03ACA, 0x00a00000}, {0x03ACB, 0x00000080}, {0x0418F, 0x00000008}, {0x04190, 0x00000001}, {0x093F1, 0x00000000},
        {phxdmub::kScratch0, 0x2}, {phxdmub::kScratch1, 0x07008748}, {phxdmub::kCntl, (1u << 16) | (1u << 20)},
        {phxdmub::kDcnVmFbBase, static_cast<uint32_t>(kFb >> 24)}, {phxdmub::kRegion4Top, 0x8000ffff},
        {phxdmub::kInbox1Base, 0x80000000}, {phxdmub::kInbox1Size, 0x1000},
        {phxdmub::kRegion4Off, static_cast<uint32_t>(kFb + kRing)}, {phxdmub::kRegion4OffHi, static_cast<uint32_t>((kFb + kRing) >> 32)},
        {phxdpb::kHpd2Status, 0x2},
    };
    for (auto &p : v) h.r[p.first] = p.second;
    const uint8_t caps[16] = {0x12, 0x14, 0xc4, 0x81, 0x01, 0x00, 0x01, 0xc0, 0x02, 0x00, 0x06, 0x00, 0x00, 0x00, 0x81, 0x00};
    const uint8_t ext[16] = {0x14, 0x1e, 0xc4, 0x81, 0x01, 0x00, 0x01, 0xc0, 0x02, 0x00, 0x06, 0x00, 0x00, 0x00, 0x81, 0x00};
    std::memcpy(h.dpcd, caps, 16);
    std::memcpy(h.dpcd + 0x2200, ext, 16);
    h.dpcd[0x60] = 0x01; h.dpcd[0x90] = 0xbf;
    h.dpcd[0x100] = 0x14; h.dpcd[0x101] = 0x84;
}
static Env env(Hw &h) {
    Env e{};
    e.io = {&h, [](void *c, uint32_t a) { return static_cast<Hw *>(c)->rd(a); }, [](void *c, uint32_t a, uint32_t v) { static_cast<Hw *>(c)->wr(a, v); },
            [](void *c, uint64_t o) { return static_cast<Hw *>(c)->vram[o]; }, [](void *c, uint64_t o, uint32_t v) { static_cast<Hw *>(c)->vram[o] = v; },
            [](void *c, uint32_t us) { static_cast<Hw *>(c)->advance(us); }};
    e.vramBytes = 24ull << 30;
    e.fbMcBase = kFb;
    e.dal = [](void *c, uint32_t m, uint32_t a) {
        Hw &h = *static_cast<Hw *>(c);
        if (h.dalNak) { h.dalNak--; return 0xfeu; }
        h.dal.push_back(m << 24 ^ a);
        return 1u;
    };
    e.pmfwIdle = [](void *) { return 1; };
    return e;
}
static phxdpb::Aux aux(Hw &h) {
    return phxdpb::Aux{&h, [](void *c, unsigned eng, uint32_t a, uint8_t *b, unsigned n) { return eng == 2 ? static_cast<Hw *>(c)->auxRd(a, b, n) : -1; },
                       [](void *c, unsigned eng, uint32_t a, const uint8_t *b, unsigned n) { return eng == 2 ? static_cast<Hw *>(c)->auxWr(a, b, n) : -1; }};
}
static bool wrote(const Hw &h, uint32_t a, uint8_t v) { for (auto &w : h.dpcdW) if (w.first == a && w.second == v) return true; return false; }

int main() {
    // ---- the plans themselves ----
    for (const Plan *p : {&kEntry, &k60To, &k60From, &k120To, &k120From, &k240To, &k240From, &kA, &kAf, &kB, &kBf, &kC, &kCf, &kD, &kDf, &kE, &kEf, &kF, &kFf, &kG, &kGf}) {
        unsigned off = 0, up = 0, bad = 0, dal = 0, waits = 0, linkups = 0;
        for (unsigned i = 0; i < p->n; i++) {
            const phx_p0odm_step &s = p->s[i];
            if ((s.op == kW || s.op == kCopy) && denied(s.reg)) bad++;
            if (s.op == kCopy && denied(s.val)) bad++;
            if (s.op == kW && (s.reg == 0x03540 || s.reg == 0x00124 || (s.reg >= 0x03600 && s.reg <= 0x036FF))) bad++;
            if (s.op == kDal) { dal++; if ((s.val >> 16) != 8 && (s.val >> 16) != 9) bad++; }
            if (s.op == kLinkOff) off = i + 1;
            if (s.op == kLinkUp) { up = i + 1; linkups++; if (s.val != p->rate || s.arg != p->lanes || !(s.mask & 1)) bad++; }
            if (s.op == kWait) waits++;
            if (s.op < kW || s.op > kSWait) bad++;
        }
        const phx_p0odm_step &last = p->s[p->n - 1];
        CHECK(!bad && off && up > off && linkups == 1 && dal >= 3 && waits >= 13);
        CHECK(last.op == kW && last.reg == 0x0504A && last.val == 0);   // ends on Linux's OTG0 unlock
        if (bad || !off || up <= off || dal < 3 || waits < 13) std::printf("  plan %s: bad %u off %u up %u dal %u waits %u\n", p->name, bad, off, up, dal, waits);
    }
    CHECK(setOk(kSet));
    // ---- survey: reads only ----
    {
        Hw h; gop(h); State st{}; Run r{};
        CHECK(run(env(h), st, r, kSelSurvey, 0, aux(h), kSet) == kSurveyed && h.writes.empty() && h.dpcdW.empty());
        CHECK(r.fecCap == 0xbf && r.dscCap == 0x01 && r.g.hz == 144 && r.where == kMac);
        // from macOS's state only the entry: 60 / 120 / 144 are the pipe-0 engine's
        for (int t : {60, 120, 144, int(kSelBack)}) CHECK(run(env(h), st, r, t, 0, aux(h), kSet) == kRefused && h.writes.empty());
        for (int t : {0, 59, 165, -1}) CHECK(run(env(h), st, r, t, 0, aux(h), kSet) == kRefused && h.writes.empty());
        CHECK(!r.failed);
    }
    // ---- entry 240, then every rate to every rate ----
    {
        Hw h; gop(h); State st{}; Run r{};
        const auto e = env(h);
        const int res = run(e, st, r, kSel240, 0, aux(h), kSet);
        CHECK(res == kDone);
        if (res != kDone) std::printf("  240: %s step %u line %u op %u reg 0x%x got 0x%x\n", r.why, r.step, r.line, r.op, r.reg, r.got);
        CHECK(r.where == kLinux && curHz(r, kSet) == 240 && odmActive(r, kSet) && !st.halted && r.postHz >= 228 && r.postHz <= 252);
        CHECK(h.r[0x04F8B] == 0x00100001 && h.r[0x04FEF] == 0x652 && h.r[0x00141] == 0x1f7e5cd8 && (h.r[0x056C9] & 0x40000000));
        CHECK(h.phyRate == 81000 && h.dpcd[0x100] == 0x1e && wrote(h, 0x120, 1) && wrote(h, 0x160, 1) && wrote(h, 0x107, 0x90));
        CHECK((h.r[0x056D7] & 0x30) == 0x30);                                     // FEC on
        CHECK(h.r[0x064C0] & 1 && h.r[0x0651C] & 1);                              // DSC0 + DSC1 clocked
        CHECK(h.r[0x03BA6] == 0x00a00000 && h.r[0x03BA7] == 0x00000080);          // HUBPREQ1 address = HUBPREQ0's
        CHECK(h.r[0x03B81] == 0x8 && h.r[0x03BA3] == 0x09ff09ff && h.r[0x03B83] == 0 && h.r[0x042FA] == 0x8);
        CHECK(h.r[0x03AA5] == 0x8 && h.r[0x03AA7] == 0 && h.r[0x03AC7] == 0x09ff09ff && h.r[0x03ACA] == 0x00a00000);
        CHECK(h.r[0x03540] == 0);                                                  // HUBP0 never power-gated
        CHECK(h.r[0x05101] == 0 && h.r[0x05081] == 0x200);                        // OTG2 / OTG1 untouched
        bool en3 = false; for (auto &c : h.cmds) en3 |= c[0] == phxdpl::kTxHdr && c[1] == 0x04000101 && c[2] == 81000;
        CHECK(en3);
        bool d1075 = false, d537 = false;
        for (auto x : h.dal) { d1075 |= x == (9u << 24 ^ 0x00080433); d537 |= x == (9u << 24 ^ 0x00090219); }
        CHECK(d1075 && d537 && h.dal.size() == 6);
        // the same rate again: nothing written
        size_t nw = h.writes.size();
        CHECK(run(e, st, r, 240, 0, aux(h), kSet) == kSame && h.writes.size() == nw);
        // every ordered pair of modes (every mode to every other, then a second round in another order)
        std::vector<uint32_t> order;
        for (unsigned i = 0; i < N(kModes); i++) for (unsigned j = 0; j < N(kModes); j++) if (i != j) { order.push_back(kModes[i].key); order.push_back(kModes[j].key); }
        for (unsigned i = N(kModes); i-- > 0;) order.push_back(kModes[i].key);
        unsigned pairs = 0;
        for (uint32_t t : order) {
            const uint32_t from = curKey(r, kSet);
            if (from == t) continue;
            pairs++;
            nw = h.writes.size();
            const int rr = run(e, st, r, static_cast<int>(t), 0, aux(h), kSet);
            const Mode &m = kModes[findMode(kSet, t)];
            CHECK(rr == kDone && curKey(r, kSet) == t && h.r[0x04FEF] == m.vTotal && h.r[0x04FEA] == m.hTotal && h.writes.size() > nw);
            CHECK(h.phyRate == m.rate * 2700u && (h.dpcd[0x101] & 0x1f) == m.lanes);   // Linux's link for the mode
            CHECK(r.postHz >= m.hz * 95 / 100 - 4 && r.postHz <= m.hz * 105 / 100 + 4 && (h.r[0x056D7] & 0x30) == 0x30);
            CHECK(odmActive(r, kSet) == !!m.odm && h.r[0x04F8B] == (m.odm ? 0x00100001u : 0xfff00000u));   // b145: 240 SDR and HDR
            CHECK(!!(h.r[0x064C0] & 1) == !!m.odm);                                // DSC0 only at 240 (SDR and HDR)
            CHECK(r.plan1 == (from == 144 ? nullptr : kModes[findMode(kSet, from)].from->name));
            CHECK(r.plan2 == (t == 144 ? nullptr : m.to->name));
            if (rr != kDone) std::printf("  %u -> %u: %s phase %d step %u line %u op %u reg 0x%x got 0x%x det 0x%x 0x%x comp 0x%x sw_to %u hz %u v 0x%x\n", from, t, r.why, r.phase, r.step, r.line, r.op, r.reg, r.got, r.postDet0, r.postDet1, r.postCompbuf, r.swaitTimeouts, r.postHz, r.postV);
            CHECK(h.r[0x03540] == 0 && h.r[0x05101] == 0 && h.r[0x05081] == 0x200);
            // b136: the buffer segments landed (box b135: DET1 kept 9 after 240 -> 144, CONFIG_ERROR, pipe 0 dark)
            CHECK(!h.compErr && h.detCur[0] == (h.r[0x0397B] & 0x1f) && h.detCur[1] == (h.r[0x0397C] & 0x1f) && h.detCur[0] && r.swaitTimeouts == 0);
            CHECK(h.detCur[1] == (m.odm ? 9u : 0u));
            // b138: pipe 0's pitch is Linux's for the mode (the width rounded up to 256 pixels)
            CHECK((h.r[0x03AC7] & 0x3fff) + 1 == ((m.w + 255u) & ~255u));
        }
        CHECK(pairs > 100);
        // b145 HDR: an HDR mode ends at 10 bpc with the PQ mastering infoframe (DP_PIXEL_FORMAT, FMT, DP_SEC_CNTL bit 23),
        // the SDR hub after it at 8 bpc again
        CHECK(run(e, st, r, 1440240, 0, aux(h), kSet) == kDone && h.r[0x056C9] == 0x42000000u && h.r[0x04D01] == 0xd100u && (h.r[0x056EB] & 0x800000u));
        CHECK(run(e, st, r, 144, 0, aux(h), kSet) == kDone && h.r[0x056C9] == 0x41000000u && h.r[0x04D01] == 0xc900u);
        CHECK(run(e, st, r, 1440060, 0, aux(h), kSet) == kDone && h.r[0x056C9] == 0x42000000u && h.phyRate == 0x0a * 2700u);
        CHECK(run(e, st, r, 60, 0, aux(h), kSet) == kDone && h.r[0x056C9] == 0x41000000u);
        pairs += 4;
        for (auto &wr : h.writes) CHECK(!denied(wr.first) || wr.first == phxdmub::kInbox1Wptr);
        CHECK(!r.failed && r.fellBacks == 0 && r.dones == 1 + pairs);
        char b[2048];
        CHECK(stat(r, kSet, b, sizeof b) > 100 && std::strstr(b, "result=DONE") && std::strstr(b, "where=linux now=2560x1440@60"));
    }
    // ---- HBR3 fails: the entry falls back to 144, not latched; 60 still works; 240 fails twice, then refused ----
    {
        Hw h; gop(h); h.failHbr3 = true; State st{}; Run r{};
        const auto e = env(h);
        CHECK(run(e, st, r, kSel240, 0, aux(h), kSet) == kFellBack);
        CHECK(!r.failed && r.where == kLinux && curHz(r, kSet) == 144 && r.backRes == 1 && h.phyRate == 54000 && h.r[0x04FEF] == 0x5eb);
        CHECK(r.op == kLinkUp && r.last.plan && std::strcmp(r.last.plan, "kPhxP0odm_1440p240") == 0 && r.fbPlan == k240From.name);
        CHECK(run(e, st, r, 60, 0, aux(h), kSet) == kDone && curHz(r, kSet) == 60 && h.r[0x04FEF] == 0x5c8);
        CHECK(run(e, st, r, 240, 0, aux(h), kSet) == kFellBack && curHz(r, kSet) == 144 && r.fbCount[3] == 2);
        const size_t nw = h.writes.size();
        CHECK(run(e, st, r, 240, 0, aux(h), kSet) == kRefused && h.writes.size() == nw);   // twice: not again this boot
        CHECK(run(e, st, r, 120, 0, aux(h), kSet) == kDone && curHz(r, kSet) == 120);       // the others still run
    }
    // ---- a DAL refusal at the first step (240 -> 60: the from-240 plan starts with DISPCLK 614): a plain refusal, nothing
    //      written, not latched (b134 latched it); the next try runs. A DAL refusal after writes: the fall-back. ----
    {
        Hw h; gop(h); State st{}; Run r{};
        const auto e = env(h);
        CHECK(k240From.s[0].op == kDal && k240To.s[0].op == kW);
        CHECK(run(e, st, r, 240, 0, aux(h), kSet) == kDone);
        const size_t nw = h.writes.size(), nd = h.dpcdW.size();
        h.dalNak = 1;
        CHECK(run(e, st, r, 60, 0, aux(h), kSet) == kRefused && h.writes.size() == nw && h.dpcdW.size() == nd && !r.failed);
        CHECK(curHz(r, kSet) == 240 && r.last.dalResp == 0xfe && r.last.why && std::strstr(r.last.why, "SetHardMinByFreq"));
        CHECK(r.last.plan == k240From.name && r.last.step == 0);
        CHECK(run(e, st, r, 60, 0, aux(h), kSet) == kDone && curHz(r, kSet) == 60);
        CHECK(run(e, st, r, 144, 0, aux(h), kSet) == kDone);
        h.dalNak = 1;   // hub -> 240: the to plan writes first, so its DAL refusal falls back to 144
        CHECK(run(e, st, r, 240, 0, aux(h), kSet) == kFellBack && !r.failed && curHz(r, kSet) == 144 && r.fbCount[3] == 1);
        CHECK(run(e, st, r, 240, 0, aux(h), kSet) == kDone && curHz(r, kSet) == 240);
    }
    // ---- a from plan that fails (HBR2 training at 60 -> 144): latched ----
    {
        Hw h; gop(h); State st{}; Run r{};
        const auto e = env(h);
        CHECK(run(e, st, r, 240, 0, aux(h), kSet) == kDone && run(e, st, r, 60, 0, aux(h), kSet) == kDone);
        h.failRate = 54000;
        CHECK(run(e, st, r, 120, 0, aux(h), kSet) == kFailed && r.failed && r.phase == 1);
        CHECK(r.last.plan == k60From.name && r.last.op == kLinkUp);
        const size_t nw = h.writes.size();
        CHECK(run(e, st, r, 144, 0, aux(h), kSet) == kRefused && h.writes.size() == nw);
        CHECK(run(e, st, r, kSelSurvey, 0, aux(h), kSet) != kDone && h.writes.size() == nw);
    }
    // ---- linux state changed under the engine: refused, nothing written ----
    {
        Hw h; gop(h); State st{}; Run r{};
        const auto e = env(h);
        CHECK(run(e, st, r, 240, 0, aux(h), kSet) == kDone && run(e, st, r, 120, 0, aux(h), kSet) == kDone);
        h.r[0x04FEF] = 0x5eb;
        const size_t nw = h.writes.size();
        CHECK(run(e, st, r, 60, 0, aux(h), kSet) == kRefused && h.writes.size() == nw && !r.failed);
        h.r[0x04FEF] = 0x5f4; h.r[0x05081] = 0x201;   // OTG1 lit
        CHECK(run(e, st, r, 60, 0, aux(h), kSet) == kRefused && h.writes.size() == nw);
        h.r[0x05081] = 0x200;
        CHECK(run(e, st, r, 60, 0, aux(h), kSet) == kDone);
    }
    // ---- refusals from macOS's state: zero writes ----
    {
        struct Case { const char *what; void (*f)(Hw &); } cs[] = {
            {"OTG1 on", [](Hw &h) { h.r[0x05081] = 0x201; }},
            {"pipe 1 in use", [](Hw &h) { h.r[0x03B90] = 0x00500001; h.r[0x03B8F] = 0; }},
            {"MPCC1 connected", [](Hw &h) { h.r[0x09017] = 1; }},
            {"DSC on", [](Hw &h) { h.r[0x064C0] = 1; }},
            {"no FEC", [](Hw &h) { h.dpcd[0x90] = 0; }},
            {"no DSC", [](Hw &h) { h.dpcd[0x60] = 0; }},
            {"no HBR3", [](Hw &h) { h.dpcd[0xe] = 0x01; }},
            {"1080p", [](Hw &h) { h.r[0x04FEA] = 0x897; }},
            {"2 ppc from macOS", [](Hw &h) { h.r[0x056C9] = 0x41000000; }},
        };
        for (auto &c : cs) {
            Hw h; gop(h); c.f(h); State st{}; Run r{};
            const int res = run(env(h), st, r, kSel240, 0, aux(h), kSet);
            CHECK(res == kRefused && h.writes.empty() && h.dpcdW.empty() && h.dal.empty() && !r.failed);
            if (res != kRefused) std::printf("  case %s: %s\n", c.what, r.why);
        }
        Hw h; gop(h); State st{}; Run r{};
        Env e = env(h); e.dal = nullptr;
        CHECK(run(e, st, r, kSel240, 0, aux(h), kSet) == kRefused && h.writes.empty());
    }
    // ---- a foreign change outside pipes 0 / 1 during the run: halted ----
    {
        Hw h; gop(h); h.touchOutsideAt = 20; State st{}; Run r{};
        CHECK(run(env(h), st, r, kSel240, 0, aux(h), kSet) == kHalted && st.halted && r.pipeWhy && r.failed);
        CHECK(h.writes.size() == 20);   // stopped at the write after which the change was seen (no further write)
    }
    {   // Adaptive Sync: Linux's DRR values (capture v3 from-vrr-143.99: max 0x11c3, min 0x5eb) and the CONTROL fields
        using namespace phxodm;
        const Mode m144{144, 2560, 1440, 144, 0xa9f, 0x5eb, 0, 0x14, 4, nullptr, nullptr};
        const Mode m240{240, 2560, 1440, 240, 0xa9f, 0x652, 1, 0x1e, 4, nullptr, nullptr};
        const Mode m60{60, 2560, 1440, 60, 0xa9f, 0x5c8, 0, 0x0a, 4, nullptr, nullptr};
        VrrRegs v{};
        CHECK(vrrRegs(m144, 48, 0, &v) && v.vmax == 0x11c3 && v.vmin == 0x5eb && v.ctl == 0x00020003u);
        CHECK(vrrRegs(m240, 48, 0, &v) && v.vmax == 1619u * 240 / 48 - 1 && v.vmin == 0x652);
        CHECK(vrrRegs(m60, 48, 0, &v) && v.vmax == 1481u * 60 / 48 - 1 && v.vmin == 0x5c8);
        // b146: the owner's floor (debug.phxfb0_vrrmin, default 60)
        CHECK(vrrRegs(m240, 60, 0, &v) && v.vmax == 6475u && v.vmin == 0x652);
        CHECK(vrrRegs(m144, 60, 0, &v) && v.vmax == 0x5ecu * 144 / 60 - 1);
        CHECK(!vrrRegs(m60, 60, 0, &v));   // a 60 Hz mode has no range at a 60 Hz floor
        CHECK(vrrRegs(m144, 48, 0xffffffffu, &v) && v.ctl == ((0xffffffffu & ~(1u | 2u | 0x10u | 0xffff0000u)) | 0x00020003u));   // other fields kept
        CHECK(!vrrRegs(m144, 0, 0, &v) && !vrrRegs(m144, 144, 0, &v) && !vrrRegs(m144, 200, 0, &v));
        const Mode mz{1, 1, 1, 60, 1, 0, 0, 0, 1, nullptr, nullptr};
        CHECK(!vrrRegs(mz, 48, 0, &v));
    }
    // ---- b150: the DRR double buffer around the range, and the underflow stickies ----
    {
        using namespace phxodm;
        Hw h; gop(h); Env e = env(h);
        VrrRegs v{0x5eb, 0xe35, 0x00020003u};
        uint32_t db = 0xffffffffu;
        const size_t w0 = h.writes.size();
        vrrWriteOn(e, v, &db);
        CHECK(db == 0 && h.writes.size() == w0 + 4);
        CHECK(h.writes[w0] == std::make_pair(0x0501Bu, 0x02000000u));   // Linux's mode first: the range itself is latched at a frame start
        CHECK(h.writes[w0 + 1].first == 0x04FF1 && h.writes[w0 + 2].first == 0x04FF0 && h.writes[w0 + 3].first == 0x04FF3);   // MAX, MIN, CONTROL
        CHECK(h.drrPending && h.drrLatched[1] == 0);
        h.advance(20000);
        CHECK(!h.drrPending && h.drrLatched[0] == 0x5eb && h.drrLatched[1] == 0xe35 && h.drrLatched[2] == 0x00020003u);
        vrrWriteOn(e, v, nullptr);
        CHECK(h.writes.size() == w0 + 7);   // already mode 2: not written again
        const uint32_t boot[3] = {0, 0, 0};
        bool to = true;
        const uint32_t f0 = h.frames;
        const uint32_t us = vrrWriteOff(e, boot, db, &to);
        CHECK(!to && us > 0 && us <= 100000 && h.frames > f0);                 // waited for a frame start
        CHECK(!h.drrPending && h.drrLatched[1] == 0 && h.drrLatched[2] == 0);  // the boot values were latched before...
        CHECK(h.writes.back() == std::make_pair(0x0501Bu, 0u) && h.r[0x0501B] == 0);   // ...the boot mode came back
        CHECK(h.r[0x04FF0] == 0 && h.r[0x04FF1] == 0 && h.r[0x04FF3] == 0);
        // never double buffered (a b149 state): off writes at once, no wait, no mode write
        const size_t w1 = h.writes.size();
        CHECK(vrrWriteOff(e, boot, 0, &to) == 0 && !to && h.writes.size() == w1 + 3);
        // the OTG stopped: no frame start, the wait gives up after 100 ms and the boot mode still comes back
        vrrWriteOn(e, v, nullptr);
        h.r[0x05001] &= ~1u;
        CHECK(vrrWriteOff(e, boot, 0, &to) == 100000 && to && !(h.r[0x0501B] & 0x03000000u));   // (the read-only pending bit is written back, as Linux's REG_UPDATE does)
        CHECK(dbufWith(0x00000100u, 0x02000000u) == 0x02000100u && dbufWith(0x03000111u, 0) == 0x00000111u);   // other fields kept
    }
    {   // an underflow sticky from before the run (as Variable, then HDR, left one on the XTX machine): cleared, the run lands
        using namespace phxodm;
        Hw h; gop(h); State st{}; Run r{}; Env e = env(h);
        CHECK(run(e, st, r, 240, 0, aux(h), kSet) == kDone && r.staleUf == 0 && r.postRetries == 0);
        h.uf[0] = 0x10000000u;
        CHECK(run(e, st, r, 1440240, 0, aux(h), kSet) == kDone && r.staleUf == 1 && r.postRetries == 0 && !r.failed && curKey(r, kSet) == 1440240);
        CHECK(h.uf[0] == 0);
        h.uf[0] = 0x10000000u; h.uf[1] = 0x40000000u;
        CHECK(run(e, st, r, 240, 0, aux(h), kSet) == kDone && r.staleUf == 3 && h.uf[0] == 0 && h.uf[1] == 0);
    }
    {   // a sticky the switch raised itself: cleared once, the check again, landed
        using namespace phxodm;
        Hw h; gop(h); State st{}; Run r{}; Env e = env(h);
        CHECK(run(e, st, r, 240, 0, aux(h), kSet) == kDone);
        h.ufAtWrite = static_cast<unsigned>(h.writes.size()) + 40;
        CHECK(run(e, st, r, 144, 0, aux(h), kSet) == kDone && r.postRetries == 1 && r.staleUf == 0 && !r.failed && curHz(r, kSet) == 144);
        CHECK(h.uf[0] == 0);
    }
    {   // a live underflow comes back after the clear: the run fails as before
        using namespace phxodm;
        Hw h; gop(h); State st{}; Run r{}; Env e = env(h);
        CHECK(run(e, st, r, 240, 0, aux(h), kSet) == kDone);
        h.ufAtWrite = static_cast<unsigned>(h.writes.size()) + 40; h.ufLive = true;
        CHECK(run(e, st, r, 144, 0, aux(h), kSet) == kFailed && r.postRetries == 1 && r.failed && (r.last.hubp0 & 0x70000000u));
        CHECK(run(e, st, r, 240, 0, aux(h), kSet) == kRefused);
    }
    std::printf("p0odm_test: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
