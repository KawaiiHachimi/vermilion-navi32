// SPDX-License-Identifier: MIT
// dpb_retrain_test.cpp: host test of the b129 DP B retrain (src/vermilion-boot/PhxDpbRetrain.h) against a model of the XTX
// machine's pipe 0 at 1440p144 (the macOS register read of 2026-11-20), a DMUB ring that consumes commands and runs
// TRANSMITTER_CONTROL on a model PHY (DISABLE also clears DIG1 BE_EN bit 0, the case the guard must allow), and a DP sink
// model (the LG's DPCD caps from the Linux capture: base HBR2, extended HBR3, TPS4) that trains when the PHY runs at the
// rate DPCD 0x100 asks for.
//   c++ -std=c++17 -Wall -Wextra -Werror -I src/vermilion-boot -o /tmp/dpb_retrain_test tests/kext/dpb_retrain_test.cpp && /tmp/dpb_retrain_test
#include "PhxDpbRetrain.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

using namespace phxdpb;
constexpr uint64_t kFb = 0x8000000000ull, kRing = 0x1000000;

struct Hw {
    std::map<uint32_t, uint32_t> r;
    std::map<uint64_t, uint32_t> vram;
    uint64_t t = 0;
    double frac = 0;
    uint32_t frames = 0x1000;
    std::vector<std::pair<uint32_t, uint32_t>> writes;
    std::vector<std::vector<uint32_t>> cmds;
    uint8_t dpcd[0x3000] = {};
    std::vector<std::pair<uint32_t, uint8_t>> dpcdW;
    uint32_t phyRate = 54000;    // symclk in 10 kHz the PHY runs (0 = off); the GOP's HBR2
    bool dmubDead = false, failHbr3 = false, failAll = false;
    uint32_t changeOnWrite = 0;  // 1 = OTG0 H_TOTAL changes at the first DP1 write (a foreign change: must halt)
    bool otgOn() { return r[0x05001] & 1; }
    void advance(uint32_t us) {
        t += us;
        if (!otgOn()) return;
        const double rate = r[0x00141] / ((double)((r[0x04FEA] & 0x7fff) + 1) * ((r[0x04FEF] & 0x7fff) + 1));
        frac += us * 1e-6 * rate;
        const double whole = std::floor(frac);
        frames += static_cast<uint32_t>(whole);
        frac -= whole;
    }
    bool linkUp() {   // the sink locked: PHY on at the rate the sink was configured for, 4 lanes
        return phyRate && phyRate == dpcd[0x100] * 2700u && (dpcd[0x101] & 0x1f) == 4 && !failAll && !(failHbr3 && phyRate == 81000);
    }
    uint32_t rd(uint32_t a) {
        const uint32_t v = r[a];
        switch (a) {
        case 0x05001: return (v & ~0x10000u) | ((v & 1u) << 16);
        case 0x0500C: return frames & 0xffffff;
        case 0x056CC: return (v & ~0x10000u) | ((v & 1u) && otgOn() && phyRate && (r[0x056C8] & 0x10) ? 0x10000u : 0u);
        case 0x05651: return (v & ~0x100000u) | ((v & 2u) << 19);
        case 0x056D2: return phyRate ? static_cast<uint32_t>(std::lround(0x8000 * (double)r[0x00141] / (phyRate * 10000.0))) : 0;
        default: return v;
        }
    }
    void wr(uint32_t a, uint32_t v) {
        writes.push_back({a, v});
        if (changeOnWrite && a >= 0x056C8 && a <= 0x05750) { r[0x04FEA] = 0x897; changeOnWrite = 0; }
        r[a] = v;
        if (a == phxdmub::kInbox1Wptr && !dmubDead) {   // the firmware consumes the slot and runs it
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
            uint32_t x = a + i;
            uint8_t val = dpcd[x];
            const bool up = linkUp(), tps1 = (dpcd[0x102] & 0xf) == 1, eq = (dpcd[0x102] & 0xf) == 7 || (dpcd[0x102] & 0xf) == 3;
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

// The XTX machine at 1440p144 after b128 (macOS read 2026-11-20), the LG's DPCD (Linux capture 2026-11-20).
static void gop(Hw &h) {
    const std::pair<uint32_t, uint32_t> v[] = {
        {0x0564B, 0x01000000}, {0x05651, 0x0000001d}, {0x05671, 0x20000200}, {0x05672, 0x00000101}, {0x056C8, 0x00000110},
        {0x056C9, 0x01000000}, {0x056CA, 0x20000000}, {0x056CB, 0x00000003}, {0x056CC, 0x00000201}, {0x056CD, 0x3f000000},
        {0x056D0, 0x00000100}, {0x056D1, 0x00008000}, {0x056D3, 0x10102000}, {0x056D8, 0x00000002}, {0x056DD, 0x7fffff00},
        {0x056DE, 0x0101ff10}, {0x05001, 0x00011201}, {0x04FEA, 0x00000a9f}, {0x04FEF, 0x000005eb}, {0x00140, 0x00021090},
        {0x00141, 0x2363e7f0}, {0x00142, 0x2ab7772b}, {0x0012F, 0x00000db6}, {0x00124, 0x1c191c1c}, {0x09002, 0x00000000},
        {0x05081, 0x00000200}, {0x03AB3, 0x000f0002}, {0x04F8A, 0x00000000},
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
    h.dpcd[0x100] = 0x14; h.dpcd[0x101] = 0x84; h.dpcd[0x102] = 0;
}

static Hw *gH;
static Env env(Hw &h) {
    gH = &h;
    Env e{};
    e.io = {&h, [](void *c, uint32_t a) { return static_cast<Hw *>(c)->rd(a); }, [](void *c, uint32_t a, uint32_t v) { static_cast<Hw *>(c)->wr(a, v); },
            [](void *c, uint64_t o) { return static_cast<Hw *>(c)->vram[o]; }, [](void *c, uint64_t o, uint32_t v) { static_cast<Hw *>(c)->vram[o] = v; },
            [](void *c, uint32_t us) { static_cast<Hw *>(c)->advance(us); }};
    e.vramBytes = 24ull << 30;
    e.fbMcBase = kFb;
    return e;
}
static Aux aux(Hw &h) {
    return Aux{&h, [](void *c, unsigned eng, uint32_t a, uint8_t *b, unsigned n) { return eng == 2 ? static_cast<Hw *>(c)->auxRd(a, b, n) : -1; },
               [](void *c, unsigned eng, uint32_t a, const uint8_t *b, unsigned n) { return eng == 2 ? static_cast<Hw *>(c)->auxWr(a, b, n) : -1; }};
}
static unsigned dispWrites(const Hw &h) {   // display register writes (not the ring pointer)
    unsigned n = 0;
    for (auto &w : h.writes) n += w.first != phxdmub::kInbox1Wptr;
    return n;
}
static bool allWritesOk(const Hw &h) {
    for (auto &w : h.writes) if (w.first != phxdmub::kInbox1Wptr && !writeOk(w.first)) return false;
    return true;
}

int main() {
    // ---- the trainer's lists: DP A / DP E unchanged, DP B only through kConnDpB ----
    {
        uint32_t c[16];
        phxdpl::txCmd(c, phxdpl::kConnDpB, phxdpl::kTxEnable, 4, phxdpl::symclk10k(phxdpl::kHbr3), 0);
        CHECK(c[0] == 0x3c000180 && c[1] == 0x04000101 && c[2] == 81000 && c[3] == 0xf4000003);   // = Linux 7.2, 144 -> 240
        CHECK(phxdpl::txAllowed(c, phxdpl::kConnDpB, nullptr));
        phxdpl::txCmd(c, phxdpl::kConnDpB, phxdpl::kTxDisable, 0, 0, 0);
        CHECK(c[1] == 0x00000001 && c[2] == 0 && c[3] == 0xf4130003 && phxdpl::txAllowed(c, phxdpl::kConnDpB, nullptr));
        for (uint8_t ls = 0; ls < 3; ls++) {
            phxdpl::txCmd(c, phxdpl::kConnDpB, phxdpl::kTxVsemph, 4, 81000, ls);
            CHECK(c[1] == (0x04000b01u | (uint32_t)ls << 16) && c[3] == 0xf6130003 && phxdpl::txAllowed(c, phxdpl::kConnDpB, nullptr));
        }
        phxdpl::txCmd(c, phxdpl::kConns[1], phxdpl::kTxEnable, 4, 81000, 0);
        CHECK(!phxdpl::txAllowed(c, phxdpl::kConns[1], nullptr));            // DP E: no HBR3
        phxdpl::txCmd(c, phxdpl::kConnDpB, phxdpl::kTxEnable, 4, 54000, 0);
        CHECK(!phxdpl::txAllowed(c, phxdpl::kConns[0], nullptr));            // PHY B never with DP A's connector
        CHECK(!phxdpl::connByDig(1));                                          // the multiport probes still refuse DP B
        CHECK(phxdpl::txAllowed(c, phxdpl::kConnDpB, nullptr));
    }
    // ---- survey: reads only ----
    {
        Hw h; gop(h); State st{}; Run r{};
        CHECK(run(env(h), st, r, kSelSurvey, 0, aux(h)) == kSurveyed);
        CHECK(h.writes.empty() && h.dpcdW.empty() && h.cmds.empty());
        CHECK(r.hz == 144 && r.pclkKhz == 593750 && r.bpp == 24 && r.extUsed == 1 && r.link0[0] == 0x14);
    }
    // ---- D1: HBR2 x4 retrain at 144 ----
    {
        Hw h; gop(h); State st{}; Run r{};
        const uint32_t f0 = h.frames;
        CHECK(run(env(h), st, r, kSelHbr2, 0, aux(h)) == kPass);
        CHECK(!st.halted && r.pipe0Same && allWritesOk(h) && !r.strayWr && !r.dpcdStray);
        CHECK(h.cmds.size() >= 4);
        if (h.cmds.size() >= 4) {
            CHECK(h.cmds[0][0] == kPingHdr);
            CHECK(h.cmds[1][0] == phxdpl::kTxHdr && h.cmds[1][1] == 0x00000001 && h.cmds[1][3] == 0xf4130003);   // DISABLE
            CHECK(h.cmds[2][1] == 0x04000101 && h.cmds[2][2] == 54000 && h.cmds[2][3] == 0xf4000003);           // ENABLE HBR2
        }
        CHECK(h.phyRate == 54000 && h.dpcd[0x100] == 0x14 && h.dpcd[0x101] == 0x84 && h.dpcd[0x600] == 1);
        CHECK((h.rd(0x056CC) & 0x10000) && (h.r[0x05672] & 1) && h.r[0x05671] == 0x20000200 && h.r[0x056CC] == 0x201);
        CHECK(h.r[0x056D2] == phxdpl::vidM1(593750, 0x14) && h.r[0x056D0] == 0x100 && h.r[0x056CD] == 0x3f000000);
        CHECK(h.r[0x05651] == 0x1d && h.r[0x056CB] == 3 && h.r[0x056DE] == 0x0101ff10 && h.r[0x056D3] == 0x10102000);
        CHECK(h.r[0x04FEA] == 0xa9f && h.r[0x00141] == 0x2363e7f0 && h.frames != f0);
        bool d3 = false; for (auto &w : h.dpcdW) d3 |= w.first == 0x600 && w.second == 2;
        CHECK(d3);                                                              // D3 before the transmitter DISABLE (Linux)
        CHECK(r.offUs != ~0u && r.onUs != ~0u && r.passes == 1);
    }
    // ---- D2: HBR3 x4 (extended caps, TPS4) ----
    {
        Hw h; gop(h); State st{}; Run r{};
        CHECK(run(env(h), st, r, kSelHbr3, 0, aux(h)) == kPass);
        CHECK(h.phyRate == 81000 && h.dpcd[0x100] == 0x1e && r.rep.pattern == 0x07);
        bool en = false; for (auto &c : h.cmds) en |= c[1] == 0x04000101 && c[2] == 81000;
        CHECK(en);
        CHECK(h.r[0x056D2] == phxdpl::vidM1(593750, 0x1e) && (h.rd(0x056CC) & 0x10000) && allWritesOk(h));
        // and back to HBR2 in the same boot
        Hw &g = h;
        CHECK(run(env(g), st, r, kSelHbr2, 0, aux(g)) == kPass && g.phyRate == 54000 && g.dpcd[0x100] == 0x14 && r.passes == 2);
    }
    // ---- HBR3 fails at the sink: falls back to HBR2, stream back ----
    {
        Hw h; gop(h); h.failHbr3 = true; State st{}; Run r{};
        CHECK(run(env(h), st, r, kSelHbr3, 0, aux(h)) == kFellBack);
        CHECK(h.phyRate == 54000 && h.dpcd[0x100] == 0x14 && (h.rd(0x056CC) & 0x10000) && !r.failed && r.done.rate == 0x14);
        CHECK(r.trc == phxdpl::kErrCr && r.trc2 == phxdpl::kOk);
    }
    // ---- both fail: TRAIN-FAIL, link off, latched (survey still reads) ----
    {
        Hw h; gop(h); h.failAll = true; State st{}; Run r{};
        CHECK(run(env(h), st, r, kSelHbr2, 0, aux(h)) == kTrainFail && r.failed && h.phyRate == 0);
        const size_t n = h.writes.size();
        CHECK(run(env(h), st, r, kSelHbr2, 0, aux(h)) == kRefused && h.writes.size() == n);
    }
    // ---- refusals: zero writes, zero DPCD writes, zero commands ----
    {
        struct Case { const char *what; void (*f)(Hw &); } cs[] = {
            {"OTG1 on", [](Hw &h) { h.r[0x05081] = 0x201; }},
            {"other DMCUB", [](Hw &h) { h.r[phxdmub::kScratch1] = 0x07003300; }},
            {"BE_CNTL", [](Hw &h) { h.r[0x05671] = 0x20000100; }},
            {"2 ppc", [](Hw &h) { h.r[0x056C9] = 0x41000000; }},
            {"stream off", [](Hw &h) { h.r[0x056CC] = 0x200; }},
            {"1080p", [](Hw &h) { h.r[0x04FEA] = 0x897; }},
            {"no HPD", [](Hw &h) { h.r[phxdpb::kHpd2Status] = 0; }},
            {"ring busy", [](Hw &h) { h.r[phxdmub::kInbox1Wptr] = 0x40; }},
            {"HUBP0 underflow", [](Hw &h) { h.r[0x03AB3] |= 0x10000000; }},
            {"no ext caps", [](Hw &h) { h.dpcd[0xe] = 0x01; }},   // base caps say HBR2: HBR3 refused
        };
        for (auto &c : cs) {
            Hw h; gop(h); c.f(h); State st{}; Run r{};
            const int res = run(env(h), st, r, kSelHbr3, 0, aux(h));
            CHECK(res == kRefused);
            CHECK(h.writes.empty() && h.dpcdW.empty() && h.cmds.empty());
            if (res != kRefused) std::printf("  case %s: %s\n", c.what, r.why);
        }
        Hw h; gop(h); State st{}; st.halted = 1; Run r{};
        CHECK(run(env(h), st, r, kSelHbr2, 0, aux(h)) == kRefused && h.writes.empty());
        Hw h2; gop(h2); State st2{}; Run r2{};
        CHECK(run(env(h2), st2, r2, 7, 0, aux(h2)) == kRefused && h2.writes.empty());
        CHECK(run(env(h2), st2, r2, kSelHbr2, 1, aux(h2)) == kRefused && h2.writes.empty());   // after a DMUB restart
    }
    // ---- the XT build's firmware passes the gate too ----
    {
        Hw h; gop(h); h.r[phxdmub::kScratch1] = 0x07008742; State st{}; Run r{};
        CHECK(run(env(h), st, r, kSelSurvey, 0, aux(h)) == kSurveyed);
    }
    // ---- the ping times out: dead latch, the picture untouched ----
    {
        Hw h; gop(h); h.dmubDead = true; State st{}; Run r{};
        CHECK(run(env(h), st, r, kSelHbr2, 0, aux(h)) == kTimeout);
        CHECK(st.dmub.dead && dispWrites(h) == 0 && h.dpcdW.empty() && (h.rd(0x056CC) & 0x10000));
    }
    // ---- a foreign pipe-0 change during the run: halted, nothing more ----
    {
        Hw h; gop(h); h.changeOnWrite = 1; State st{}; Run r{};
        CHECK(run(env(h), st, r, kSelHbr2, 0, aux(h)) == kHalted);
        CHECK(st.halted && !r.pipe0Same && dispWrites(h) == 1 && h.cmds.size() == 1);   // the ping, one write, then stop
    }
    // ---- 60 Hz at the GOP's 12 bpc ----
    {
        Hw h; gop(h); h.r[0x04FEF] = 0x5c8; h.r[0x00141] = 241500000; h.r[0x056C9] = 0x03000000; State st{}; Run r{};
        CHECK(run(env(h), st, r, kSelHbr2, 0, aux(h)) == kPass && r.hz == 60 && r.bpp == 36);
        CHECK(h.r[0x056C9] == 0x03000000 && h.r[0x056D2] == phxdpl::vidM1(241500, 0x14));   // depth untouched, M for 60
    }
    // ---- stat formats ----
    {
        Hw h; gop(h); State st{}; Run r{};
        run(env(h), st, r, kSelHbr2, 0, aux(h));
        char b[2048];
        const int n = stat(r, b, sizeof b);
        CHECK(n > 200 && n < (int)sizeof b && std::strstr(b, "result=PASS") && std::strstr(b, "scratch1=0x07008748"));
    }
    std::printf("dpb_retrain_test: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
