// SPDX-License-Identifier: MIT
// PhxP0Odm.h: kext b131, the 240 Hz plan's E1 (.agents/findings/2026-11-20-240hz-plan.md): pipe 0 to 2560x1440 at 240 Hz
// on ODM 2:1 (OTG0 -> OPP0 + OPP1, HUBP0 + HUBP1 each scanning half of the same surface, DSC0 + DSC1, 16 bpp) over DP B at
// HBR3 x4 with FEC, and back to 1440p144 (Linux's 144: two pixels per clock, HBR2 x4, FEC on).
//
// Source of every step: Linux 7.2's own mode sets on this card and monitor (capture 2026-11-20 065140, 144 -> 240 and
// 240 -> 144), turned into replay plans by tools/capture/p0odm_from_trace.py (phx_p0odm_1440p240.inc,
// phx_p0odm_1440p144odm.inc): Linux's values in Linux's order, pipe 1's surface / colour registers copied from pipe 0's
// live values (the right half scans the same macOS surface through the same colour path), Linux's link windows run by
// PhxDpLink.h's trainer (TRANSMITTER_CONTROL bytes = Linux's, FEC_READY as Linux), Linux's REG_WAITs inserted. What the
// generator drops is listed there (pipe 0's surface / colour / power gating, DAL messages but the DISPCLK / DPPCLK hard
// minimums, DENTIST, audio, cursor, flips, DMCUB GPINT, every DMUB command but TRANSMITTER_CONTROL, the DPCD source OUI).
//
// Selector (root writes debug.phxp0odm): 1 = survey (reads + DPCD reads only), 2 = 240 Hz, 3 = back to 144 (only after 2
// in this boot). Inert until written. Not wired to System Settings yet.
// Before the first write (refusal = zero writes): PhxDpbRetrain.h's gates (the GOP's DP B, a 1440p mode, OTG1 off, DMCUB
// 0x07008742 / 0x07008748, ring idle, HBR3 x4 in the sink's extended caps), the sink's FEC and DSC caps, pipe 1 free
// (HUBP1 blanked, MPCC1 unconnected, DSC0 / DSC1 clocks off), the DAL path up. During the run: after every write and every
// DMUB command the fingerprint of everything outside pipes 0 / 1 (OTG1-3, HUBP2-3, MPCC2-3, DSC2-3, DIG0 / DIG2-4 back
// ends): a change = `halted` (shared latch). Any failed step on the way to 240 runs the back plan once (best effort: the
// result is 144 Hz on Linux's two-pixel-per-clock setup, or dark until reboot).
// After 240: the flip path writes HUBP1's address with HUBP0's (phx_scan.cpp, gP0OdmActive).
// Kernel-free; glue phx_p0odm.cpp; host test tests/kext/p0odm_test.cpp.
#pragma once
#include <stdint.h>
#include "PhxDpbRetrain.h"

#define PHX_P0ODM_TYPES
struct phx_p0odm_step { uint8_t op; uint32_t line, reg, val, mask, arg; };

