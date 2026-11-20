// SPDX-License-Identifier: MIT
// p0odm_test.cpp: host test of the b131 240 Hz path (src/vermilion-boot/PhxP0Odm.h + the generated ODM plans) against a model
// of the XTX machine: pipe 0 at the GOP's 1440p144 (macOS read 2026-11-20), pipe 1 dark, status bits that follow their
// enables (OTG busy / clock on, ODM input clock, power-gating FSM, update lock, HUBP outstanding requests, DIG FIFO reset,
// stream status), a frame counter from the DTO and the totals (two pixels per clock when DP_PIXEL_FORMAT bit 30 is set),
// a DMUB ring running TRANSMITTER_CONTROL on a model PHY, a DP sink (the LG's caps: HBR3 in the extended block, FEC, DSC),
// and the DAL mailbox.
//   c++ -std=c++17 -Wall -Wextra -Werror -I src/vermilion-boot -o /tmp/p0odm_test tests/kext/p0odm_test.cpp && /tmp/p0odm_test
#include "PhxP0Odm.h"
#include "phx_p0odm_1440p240.inc"
#include "phx_p0odm_1440p144odm.inc"
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
static const Plan kTo = {kPhxP0odm_1440p240, N(kPhxP0odm_1440p240), "1440p240", 240, 0x652, 0x1e};
static const Plan kBack = {kPhxP0odm_1440p144odm, N(kPhxP0odm_1440p144odm), "1440p144odm", 144, 0x5eb, 0x14};
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
    uint32_t touchOutsideAt = 0;   // after this many writes, OTG2 turns on by itself (a foreign change)
    bool otgOn() { return r[0x05001] & 1; }
    void advance(uint32_t us) {
        t += us;
        if (!otgOn()) return;
        const uint32_t div = r[0x0012F] & 7u;   // OTG0 field: 6 = the GOP's 1 ppc (DTO = pclk), 2 = 2 ppc (DTO = pclk / 2)
        const double pclk = (double)r[0x00141] * (div == 2 ? 2 : 1);
        const double rate = pclk / ((double)((r[0x04FEA] & 0x7fff) + 1) * ((r[0x04FEF] & 0x7fff) + 1));
        frac += us * 1e-6 * rate;
        const double whole = std::floor(frac);
        frames += static_cast<uint32_t>(whole);
        frac -= whole;
    }
    bool linkUp() {
        return phyRate && phyRate == dpcd[0x100] * 2700u && (dpcd[0x101] & 0x1f) == 4 && !(failHbr3 && phyRate == 81000);
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
        case 0x03AB3: case 0x03B8F: return v | 2u;
        case 0x0500C: return frames & 0xffffff;
        case 0x056CC: return (v & ~0x10000u) | ((v & 1u) && otgOn() && phyRate && (r[0x056C8] & 0x10) ? 0x10000u : 0u);
        case 0x05651: return (v & ~0x100000u) | ((v & 2u) && phyRate ? 0x100000u : 0u);   // RESET_DONE needs the symbol clock (box b131)
        case 0x056D2: return phyRate ? static_cast<uint32_t>(std::lround(0x8000 * (double)r[0x00141] / (phyRate * 10000.0))) : 0;
        default: return v;
        }
    }
    void wr(uint32_t a, uint32_t v) {
        writes.push_back({a, v});
        if (touchOutsideAt && writes.size() == touchOutsideAt) r[0x05101] = 1;
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
        {0x05081, 0x00000200}, {0x03AB3, 0x000f0000}, {0x04F8A, 0x00000000}, {0x04F8B, 0xfff00000},
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
    e.dal = [](void *c, uint32_t m, uint32_t a) { static_cast<Hw *>(c)->dal.push_back(m << 24 ^ a); return 1u; };
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
    for (const Plan *p : {&kTo, &kBack}) {
        unsigned off = 0, up = 0, bad = 0, dal = 0, waits = 0;
        for (unsigned i = 0; i < p->n; i++) {
            const phx_p0odm_step &s = p->s[i];
            if ((s.op == kW || s.op == kCopy) && denied(s.reg)) bad++;
            if (s.op == kCopy && denied(s.val)) bad++;
            if (s.op == kW && (s.reg == 0x03540 || s.reg == 0x00124 || (s.reg >= 0x03600 && s.reg <= 0x036FF))) bad++;
            if (s.op == kDal) { dal++; if ((s.val >> 16) != 8 && (s.val >> 16) != 9) bad++; }
            if (s.op == kLinkOff) off = i + 1;
            if (s.op == kLinkUp) { up = i + 1; if (s.val != p->rate || s.arg != 4 || !(s.mask & 1)) bad++; }
            if (s.op == kWait) waits++;
            if (s.op < kW || s.op > kDelay) bad++;
        }
        CHECK(!bad && off && up > off && dal >= 3 && waits >= 15);
    }
    // ---- survey: reads only ----
    {
        Hw h; gop(h); State st{}; Run r{};
        CHECK(run(env(h), st, r, kSelSurvey, 0, aux(h), kTo, kBack) == kSurveyed && h.writes.empty() && h.dpcdW.empty());
        CHECK(r.fecCap == 0xbf && r.dscCap == 0x01 && r.g.hz == 144);
        CHECK(run(env(h), st, r, kSelBack, 0, aux(h), kTo, kBack) == kRefused && h.writes.empty());   // back needs 240 first
    }
    // ---- 144 -> 240 -> 144 ----
    {
        Hw h; gop(h); State st{}; Run r{};
        const auto e = env(h);
        const int res = run(e, st, r, kSel240, 0, aux(h), kTo, kBack);
        CHECK(res == kDone);
        if (res != kDone) std::printf("  240: %s step %u line %u op %u reg 0x%x got 0x%x\n", r.why, r.step, r.line, r.op, r.reg, r.got);
        CHECK(r.at240 && !st.halted && r.postHz >= 228 && r.postHz <= 252);
        CHECK(h.r[0x04F8B] == 0x00100001 && h.r[0x04FEF] == 0x652 && h.r[0x00141] == 0x1f7e5cd8 && (h.r[0x056C9] & 0x40000000));
        CHECK(h.phyRate == 81000 && h.dpcd[0x100] == 0x1e && wrote(h, 0x120, 1) && wrote(h, 0x160, 1) && wrote(h, 0x107, 0x90));
        CHECK((h.r[0x056D7] & 0x30) == 0x30);                                     // FEC on
        CHECK(h.r[0x064C0] & 1 && h.r[0x0651C] & 1);                              // DSC0 + DSC1 clocked
        // pipe 1 scans pipe 0's surface with pipe 0's colour path; pipe 0's own values untouched
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
        for (auto &w : h.writes) CHECK(!denied(w.first) || w.first == phxdmub::kInbox1Wptr);
        // and back
        const size_t nw = h.writes.size();
        const int rb = run(e, st, r, kSelBack, 0, aux(h), kTo, kBack);
        CHECK(rb == kDone && !r.at240 && h.writes.size() > nw);
        if (rb != kDone) std::printf("  back: %s step %u line %u op %u reg 0x%x got 0x%x\n", r.why, r.step, r.line, r.op, r.reg, r.got);
        CHECK(h.r[0x04FEF] == 0x5eb && h.phyRate == 54000 && h.dpcd[0x100] == 0x14 && wrote(h, 0x160, 0) && r.postHz >= 137 && r.postHz <= 151);
        CHECK(h.r[0x03542] & 0x100);                                               // pipe 1 power-gated again (Linux)
        char b[2048];
        CHECK(stat(r, b, sizeof b) > 100 && std::strstr(b, "result=DONE"));
        // b134: 240 again from the back plan's 144 (Linux's own starting state), then back again
        CHECK(r.linux144 && !r.at240);
        const int r2 = run(e, st, r, kSel240, 0, aux(h), kTo, kBack);
        CHECK(r2 == kDone && r.at240 && !r.linux144 && h.r[0x04FEF] == 0x652 && r.postHz >= 228 && r.postHz <= 252);
        if (r2 != kDone) std::printf("  240 again: %s step %u line %u\n", r.why, r.step, r.line);
        CHECK(run(e, st, r, kSelBack, 0, aux(h), kTo, kBack) == kDone && r.linux144 && h.r[0x04FEF] == 0x5eb);
    }
    // ---- HBR3 fails: the back plan runs, 144 at HBR2 ----
    {
        Hw h; gop(h); h.failHbr3 = true; State st{}; Run r{};
        CHECK(run(env(h), st, r, kSel240, 0, aux(h), kTo, kBack) == kFellBack);
        CHECK(r.failed && !r.at240 && r.backRes == 1 && h.phyRate == 54000 && h.r[0x04FEF] == 0x5eb && r.op == kLinkUp);
        CHECK(run(env(h), st, r, kSel240, 0, aux(h), kTo, kBack) == kRefused);   // latched
    }
    // ---- refusals: zero writes ----
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
        {"2 ppc without the back plan", [](Hw &h) { h.r[0x056C9] = 0x41000000; }},
        };
        for (auto &c : cs) {
            Hw h; gop(h); c.f(h); State st{}; Run r{};
            const int res = run(env(h), st, r, kSel240, 0, aux(h), kTo, kBack);
            CHECK(res == kRefused && h.writes.empty() && h.dpcdW.empty() && h.dal.empty());
            if (res != kRefused) std::printf("  case %s: %s\n", c.what, r.why);
        }
        Hw h; gop(h); State st{}; Run r{};
        Env e = env(h); e.dal = nullptr;
        CHECK(run(e, st, r, kSel240, 0, aux(h), kTo, kBack) == kRefused && h.writes.empty());
    }
    // ---- a foreign change outside pipes 0 / 1 during the run: halted ----
    {
        Hw h; gop(h); h.touchOutsideAt = 20; State st{}; Run r{};
        CHECK(run(env(h), st, r, kSel240, 0, aux(h), kTo, kBack) == kHalted && st.halted && r.pipeWhy);
        CHECK(h.writes.size() == 20);   // stopped at the write after which the change was seen (no further write)
    }
    std::printf("p0odm_test: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