namespace phxodm {

using phxp2::Env;
using phxp2::State;
using phxp2::rd;
using phxdpl::kConnDpB;

enum Op : uint8_t { kW = 1, kCopy = 2, kWait = 3, kDal = 4, kDpcdW = 5, kLinkOff = 6, kLinkUp = 7, kDelay = 8 };
enum Sel : int { kSelSurvey = 1, kSel240 = 2, kSelBack = 3 };
enum Res : int { kNone = 0, kRefused, kSurveyed, kDone, kFellBack, kFailed, kHalted, kTimeout };
static const char *const kResName[] = {"none", "refused", "surveyed", "DONE", "FAILED-BACK-AT-144", "FAILED", "HALTED", "timeout"};

struct Plan { const phx_p0odm_step *s; unsigned n; const char *name; uint32_t hz, vTotal; uint8_t rate; };

// Never written by a plan whatever it says: the DMCUB block, DENTIST, the DAL mailbox, MMVM semaphores, other pipes.
inline bool denied(uint32_t r) {
    return (r >= 0x03600 && r <= 0x036FF) || r == 0x00124 || (r >= 0x16200 && r <= 0x162FF) || (r >= 0x1A751 && r <= 0x1A762) ||
           (r >= 0x0506A && r <= 0x051E9) ||   // OTG1-3 (OTGn_OTG_H_TOTAL = 0x4FEA + 0x80 n, 0x80 each)
           (r >= 0x03C5D && r <= 0x03E14) ||   // HUBP2-3 / HUBPREQ2-3 / HUBPRET2-3 (HUBP0 0x3AA5, stride 0xDC)
           (r >= 0x06578 && r <= 0x06690);     // DSC2-3
}
// Everything outside pipes 0 / 1 a run must not change.
struct Fp { uint32_t reg, mask; const char *name; };
static const Fp kFp[] = {
    {0x05081, 0xffffffffu, "OTG1_OTG_CONTROL"}, {0x05101, 0xffffffffu, "OTG2_OTG_CONTROL"}, {0x05181, 0xffffffffu, "OTG3_OTG_CONTROL"},
    {0x03C6B, 0x0000000fu, "HUBP2_DCHUBP_CNTL"}, {0x03D47, 0x0000000fu, "HUBP3_DCHUBP_CNTL"},
    {0x0902C, 0x0000000fu, "MPCC2_MPCC_OPP_ID"}, {0x09041, 0x0000000fu, "MPCC3_MPCC_OPP_ID"},
    {0x05572, 0x00000001u, "DIG0_DIG_BE_EN_CNTL"}, {0x05772, 0x00000001u, "DIG2_DIG_BE_EN_CNTL"},
    {0x05872, 0x00000001u, "DIG3_DIG_BE_EN_CNTL"}, {0x05972, 0x00000001u, "DIG4_DIG_BE_EN_CNTL"},
    {0x0012F, 0x00000ff8u, "OTG_PIXEL_RATE_DIV(OTG1-3)"},
};
static constexpr unsigned kNFp = sizeof kFp / sizeof kFp[0];
constexpr uint32_t kHubp1Cntl = 0x03B8F, kMpcc1Opp = 0x09017, kDscTop0 = 0x064C0, kDscTop1 = 0x0651C, kOdmSrc = 0x04F8B,
                   kOdmTwo = 0x00100001u, kOtg0V = 0x04FEF, kOtg0Frame = 0x0500C, kHubp0Cntl = 0x03AB3, kOdm1Global = 0x04F9A;
constexpr uint32_t kDphyCntl = 0x056D7, kHubp1Clk = 0x03B90, kDom1Status = 0x03543;

struct Run {
    int failed, at240, linux144;    // latches: failed = nothing but survey until reboot; at240 = the 240 plan finished;
                                    // linux144 = the back plan finished (pipe 0 on Linux's 2 ppc 144, b134)
    uint32_t attempts, refusals, dones;
    int sel, res, phase;            // phase: 0 gates, 1 plan, 2 back plan, 3 post
    const char *why, *lastRefusal, *pipeWhy;
    phxdpb::Run g;                  // the retrain gates' reads (DP B, mode, DMCUB, DPCD)
    uint8_t fecCap, dscCap;
    uint32_t hubp1Clk, dom1;
    uint32_t step, line, op, reg, val, got;   // the step a failure stopped at
    uint32_t writes, copies, waits, waitMaxUs, dals, dpcd, dalResp, strayWr;
    phxdpl::Report rep;
    int trc;
    uint32_t fp[kNFp];
    uint32_t postHz, postOdm, postV, postHubp0, postHubp1, postStream, postDphy, backRes, postStream2, postPixFmt, postPixDiv;
};

inline int refuse(Run &r, const char *why) { r.res = kRefused; r.why = r.lastRefusal = why; r.refusals++; return kRefused; }

inline bool outsideSame(const Env &e, const Run &r, const char **why) {
    for (unsigned k = 0; k < kNFp; k++)
        if ((rd(e, kFp[k].reg) & kFp[k].mask) != (r.fp[k] & kFp[k].mask)) { *why = kFp[k].name; return false; }
    return true;
}

// ---- the run's Io for the trainer (DP B only, every write through the guard) ----
struct Ctx { const Env *e; State *st; Run *r; phxdpb::Aux aux; };
inline bool guard(Ctx &x) {
    const char *w = nullptr;
    if (!outsideSame(*x.e, *x.r, &w)) { x.st->halted = 1; x.r->pipeWhy = w; return false; }
    return true;
}
inline uint32_t cRd(void *c, uint32_t reg) { return rd(*static_cast<Ctx *>(c)->e, reg); }
inline void cWr(void *c, uint32_t reg, uint32_t v) {
    Ctx &x = *static_cast<Ctx *>(c);
    if (x.st->halted) return;
    if (!phxdpb::writeOk(reg)) { x.r->strayWr++; return; }
    x.e->io.wr(x.e->io.c, reg, v);
    x.r->writes++;
    guard(x);
}
inline int cDmub(void *c, const uint32_t cmd[16]) {
    Ctx &x = *static_cast<Ctx *>(c);
    if (x.st->halted) return -1;
    const char *w = nullptr;
    if (phxdmub::submitWith(x.e->io, x.st->dmub, x.e->vramBytes, cmd, phxdpb::txCheck, nullptr, &w) != phxdmub::kOk) return -1;
    return guard(x) ? 0 : -1;
}
inline int cAuxRd(void *c, uint32_t a, uint8_t *b, unsigned n) { Ctx &x = *static_cast<Ctx *>(c); return x.aux.rd(x.aux.c, kConnDpB.aux, a, b, n); }
inline int cAuxWr(void *c, uint32_t a, const uint8_t *b, unsigned n) {
    Ctx &x = *static_cast<Ctx *>(c);
    if (x.st->halted || !phxdpl::dpcdWriteAllowed(a, n)) return -1;
    x.r->dpcd++;
    return x.aux.wr(x.aux.c, kConnDpB.aux, a, b, n);
}
inline void cDelay(void *c, uint32_t us) { Ctx &x = *static_cast<Ctx *>(c); x.e->io.delayUs(x.e->io.c, us); }

// One plan step. 0 = ok, else a failure (r.why set).
inline int step(Ctx &x, const phxdpl::Io &io, const Plan &p, unsigned i) {
    const phx_p0odm_step &s = p.s[i];
    const Env &e = *x.e;
    Run &r = *x.r;
    r.step = i; r.line = s.line; r.op = s.op; r.reg = s.reg; r.val = s.val;
    switch (s.op) {
    case kW: case kCopy: {
        if (denied(s.reg) || (s.op == kCopy && denied(s.val))) { r.why = "a plan register is on the deny list"; return 1; }
        const uint32_t src = s.op == kCopy ? rd(e, s.val) : s.val;
        const uint32_t v = s.mask == 0xffffffffu ? src : (rd(e, s.reg) & ~s.mask) | (src & s.mask);
        e.io.wr(e.io.c, s.reg, v);
        if (s.op == kW) r.writes++; else r.copies++;
        if (!guard(x)) { r.why = "a register outside pipes 0 / 1 changed"; return 1; }
        return 0;
    }
    case kWait: {
        uint32_t t = 0;
        for (; t <= s.arg; t += 10) {
            if ((rd(e, s.reg) & s.mask) == s.val) break;
            e.io.delayUs(e.io.c, 10);
        }
        r.waits++;
        if (t > r.waitMaxUs) r.waitMaxUs = t;
        if (t > s.arg) { r.got = rd(e, s.reg); r.why = "a wait timed out (Linux's REG_WAIT)"; return 1; }
        return 0;
    }
    case kDal: {
        if (!e.dal || e.dalDead) { r.why = "no DAL path"; return 1; }
        if (e.pmfwIdle && !e.pmfwIdle(e.io.c)) { r.why = "PMFW busy before a DAL message"; return 1; }
        if ((s.val >> 16) != 8 && (s.val >> 16) != 9) { r.why = "DAL hard min not DISPCLK / DPPCLK"; return 1; }
        r.dalResp = e.dal(e.io.c, 9, s.val);
        r.dals++;
        if (r.dalResp != 1) { r.why = "SetHardMinByFreq not acknowledged (response != 1)"; return 1; }
        return 0;
    }
    case kDpcdW: {
        if (s.reg != 0x107 && s.reg != 0x160) { r.why = "DPCD write not 0x107 / 0x160"; return 1; }
        const uint8_t b = static_cast<uint8_t>(s.val);
        if (x.aux.wr(x.aux.c, kConnDpB.aux, s.reg, &b, 1)) { r.why = "DPCD write not acknowledged"; return 1; }
        r.dpcd++;
        return 0;
    }
    case kLinkOff: {   // Linux: D3, DISABLE, pattern / link / internal 0, FEC_READY shadow, DPCD 0x120 = 0, shadow off
        phxdpl::Opts o; o.onePpc = false; o.fec = true;
        phxdpl::Ctx lx{io, kConnDpB, o, &r.rep};
        int rc = phxdpl::linkOff(lx);
        if (x.st->dmub.dead || x.st->halted || rc == phxdpl::kErrDmub) { r.why = "link off: TRANSMITTER_CONTROL / guard failed"; return 1; }
        phxdpl::W(lx, kDphyCntl, phxdpl::kDphyFecReady);
        const uint8_t z = 0;
        if (x.aux.wr(x.aux.c, kConnDpB.aux, 0x120, &z, 1)) { r.why = "DPCD 0x120 = 0 not acknowledged"; return 1; }
        phxdpl::W(lx, kDphyCntl, 0);
        return x.st->halted ? 1 : 0;
    }
    case kLinkUp: {
        phxdpb::Run &g = r.g;
        phxdpl::Sink sk{};
        phxdpb::sinkOf(g, &sk);
        phxdpl::Opts o; o.onePpc = false; o.fec = (s.mask & 1) != 0; o.panelModeRead = true; o.attempts = 1;
        phxdpl::Ctx lx{io, kConnDpB, o, &r.rep};
        const phxdpl::Link l{static_cast<uint8_t>(s.val), static_cast<uint8_t>(s.arg)};
        r.trc = phxdpb::linkUp(lx, sk, l);
        if (x.st->dmub.dead || x.st->halted) { r.why = "link up: TRANSMITTER_CONTROL timed out / guard"; return 1; }
        if (r.trc != phxdpl::kOk) { r.why = "link training failed"; return 1; }
        if (o.fec) phxdpl::W(lx, kDphyCntl, phxdpl::kDphyFecReady | phxdpl::kDphyFecEn);   // Linux: FEC on after training
        return x.st->halted ? 1 : 0;
    }
    case kDelay:
        e.io.delayUs(e.io.c, s.arg);
        return 0;
    }
    r.why = "unknown plan op";
    return 1;
}
inline int runPlan(Ctx &x, const Plan &p) {
    const phxdpl::Io io{&x, cRd, cWr, cDmub, cAuxRd, cAuxWr, cDelay};
    for (unsigned i = 0; i < p.n; i++)
        if (step(x, io, p, i)) return 1;
    return 0;
}

// OTG0 frames over ~250 ms -> Hz.
inline uint32_t measureHz(const Env &e) {
    const uint32_t f0 = rd(e, kOtg0Frame) & 0xffffff;
    e.io.delayUs(e.io.c, 250000);
    return ((rd(e, kOtg0Frame) - f0) & 0xffffff) * 4u;
}

// The sink's caps for the trainer (AUX reads only).
inline const char *readSink(const phxdpb::Aux &aux, phxdpb::Run &g, uint8_t minRate) {
    g = phxdpb::Run{};
    if (aux.rd(aux.c, kConnDpB.aux, 0, g.caps, 16)) return "DPCD caps not readable";
    if (g.caps[0xe] & 0x80) {
        g.extUsed = 1;
        if (aux.rd(aux.c, kConnDpB.aux, 0x2200, g.ext, 16)) return "DPCD extended caps not readable";
    }
    phxdpl::Sink sk{};
    if (!phxdpb::sinkOf(g, &sk) || sk.maxLanes < 4 || sk.maxRate < minRate) return "the sink cannot take the link (caps)";
    return nullptr;
}
inline const char *gates(const Env &e, State &st, Run &r, int restartDone, const phxdpb::Aux &aux, const Plan &to, const Plan &back) {
    if (r.sel != kSelSurvey && r.sel != kSel240 && r.sel != kSelBack) return "selector: 1 survey, 2 240 Hz, 3 back to 144";
    if (r.failed && r.sel != kSelSurvey) return "a run failed in this boot: nothing more until reboot";
    if (st.halted) return "halted: something changed earlier in this boot";
    if (!e.dal || e.dalDead) return "no DAL path (hard minimums)";
    if (!to.n || !back.n) return "no plan";
    for (unsigned k = 0; k < kNFp; k++) r.fp[k] = rd(e, kFp[k].reg);
    if (r.sel == kSelBack) {
        if (!r.at240) return "back: 240 Hz was not set by debug.phxp0odm in this boot";
        if (rd(e, kOdmSrc) != kOdmTwo || rd(e, kOtg0V) != to.vTotal) return "back: pipe 0 is not on the 240 plan's ODM 2:1 timing";
        return readSink(aux, r.g, back.rate);
    }
    if (r.at240) return "already at 240 Hz (3 = back)";
    if (r.linux144) {   // b134: again from the back plan's state (= Linux's own starting point of the 240 capture)
        if (rd(e, kOdmSrc) != 0xfff00000u || rd(e, kOtg0V) != back.vTotal) return "pipe 0 is not on the back plan's 144 (ODM 1:1, V 0x5eb)";
        if (!(rd(e, phxdpb::kR.vidStream) & phxdpl::kStreamStatus)) return "DP1 stream not running";
        if (!(rd(e, phxdpb::kR.pixFmt) & phxdpb::kPix2ppc)) return "DP1 not two pixels per clock (not the back plan's state)";
        if (rd(e, phxdpb::kOtg1Ctl) & 1u) return "OTG1 enabled (a second display)";
        const char *sk = readSink(aux, r.g, phxdpl::kHbr3);
        if (sk) return sk;
        r.g.hz = 144;
    } else {
        r.g = phxdpb::Run{};
        r.g.sel = phxdpb::kSelHbr3;
        const char *g = phxdpb::gates(e, st, r.g, restartDone, aux);
        if (g) return g;
    }
    uint8_t b = 0;
    if (aux.rd(aux.c, kConnDpB.aux, 0x090, &b, 1)) return "DPCD 0x090 (FEC caps) not readable";
    r.fecCap = b;
    if (!(b & 1)) return "the sink is not FEC capable";
    if (aux.rd(aux.c, kConnDpB.aux, 0x060, &b, 1)) return "DPCD 0x060 (DSC caps) not readable";
    r.dscCap = b;
    if (!(b & 1)) return "the sink does not support DSC";
    r.hubp1Clk = rd(e, kHubp1Clk); r.dom1 = rd(e, kDom1Status);   // recorded (a power-gated pipe 1 may read 0)
    if (!(rd(e, kHubp1Cntl) & 1u) && (r.hubp1Clk & 1u)) return "pipe 1 busy: HUBP1 clocked and not blanked";
    if ((rd(e, kMpcc1Opp) & 0xf) != 0xf) return "pipe 1 busy: MPCC1 connected to an OPP";
    if ((rd(e, kDscTop0) & 1u) || (rd(e, kDscTop1) & 1u)) return "DSC0 / DSC1 already clocked";
    return nullptr;
}

inline int run(const Env &e, State &st, Run &r, int sel, int restartDone, const phxdpb::Aux &aux, const Plan &to, const Plan &back) {
    const int failed = r.failed, at240 = r.at240, l144 = r.linux144;
    const uint32_t att = r.attempts + 1, refs = r.refusals, dones = r.dones;
    const char *lastRef = r.lastRefusal;
    r = Run{};
    r.failed = failed; r.at240 = at240; r.linux144 = l144; r.attempts = att; r.refusals = refs; r.dones = dones; r.lastRefusal = lastRef;
    r.sel = sel;
    const char *g = gates(e, st, r, restartDone, aux, to, back);
    if (g) return refuse(r, g);
    if (sel == kSelSurvey) { r.res = kSurveyed; r.why = "gates pass (reads only, nothing written)"; return kSurveyed; }
    Ctx x{&e, &st, &r, aux};
    const Plan &p = sel == kSel240 ? to : back;
    r.phase = 1;
    const int bad = runPlan(x, p);
    if (st.halted) { r.res = kHalted; r.failed = 1; if (!r.why) r.why = "halted"; return kHalted; }
    if (st.dmub.dead) { r.res = kTimeout; r.failed = 1; r.why = "a DMUB command timed out: debug.phxdmub_dead latched"; return kTimeout; }
    if (bad) {
        r.failed = 1;
        if (sel == kSel240) {   // best effort: Linux's way back to 144
            const char *why = r.why;
            const uint32_t s0 = r.step, l0 = r.line, o0 = r.op, rg0 = r.reg, v0 = r.val, got0 = r.got;
            r.phase = 2;
            r.why = nullptr;
            const int bb = runPlan(x, back);
            r.backRes = bb ? 2 : 1;
            r.step = s0; r.line = l0; r.op = o0; r.reg = rg0; r.val = v0; r.got = got0; r.why = why;
            if (!bb && !st.halted && !st.dmub.dead) { r.res = kFellBack; return kFellBack; }
        }
        r.res = kFailed;
        return kFailed;
    }
    r.phase = 3;
    r.postHz = measureHz(e);
    r.postOdm = rd(e, kOdmSrc); r.postV = rd(e, kOtg0V); r.postHubp0 = rd(e, kHubp0Cntl); r.postHubp1 = rd(e, kHubp1Cntl);
    r.postStream = rd(e, phxdpb::kR.vidStream); r.postDphy = rd(e, kDphyCntl);
    e.io.delayUs(e.io.c, 500000);   // b133: the stream once went off after the first read (box b132)
    r.postStream2 = rd(e, phxdpb::kR.vidStream); r.postPixFmt = rd(e, phxdpb::kR.pixFmt); r.postPixDiv = rd(e, phxdpb::kPixDiv);
    const uint32_t want = p.hz, lo = want * 95 / 100, hi = want * 105 / 100;
    const bool ok = r.postHz >= lo && r.postHz <= hi && r.postV == p.vTotal && (r.postStream & phxdpl::kStreamStatus) &&
                    (r.postStream2 & phxdpl::kStreamStatus) && !(r.postHubp0 & phxp2::kUnderflow) &&
                    !(sel == kSel240 && (r.postHubp1 & phxp2::kUnderflow));
    if (!ok) {
        r.failed = 1;
        r.why = "post check: frame rate / V_TOTAL / stream / underflow not as planned";
        if (sel == kSel240 && !st.halted && !st.dmub.dead) {   // b133: back to 144 instead of leaving it dark
            const char *why = r.why;
            r.phase = 2; r.why = nullptr;
            const int bb = runPlan(x, back);
            r.backRes = bb ? 2 : 1; r.why = why;
            if (!bb && !st.halted && !st.dmub.dead) { r.res = kFellBack; return kFellBack; }
        }
        r.res = kFailed;
        return kFailed;
    }
    r.at240 = sel == kSel240;
    r.linux144 = sel == kSelBack;
    r.dones++;
    r.res = kDone;
    r.why = sel == kSel240 ? "240 Hz on ODM 2:1 + DSC + FEC, HBR3 x4" : "back at 144 Hz (Linux's 2 ppc setup, HBR2 + FEC)";
    return kDone;
}

inline int stat(const Run &r, char *b, int n) {
    int o = 0;
#define PHXODM_OUT(...) do { if (o < n - 1) { const int k_ = snprintf(b + o, static_cast<size_t>(n - o), __VA_ARGS__); o += k_ > 0 ? k_ : 0; if (o > n - 1) o = n - 1; } } while (0)
    static const char *const ph[] = {"gates", "plan", "back-plan", "post"};
    PHXODM_OUT("result=%s sel=%d phase=%s why=%s\n", kResName[r.res >= 0 && r.res <= kTimeout ? r.res : 0], r.sel,
               ph[r.phase >= 0 && r.phase <= 3 ? r.phase : 0], r.why ? r.why : "-");
    PHXODM_OUT("boot: attempts=%u dones=%u refusals=%u failed=%d at240=%d last_refusal=%s\n", r.attempts, r.dones, r.refusals,
               r.failed, r.at240, r.lastRefusal ? r.lastRefusal : "-");
    PHXODM_OUT("gates: otg0 %u Hz pclk=%u kHz scratch1=0x%08x link_now 0x%02x fec_cap=0x%02x dsc_cap=0x%02x hubp1_clk=0x%x dom1=0x%08x\n",
               r.g.hz, r.g.pclkKhz, r.g.scr1, r.g.link0[0], r.fecCap, r.dscCap, r.hubp1Clk, r.dom1);
    PHXODM_OUT("stop: step=%u line=%u op=%u reg=0x%05x val=0x%08x got=0x%08x back=%u\n", r.step, r.line, r.op, r.reg, r.val, r.got, r.backRes);
    PHXODM_OUT("io: writes=%u copies=%u waits=%u wait_max_us=%u dal=%u dal_resp=%u dpcd=%u stray=%u train=%s cr=%u eq=%u\n", r.writes,
               r.copies, r.waits, r.waitMaxUs, r.dals, r.dalResp, r.dpcd, r.strayWr, phxdpl::rcName(r.trc), r.rep.crLoops, r.rep.eqLoops);
    PHXODM_OUT("post: hz=%u odm=0x%08x v_total=0x%x hubp0=0x%08x hubp1=0x%08x stream=0x%x stream_0.5s=0x%x pixfmt=0x%08x pixdiv=0x%x "
               "dphy=0x%x%s%s\n", r.postHz, r.postOdm, r.postV, r.postHubp0, r.postHubp1, r.postStream, r.postStream2, r.postPixFmt,
               r.postPixDiv, r.postDphy, r.pipeWhy ? " changed=" : "", r.pipeWhy ? r.pipeWhy : "");
#undef PHXODM_OUT
    return o;
}

}  // namespace phxodm
