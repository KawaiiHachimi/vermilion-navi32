// SPDX-License-Identifier: MIT
// PhxP0Odm.h: the pipe-0 replay engine for the XTX machine's 2560x1440 monitor (LG ULTRAGEAR+ on DP B).
// b131-b134: 240 Hz on ODM 2:1 (OTG0 -> OPP0 + OPP1, HUBP0 + HUBP1 each scanning half of the same surface, DSC0 + DSC1,
// 16 bpp) over HBR3 x4 with FEC, and back to Linux's 144. b135: one engine for every 1440p rate (60 / 120 / 144 / 240),
// always through Linux's 144 as the hub (.agents/findings/2026-11-20-240hz-plan.md, "b135").
//
// Source of every step: Linux 7.2's own mode sets on this card and monitor, turned into replay plans by
// tools/capture/p0odm_from_trace.py. Capture v3 (2026-11-20 130351) recorded each rate from Linux's 144 and back
// (to-<mode> / from-<mode>): phx_p0odm_1440p{60,120,240}_{to,from}.inc. The entry from macOS's own state (the GOP's
// one pixel per clock at 144, set by the pipe-0 engine) is b133's 240 plan (phx_p0odm_1440p240.inc, capture v2), the one
// plan whose start is not Linux's. Linux's values in Linux's order, pipe 1's surface / colour registers copied from pipe
// 0's live values, Linux's link windows run by PhxDpLink.h's trainer (TRANSMITTER_CONTROL bytes = Linux's, the rate Linux
// picked per mode: HBR at 60, HBR2 at 120 / 144, HBR3 at 240, FEC on at every rate as Linux), Linux's REG_WAITs inserted.
// What the generator drops is listed there.
//
// States: `mac` (pipe 0 as the GOP / the pipe-0 engine left it: PhxPipe0.h owns 60 / 120 / 144) and `linux` at one mode
// of the set (cur). From mac only the entry runs (target 240). From linux every target is cur -> 144 (from plan) ->
// target (to plan); 144 itself is the hub (no plan). A to plan that fails (a step or the post check) runs the target's
// from plan, which lands on 144 (fell back, not latched; a target that fell back twice is refused for the boot). A
// failure before anything was written or sent is a plain refusal (b135: b134 latched a DAL refusal at the first step).
// b151: a from plan that fails (a fall-back, or leg 1 from linux) runs once more, as long as the boot has a recovery left
// (kMaxRecoveries); landing on 144 then counts as a fall-back. Only a from plan that fails again (or with no recovery
// left), a halt or a DMUB timeout latch `failed` (no more writes until reboot).
// b151: every link window trains the way Linux's perform_link_training_with_retries does on a PHY endpoint: up to
// kTrainAttempts at the plan's rate and lanes, link off between (dp_disable_link_phy: D3, TRANSMITTER_CONTROL DISABLE),
// then 100, 150, 200 ms; the first failed attempt's CR / EQ report is kept (Run.trainFail, Fail.train*).
//
// Selector (root writes debug.phxp0odm): 1 = survey (reads + DPCD reads only), 2 = 240 (b131 alias), 3 = 144 (alias),
// or the target in Hz: 60, 120, 144, 240. System Settings reaches it through phxPipe0SetMode (phx_p0odm.cpp p0OdmRoute).
// Before the first write (refusal = zero writes): from mac PhxDpbRetrain.h's gates (the GOP's DP B, a 1440p mode, OTG1
// off, DMCUB 0x07008742 / 0x07008748, ring idle, HBR3 x4 in the sink's caps); from linux cur's own state (V_TOTAL, ODM
// select, stream up, two pixels per clock, OTG1 off) and the sink's caps for the rates the run needs; the sink's FEC caps
// (and DSC caps and pipe 1 free for 240), the DAL path up. During the run: after every write and every DMUB command the
// fingerprint of everything outside pipes 0 / 1 (OTG1-3, HUBP2-3, MPCC2-3, DSC2-3, DIG0 / DIG2-4 back ends): a change =
// `halted` (shared latch).
// While pipe 0 runs ODM 2:1 the flip path writes HUBP1's address with HUBP0's (phx_scan.cpp, gP0OdmActive).
//
// Also here, because they act on the mode this engine lit:
//   HDR             the 1440p rates' HDR variants are modes of the same set (keys 1440060 .. 1440240, capture v4);
//   Adaptive Sync   OTG0's V_TOTAL range for the lit mode, double buffered (vrrRegs, vrrWriteOn, vrrWriteOff);
//   underflow       HUBPn's sticky underflow status around a run (clearUnderflow, post, leg).
// Kernel-free; glue phx_p0odm.cpp; host test tests/kext/p0odm_test.cpp. Build history: CHANGELOG.md.
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

enum Op : uint8_t { kW = 1, kCopy = 2, kWait = 3, kDal = 4, kDpcdW = 5, kLinkOff = 6, kLinkUp = 7, kDelay = 8, kSWait = 9 };
enum Sel : int { kSelSurvey = 1, kSel240 = 2, kSelBack = 3 };
enum Res : int { kNone = 0, kRefused, kSurveyed, kDone, kFellBack, kFailed, kHalted, kTimeout, kSame };
static const char *const kResName[] = {"none", "refused", "surveyed", "DONE", "FELL-BACK-TO-144", "FAILED", "HALTED", "timeout", "same"};
constexpr int kResMax = kSame;
enum Where : int { kMac = 0, kLinux = 1 };

// One replay plan; hz / vTotal / rate / lanes = the mode it lands on and its link.
struct Plan { const phx_p0odm_step *s; unsigned n; const char *name; uint32_t hz, vTotal; uint8_t rate, lanes; };
// One mode of the set: to = hub -> this mode, from = this mode -> hub (both nullptr for the hub). key = the selector that
// names it (b137): the 1440p rates by their Hz (60, 120, 144, 240: the b131-b136 selectors and phxPipe0SetMode's
// target_hz), every other resolution as height * 1000 + Hz (1080060 = 1920x1080 at 60, 480059 = 640x480 at 59.94).
struct Mode { uint32_t key, w, h, hz, hTotal, vTotal; uint8_t odm, rate, lanes; const Plan *to, *from; };
// The set: modes, the hub's index, the entry from mac (lands on mode entryMode).
struct Set { const Mode *m; unsigned n, hub; const Plan *entry; unsigned entryMode; };
constexpr unsigned kMaxModes = 16;

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
constexpr uint32_t kOtg0H = 0x04FEA, kHubp1Cntl = 0x03B8F, kMpcc1Opp = 0x09017, kDscTop0 = 0x064C0, kDscTop1 = 0x0651C, kOdmSrc = 0x04F8B,
                   kOdmTwo = 0x00100001u, kOdmOne = 0xfff00000u, kOtg0V = 0x04FEF, kOtg0Frame = 0x0500C, kHubp0Cntl = 0x03AB3,
                   kOdm1Global = 0x04F9A;
constexpr uint32_t kDphyCntl = 0x056D7, kHubp1Clk = 0x03B90, kDom1Status = 0x03543;
// b136: DCHUBBUB DET / compressed-buffer segments (SIZE [4:0], SIZE_CURRENT [12:8]; COMPBUF CONFIG_ERROR bit 31)
constexpr uint32_t kDet0 = 0x0397B, kDet1 = 0x0397C, kCompbuf = 0x0397A, kCompbufErr = 0x80000000u;
inline bool detApplied(uint32_t v) { return (v & 0x1fu) == ((v >> 8) & 0x1fu); }

// b151: Linux's link-training retries (drivers/gpu/drm/amd/display/dc/link/link_dpms.c LINK_TRAINING_ATTEMPTS 4;
// link/protocols/link_dp_training.c LINK_TRAINING_RETRY_DELAY 50 ms, the delay grows by 50 before each wait)
constexpr unsigned kTrainAttempts = 4;
constexpr uint32_t kTrainRetryDelayUs[kTrainAttempts - 1] = {100000, 150000, 200000};
// b151: from plans run again after a failure at most this often per boot (then a failure latches, as before b151)
constexpr uint32_t kMaxRecoveries = 2;

// One link window's training: attempts made, each attempt's result, the first failed attempt's report.
// b152: the plan it belongs to and its timing (us, from Env::nowUs; 0 without a clock): offToUp = from the plan's
// LINKOFF to this LINKUP (Linux's capture v4: 12-28 ms), train = the LINKUP with every attempt, sincePrev = from the
// previous window's end in this run to this window's LINKOFF (~0.2 s between the two windows of an HDR switch).
struct Train {
    uint8_t tries, rate, lanes;
    int rc[kTrainAttempts];
    phxdpl::Report first;   // the first failed attempt (CR / EQ loops, last lane status, drive, the status after)
    bool failedOnce;
    const char *plan;
    uint32_t offToUpUs, trainUs, sincePrevUs;
};
constexpr unsigned kMaxWins = 4;   // link windows kept per run (leg 1 + an HDR to plan's two + a fall-back's)

struct Fail {
    const char *why, *plan; uint32_t step, line, op, reg, val, got, dalResp, target;
    // the failed run's post check, kept here because a later refused run clears the run's own copy
    uint32_t hz, v, h, odm, stream, stream2, hubp0, hubp1, det0, det1, compbuf;
    Train train;            // b151: the failed run's last link window (tries = 0: the run never trained)
};

struct Run {
    // boot-long
    int failed;                     // latch: nothing but survey until reboot
    int where, cur;                 // kMac, or kLinux at mode m[cur]
    uint32_t attempts, refusals, dones, fellBacks, sames;
    uint32_t fbCount[kMaxModes];    // fall-backs per target mode (2 = refused for the boot)
    uint32_t recoveries, recovered; // b151: from plans run again after a failure / of those, landed on 144
    uint32_t trainRetried;          // b151: link windows that needed more than one attempt and trained
    const char *lastRefusal;
    Fail last;                      // the last failure, kept across runs
    // this run
    int sel, res, phase;            // phase: 0 gates, 1 leg 1, 2 leg 2, 3 fall-back, 4 post
    uint32_t target;
    const char *why, *pipeWhy, *plan1, *plan2, *fbPlan;
    const char *retryWhy;           // b151: why the first try of the from plan that ran again failed
    phxdpb::Run g;                  // the gates' reads (DP B, mode, DMCUB, DPCD)
    uint8_t fecCap, dscCap;
    uint32_t hubp1Clk, dom1;
    uint32_t step, line, op, reg, val, got;   // the step a failure stopped at
    uint32_t effects;               // writes / copies / DPCD writes / link ops / acknowledged DAL messages so far
    uint32_t writes, copies, waits, waitMaxUs, dals, dpcd, dalResp, strayWr, swaits, swaitTimeouts, swaitMaxUs;
    phxdpl::Report rep;
    int trc;
    Train train;                    // b151: this run's last link window
    Train wins[kMaxWins];           // b152: this run's link windows in order (the first kMaxWins)
    uint32_t nWins;
    uint64_t tOff, tUpEnd;          // b152: the clock at the last LINKOFF / at the end of the last LINKUP (0 = none yet)
    uint32_t fp[kNFp];
    uint32_t postHz, postOdm, postV, postHubp0, postHubp1, postStream, postDphy, backRes, postStream2, postPixFmt, postPixDiv;
    uint32_t postDet0, postDet1, postCompbuf, postH;
    uint32_t staleUf;               // HUBP underflow stickies set before the plans, cleared (bit 0 HUBP0, bit 1 HUBP1)
    uint32_t postRetries;           // post checks repeated after clearing an underflow sticky the switch raised
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

inline uint64_t now(const Env &e) { return e.nowUs ? e.nowUs(e.io.c) : 0; }

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
        r.effects++;
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
        r.effects++;
        return 0;
    }
    case kDpcdW: {
        if (s.reg != 0x107 && s.reg != 0x160) { r.why = "DPCD write not 0x107 / 0x160"; return 1; }
        const uint8_t b = static_cast<uint8_t>(s.val);
        r.effects++;
        if (x.aux.wr(x.aux.c, kConnDpB.aux, s.reg, &b, 1)) { r.why = "DPCD write not acknowledged"; return 1; }
        r.dpcd++;
        return 0;
    }
    case kLinkOff: {   // Linux: D3, DISABLE, pattern / link / internal 0, FEC_READY shadow, DPCD 0x120 = 0, shadow off
        r.effects++;
        r.tOff = now(e);
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
        r.effects++;
        phxdpb::Run &g = r.g;
        phxdpl::Sink sk{};
        phxdpb::sinkOf(g, &sk);
        phxdpl::Opts o; o.onePpc = false; o.fec = (s.mask & 1) != 0; o.panelModeRead = true; o.attempts = 1;
        phxdpl::Ctx lx{io, kConnDpB, o, &r.rep};
        const phxdpl::Link l{static_cast<uint8_t>(s.val), static_cast<uint8_t>(s.arg)};
        // b151: Linux's retries on a PHY endpoint (perform_link_training_with_retries, no fallback to a lower rate):
        // after a failed attempt dp_disable_link_phy (phxdpl::linkOff: D3, TRANSMITTER_CONTROL DISABLE, pattern / link /
        // internal 0), then the wait, then the next attempt at the same link (link setup, ENABLE, training: SET_POWER D0,
        // FEC_READY again as dp_perform_link_training's dp_set_fec_ready, the patterns). After the last failure Linux keeps
        // the PHY on; so does this. A DMUB or argument failure is not retried (nothing more may be sent / the plan is wrong).
        Train &t = r.train;
        t = Train{};
        t.rate = l.rate; t.lanes = l.lanes; t.plan = p.name;
        const uint64_t t0 = now(e);
        if (t0 && r.tOff) t.offToUpUs = static_cast<uint32_t>(t0 - r.tOff);
        if (r.tOff && r.tUpEnd && r.tOff > r.tUpEnd) t.sincePrevUs = static_cast<uint32_t>(r.tOff - r.tUpEnd);
        auto keepWin = [&]() {
            const uint64_t t1 = now(e);
            if (t0 && t1) t.trainUs = static_cast<uint32_t>(t1 - t0);
            r.tUpEnd = t1;
            if (r.nWins < kMaxWins) r.wins[r.nWins] = t;
            r.nWins++;
        };
        for (unsigned a = 0; a < kTrainAttempts; a++) {
            if (a) {
                const int off = phxdpl::linkOff(lx);
                if (x.st->dmub.dead || x.st->halted || off == phxdpl::kErrDmub) { r.why = "link training retry: link off failed (DMUB / guard)"; keepWin(); return 1; }
                e.io.delayUs(e.io.c, kTrainRetryDelayUs[a - 1]);
            }
            r.rep = phxdpl::Report{};
            r.trc = phxdpb::linkUp(lx, sk, l);
            t.rc[a] = r.trc;
            t.tries = static_cast<uint8_t>(a + 1);
            if (x.st->dmub.dead || x.st->halted) { r.why = "link up: TRANSMITTER_CONTROL timed out / guard"; keepWin(); return 1; }
            if (r.trc == phxdpl::kOk) break;
            if (!t.failedOnce) { t.first = r.rep; t.failedOnce = true; }
            if (r.trc == phxdpl::kErrDmub || r.trc == phxdpl::kErrArg) break;
        }
        keepWin();
        if (r.trc != phxdpl::kOk) { r.why = t.tries > 1 ? "link training failed (every attempt)" : "link training failed"; return 1; }
        if (t.tries > 1) r.trainRetried++;
        if (o.fec) phxdpl::W(lx, kDphyCntl, phxdpl::kDphyFecReady | phxdpl::kDphyFecEn);   // Linux: FEC on after training
        return x.st->halted ? 1 : 0;
    }
    case kDelay:
        e.io.delayUs(e.io.c, s.arg);
        return 0;
    case kSWait: {   // b136: Linux's REG_WAIT semantics (a timeout logs, the sequence goes on)
        uint32_t t = 0;
        for (; t <= s.arg; t += 10) {
            if ((rd(e, s.reg) & s.mask) == s.val) break;
            e.io.delayUs(e.io.c, 10);
        }
        r.swaits++;
        if (t > s.arg) r.swaitTimeouts++;
        if (t > r.swaitMaxUs) r.swaitMaxUs = t;
        return 0;
    }
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

inline int findMode(const Set &s, uint32_t key) {
    for (unsigned i = 0; i < s.n; i++) if (s.m[i].key == key) return static_cast<int>(i);
    return -1;
}
// ---- Adaptive Sync (.agents/findings/2026-11-20-adaptive-sync-research.md) ---------------------------------------------
// Linux's DCN 3.2 DRR on a firmware without fw-assisted mclk switching (the GOP build reports none, debug.phxdmub_caps):
// optc32_set_drr -> optc1_set_vtotal_min_max writes OTG_V_TOTAL_MAX, then OTG_V_TOTAL_MIN (lines - 1 each), then
// optc32_setup_manual_trigger updates OTG_V_TOTAL_CONTROL (MIN_SEL 1, MAX_SEL 1, FORCE_LOCK_ON_EVENT 0,
// SET_V_TOTAL_MIN_MASK = TRIGA); each flip, optc2_program_manual_trigger writes OTG_TRIGA_MANUAL_TRIG = 1 (phx_scan.cpp
// scanPresent, kOtg0TrigAManualTrig). The range for a mode: min = its V_TOTAL, max = its lines at minHz with integer Hz
// (Linux at 144 with a 48 Hz floor: 1516 * 144 / 48 = 4548 lines -> 0x11c3, capture v3).
constexpr uint32_t kOtg0VMin = 0x04FF0, kOtg0VMax = 0x04FF1, kOtg0VCtl = 0x04FF3;
constexpr uint32_t kVCtlMinSel = 1u, kVCtlMaxSel = 2u, kVCtlForceLock = 0x10u, kVCtlMinMask = 0xffff0000u;
constexpr uint32_t kVCtlTrigA = (1u << 1) << 16;   // SET_V_TOTAL_MIN_MASK bit 1 = TRIGA
struct VrrRegs { uint32_t vmin, vmax, ctl; };
inline bool vrrRegs(const Mode &m, uint32_t minHz, uint32_t ctlNow, VrrRegs *o) {
    if (!minHz || minHz >= m.hz || !m.vTotal) return false;
    const uint32_t lines = (m.vTotal + 1) * m.hz / minHz;
    if (lines <= m.vTotal + 1 || lines - 1 > 0x7fffu) return false;
    o->vmin = m.vTotal;
    o->vmax = lines - 1;
    o->ctl = (ctlNow & ~(kVCtlMinSel | kVCtlMaxSel | kVCtlForceLock | kVCtlMinMask)) | kVCtlMinSel | kVCtlMaxSel | kVCtlTrigA;
    return true;
}
// Linux's DRR double buffer. optc3_tg_init (dcn30_optc.c, dcn32's .tg_init, run for every OTG at init_hw) sets
// OTG_DOUBLE_BUFFER_CONTROL.OTG_DRR_TIMING_DBUF_UPDATE_MODE = 2, so V_TOTAL_MIN / MAX and their selectors latch at the
// start of a frame. The GOP leaves the mode at 0 (any time; read on the XTX machine): a range or ramp write then takes
// effect mid-frame, a MAX below the current line ends the frame at the write, and HUBP0's underflow sticky comes up
// (measured on the XTX machine: .agents/findings/2026-11-22-vrr-underflow-and-auxkc.md). So while the range is on the
// mode is Linux's 2; off waits for the latch (Linux optc3_wait_drr_doublebuffer_pending_clear) and puts the boot mode
// back, so the replay plans run under the GOP's mode, as they always have here.
constexpr uint32_t kOtg0Dbuf = 0x0501B, kDbufDrrMode = 0x03000000u, kDbufDrrFrameStart = 0x02000000u, kDbufDrrPending = 0x10u;
inline uint32_t dbufWith(uint32_t now, uint32_t mode) { return (now & ~kDbufDrrMode) | (mode & kDbufDrrMode); }
// The range on (Linux's order: MAX, MIN, then the CONTROL fields), double buffered. *bootDbuf = the register before.
inline void vrrWriteOn(const Env &e, const VrrRegs &v, uint32_t *bootDbuf) {
    const uint32_t db = rd(e, kOtg0Dbuf);
    if (bootDbuf) *bootDbuf = db;
    if ((db & kDbufDrrMode) != kDbufDrrFrameStart) e.io.wr(e.io.c, kOtg0Dbuf, dbufWith(db, kDbufDrrFrameStart));
    e.io.wr(e.io.c, kOtg0VMax, v.vmax);
    e.io.wr(e.io.c, kOtg0VMin, v.vmin);
    e.io.wr(e.io.c, kOtg0VCtl, v.ctl);
}
// The range off: the boot values, latched at the next frame start (at most one frame of the floor away; 100 ms cap), then
// the boot double-buffer mode. Returns the wait in us; *timedOut when no frame start was seen.
inline uint32_t vrrWriteOff(const Env &e, const uint32_t boot[3], uint32_t bootDbuf, bool *timedOut) {
    const uint32_t f0 = rd(e, kOtg0Frame) & 0xffffff;
    e.io.wr(e.io.c, kOtg0VCtl, boot[2]);
    e.io.wr(e.io.c, kOtg0VMin, boot[0]);
    e.io.wr(e.io.c, kOtg0VMax, boot[1]);
    uint32_t us = 0;
    bool done = false;
    if ((rd(e, kOtg0Dbuf) & kDbufDrrMode) != 0) {   // double buffered: a frame start applies them
        while (us < 100000) {
            const uint32_t df = ((rd(e, kOtg0Frame) & 0xffffff) - f0) & 0xffffff;
            if (df >= 2 || (df >= 1 && !(rd(e, kOtg0Dbuf) & kDbufDrrPending))) { done = true; break; }
            e.io.delayUs(e.io.c, 200); us += 200;
        }
    } else done = true;
    if (timedOut) *timedOut = !done;
    const uint32_t db = rd(e, kOtg0Dbuf);
    if ((db & kDbufDrrMode) != (bootDbuf & kDbufDrrMode)) e.io.wr(e.io.c, kOtg0Dbuf, dbufWith(db, bootDbuf));
    return us;
}
// HUBPn's underflow status is sticky (read on the XTX machine: set after a failed run, cleared by HUBP_UNDERFLOW_CLEAR,
// not back in 25 s at a fixed rate). One set before a run says nothing about the run: clear it (Linux
// hubp1_clear_underflow's bit; 1 then 0 as the pipe-0 engine's stage 10 does) so the post check sees only its own.
constexpr uint32_t kHubpUfClear = 0x80000000u;
inline bool clearUnderflow(const Env &e, uint32_t reg) {
    const uint32_t v = rd(e, reg);
    if (!(v & phxp2::kUnderflow)) return false;
    const uint32_t keep = v & ~(phxp2::kUnderflow | kHubpUfClear);
    e.io.wr(e.io.c, reg, keep | kHubpUfClear);
    e.io.wr(e.io.c, reg, keep);
    return true;
}
// The mode pipe 0 is on, once a replay landed (null in the mac state).
inline const Mode *curMode(const Run &r, const Set &s) {
    return r.where == kLinux && r.cur >= 0 && static_cast<unsigned>(r.cur) < s.n ? &s.m[r.cur] : nullptr;
}
inline uint32_t curHz(const Run &r, const Set &s) { const Mode *m = curMode(r, s); return m ? m->hz : 0; }
inline uint32_t curKey(const Run &r, const Set &s) { const Mode *m = curMode(r, s); return m ? m->key : 0; }
inline bool odmActive(const Run &r, const Set &s) { const Mode *m = curMode(r, s); return m && m->odm; }
inline bool setOk(const Set &s) {
    if (!s.m || !s.n || s.n > kMaxModes || s.hub >= s.n || s.entryMode >= s.n || !s.entry || !s.entry->n) return false;
    for (unsigned i = 0; i < s.n; i++) {
        const bool hub = i == s.hub;
        if (hub != !s.m[i].to || hub != !s.m[i].from) return false;
        if (!hub && (!s.m[i].to->n || !s.m[i].from->n)) return false;
        for (unsigned j = 0; j < i; j++) if (s.m[j].key == s.m[i].key) return false;
    }
    return true;
}

// Pipe 0 is on mode m of the set as a replay left it (reads only).
inline const char *stateOf(const Env &e, const Mode &m) {
    if (rd(e, kOtg0V) != m.vTotal || rd(e, kOtg0H) != m.hTotal) return "pipe 0 is not on the mode the engine set (H / V_TOTAL)";
    if (rd(e, kOdmSrc) != (m.odm ? kOdmTwo : kOdmOne)) return "pipe 0 is not on the mode the engine set (ODM select)";
    if (!(rd(e, phxdpb::kR.vidStream) & phxdpl::kStreamStatus)) return "DP1 stream not running";
    if (!(rd(e, phxdpb::kR.pixFmt) & phxdpb::kPix2ppc)) return "DP1 not two pixels per clock (not Linux's setup)";
    if (rd(e, phxdpb::kOtg1Ctl) & 1u) return "OTG1 enabled (a second display)";
    return nullptr;
}

inline const char *gates(const Env &e, State &st, Run &r, int restartDone, const phxdpb::Aux &aux, const Set &s, int ti) {
    if (r.failed && r.sel != kSelSurvey) return "a run failed in this boot: nothing more until reboot";
    if (st.halted) return "halted: something changed earlier in this boot";
    if (!e.dal || e.dalDead) return "no DAL path (hard minimums)";
    if (!setOk(s)) return "no plan set";
    for (unsigned k = 0; k < kNFp; k++) r.fp[k] = rd(e, kFp[k].reg);
    bool toOdm = false;
    if (r.where == kMac) {
        if (r.sel != kSelSurvey && ti != static_cast<int>(s.entryMode)) return "from macOS's own mode only the entry (240 Hz) runs here";
        r.g = phxdpb::Run{};
        r.g.sel = phxdpb::kSelHbr3;
        const char *g = phxdpb::gates(e, st, r.g, restartDone, aux);
        if (g) return g;
        toOdm = s.m[s.entryMode].odm;
    } else {
        if (r.cur < 0 || static_cast<unsigned>(r.cur) >= s.n) return "no current mode";
        const Mode &c = s.m[r.cur];
        const char *w = stateOf(e, c);
        if (w) return w;
        uint8_t need = c.rate;
        if (ti >= 0) {
            if (s.m[ti].rate > need) need = s.m[ti].rate;
            if (s.m[s.hub].rate > need) need = s.m[s.hub].rate;
            toOdm = s.m[ti].odm && !c.odm;
        }
        const char *sk = readSink(aux, r.g, need);
        if (sk) return sk;
        r.g.hz = c.hz;
    }
    uint8_t b = 0;
    if (aux.rd(aux.c, kConnDpB.aux, 0x090, &b, 1)) return "DPCD 0x090 (FEC caps) not readable";
    r.fecCap = b;
    if (!(b & 1)) return "the sink is not FEC capable";
    if (aux.rd(aux.c, kConnDpB.aux, 0x060, &b, 1)) return "DPCD 0x060 (DSC caps) not readable";
    r.dscCap = b;
    if (r.sel == kSelSurvey) toOdm = r.where == kMac;
    if (toOdm) {
        if (!(b & 1)) return "the sink does not support DSC";
        r.hubp1Clk = rd(e, kHubp1Clk); r.dom1 = rd(e, kDom1Status);   // recorded (a power-gated pipe 1 may read 0)
        if (!(rd(e, kHubp1Cntl) & 1u) && (r.hubp1Clk & 1u)) return "pipe 1 busy: HUBP1 clocked and not blanked";
        if ((rd(e, kMpcc1Opp) & 0xf) != 0xf) return "pipe 1 busy: MPCC1 connected to an OPP";
        if ((rd(e, kDscTop0) & 1u) || (rd(e, kDscTop1) & 1u)) return "DSC0 / DSC1 already clocked";
    }
    return nullptr;
}

// After a plan: pipe 0 on mode m (frame rate, totals, ODM select, DET applied, the stream twice 0.5 s apart, no
// underflow). *onlyUnderflow = everything on the mode except an underflow sticky.
inline bool post(const Env &e, Run &r, const Mode &m, bool *onlyUnderflow = nullptr) {
    r.postHz = measureHz(e);
    r.postOdm = rd(e, kOdmSrc); r.postV = rd(e, kOtg0V); r.postH = rd(e, kOtg0H);
    r.postHubp0 = rd(e, kHubp0Cntl); r.postHubp1 = rd(e, kHubp1Cntl);
    r.postStream = rd(e, phxdpb::kR.vidStream); r.postDphy = rd(e, kDphyCntl);
    e.io.delayUs(e.io.c, 500000);   // the stream once went off after the first read (XTX machine, b132)
    r.postStream2 = rd(e, phxdpb::kR.vidStream); r.postPixFmt = rd(e, phxdpb::kR.pixFmt); r.postPixDiv = rd(e, phxdpb::kPixDiv);
    r.postDet0 = rd(e, kDet0); r.postDet1 = rd(e, kDet1); r.postCompbuf = rd(e, kCompbuf);
    // +-4 Hz on top of 5 %: measureHz counts whole frames in 250 ms (60 Hz reads 56 or 60 by phase)
    const uint32_t lo = m.hz * 95 / 100 - 4, hi = m.hz * 105 / 100 + 4;
    const bool rest = detApplied(r.postDet0) && detApplied(r.postDet1) && (r.postDet0 & 0x1fu) && !(r.postCompbuf & kCompbufErr) &&
                      r.postHz >= lo && r.postHz <= hi && r.postV == m.vTotal && r.postH == m.hTotal &&
                      r.postOdm == (m.odm ? kOdmTwo : kOdmOne) &&
                      (r.postStream & phxdpl::kStreamStatus) && (r.postStream2 & phxdpl::kStreamStatus);
    const bool uf = (r.postHubp0 & phxp2::kUnderflow) || (m.odm && (r.postHubp1 & phxp2::kUnderflow));
    if (onlyUnderflow) *onlyUnderflow = rest && uf;
    return rest && !uf;
}

inline void keepFail(Run &r, const char *plan) {
    r.last = Fail{r.why, plan, r.step, r.line, r.op, r.reg, r.val, r.got, r.dalResp, r.target,
                  r.postHz, r.postV, r.postH, r.postOdm, r.postStream, r.postStream2, r.postHubp0, r.postHubp1,
                  r.postDet0, r.postDet1, r.postCompbuf, r.train};
}

// One leg: the plan, then the post check on mode m. 0 = landed, 1 = failed (r.why set), -1 = halted / DMUB dead.
inline int leg(Ctx &x, const Plan &p, const Mode &m) {
    const int bad = runPlan(x, p);
    if (x.st->halted || x.st->dmub.dead) return -1;
    if (bad) return 1;
    bool onlyUf = false;
    if (post(*x.e, *x.r, m, &onlyUf)) return 0;
    if (onlyUf) {
        // Everything on the mode, only an underflow sticky: one the switch itself raised (the pipe-0 engine clears its
        // own the same way, stage 10). Cleared once and the whole check again (0.75 s): a live underflow is back by then.
        clearUnderflow(*x.e, kHubp0Cntl);
        if (m.odm) clearUnderflow(*x.e, kHubp1Cntl);
        x.r->postRetries++;
        if (post(*x.e, *x.r, m)) return 0;
    }
    x.r->why = "post check: frame rate / V_TOTAL / ODM / stream / underflow / DET not as planned";
    return 1;
}

inline int stop(Run &r, State &st) {
    r.failed = 1;
    if (st.halted) { r.res = kHalted; if (!r.why) r.why = "halted"; }
    else { r.res = kTimeout; r.why = "a DMUB command timed out: debug.phxdmub_dead latched"; }
    keepFail(r, r.plan2 ? r.plan2 : r.plan1);
    return r.res;
}

// sel: kSelSurvey, kSel240 / kSelBack (aliases), or the target in Hz.
inline int run(const Env &e, State &st, Run &r, int sel, int restartDone, const phxdpb::Aux &aux, const Set &s) {
    {
        Run k = r;
        r = Run{};
        r.failed = k.failed; r.where = k.where; r.cur = k.cur; r.attempts = k.attempts + 1; r.refusals = k.refusals; r.dones = k.dones;
        r.fellBacks = k.fellBacks; r.sames = k.sames; r.lastRefusal = k.lastRefusal; r.last = k.last;
        r.recoveries = k.recoveries; r.recovered = k.recovered; r.trainRetried = k.trainRetried;
        for (unsigned i = 0; i < kMaxModes; i++) r.fbCount[i] = k.fbCount[i];
    }
    r.sel = sel;
    const uint32_t target = sel == kSel240 ? 240u : sel == kSelBack ? 144u : sel == kSelSurvey ? 0u : static_cast<uint32_t>(sel);
    r.target = target;
    const int ti = target ? findMode(s, target) : -1;
    if (sel != kSelSurvey && (ti < 0 || sel <= 0))
        return refuse(r, "target: 1 survey, or a mode of the set (1440p 60 / 120 / 144 / 240, or height * 1000 + Hz)");
    if (ti >= 0 && r.fbCount[ti] >= 2) return refuse(r, "this rate fell back twice in this boot");
    const char *g = gates(e, st, r, restartDone, aux, s, ti);
    if (g) return refuse(r, g);
    if (sel == kSelSurvey) { r.res = kSurveyed; r.why = "gates pass (reads only, nothing written)"; return kSurveyed; }
    if (r.where == kLinux && ti == r.cur) { r.sames++; r.res = kSame; r.why = "already at that rate"; return kSame; }

    // underflow stickies from before this run (Adaptive Sync time, an earlier switch) are not this run's
    if (clearUnderflow(e, kHubp0Cntl)) r.staleUf |= 1u;
    if (clearUnderflow(e, kHubp1Cntl)) r.staleUf |= 2u;
    Ctx x{&e, &st, &r, aux};
    const Mode &tm = s.m[ti];
    // the legs: mac -> entry; linux: cur -> hub (from), hub -> target (to)
    const Plan *p1 = nullptr, *p2 = nullptr;
    int m1 = -1;
    if (r.where == kMac) { p1 = s.entry; m1 = ti; }
    else {
        if (static_cast<unsigned>(r.cur) != s.hub) { p1 = s.m[r.cur].from; m1 = static_cast<int>(s.hub); }
        if (static_cast<unsigned>(ti) != s.hub) p2 = tm.to;
    }
    r.plan1 = p1 ? p1->name : nullptr;
    r.plan2 = p2 ? p2->name : nullptr;
    // b151: a from plan (to the hub) that failed (not halted, not a DMUB timeout) runs once more while the boot has a
    // recovery left: the same Linux sequence again from where the failure left pipe 0. 0 = landed, else leg()'s result.
    auto again = [&](int b, const Plan &p) -> int {
        if (b != 1 || r.recoveries >= kMaxRecoveries) return b;
        r.recoveries++;
        r.retryWhy = r.why;
        r.why = nullptr;
        const int b2 = leg(x, p, s.m[s.hub]);
        if (!b2) r.recovered++;
        return b2;
    };
    // a to-leg (the entry or hub -> target) that fails runs the target's from plan back to the hub
    auto fallBack = [&](const char *plan) -> int {
        keepFail(r, plan);
        if (!tm.from) { r.failed = 1; r.res = kFailed; return kFailed; }
        const char *why = r.why;
        const uint32_t s0 = r.step, l0 = r.line, o0 = r.op, rg0 = r.reg, v0 = r.val, got0 = r.got;
        r.phase = 3; r.why = nullptr; r.fbPlan = tm.from->name;
        const int b = again(leg(x, *tm.from, s.m[s.hub]), *tm.from);
        r.backRes = b ? 2 : 1;
        r.step = s0; r.line = l0; r.op = o0; r.reg = rg0; r.val = v0; r.got = got0; r.why = why;
        if (b < 0) return stop(r, st);
        if (b) { r.failed = 1; r.res = kFailed; return kFailed; }
        r.where = kLinux; r.cur = static_cast<int>(s.hub);
        r.fbCount[ti]++; r.fellBacks++;
        r.res = kFellBack;
        return kFellBack;
    };
    if (p1) {
        r.phase = 1;
        const int b = leg(x, *p1, s.m[m1]);
        if (b < 0) return stop(r, st);
        if (b) {
            if (!r.effects) { keepFail(r, p1->name); return refuse(r, r.why); }   // nothing written or sent: a plain refusal
            if (r.where == kMac) return fallBack(p1->name);
            keepFail(r, p1->name);   // a from plan failed: no plan knows this state, unless the plan lands when run again
            const uint32_t s0 = r.step, l0 = r.line, o0 = r.op, rg0 = r.reg, v0 = r.val, got0 = r.got;
            const char *why = r.why;
            const int b2 = again(b, *p1);
            r.step = s0; r.line = l0; r.op = o0; r.reg = rg0; r.val = v0; r.got = got0; r.why = why;
            if (b2 < 0) return stop(r, st);
            if (b2) { r.failed = 1; r.res = kFailed; return kFailed; }
            // landed on the hub: the target was not tried; this run counts as a fall-back for it
            r.where = kLinux; r.cur = static_cast<int>(s.hub);
            r.fbCount[ti]++; r.fellBacks++;
            r.res = kFellBack;
            return kFellBack;
        }
        r.where = kLinux; r.cur = m1;
    }
    if (p2) {
        r.phase = 2;
        const uint32_t e0 = r.effects;
        const int b = leg(x, *p2, tm);
        if (b < 0) return stop(r, st);
        if (b) {
            if (r.effects == e0 && !p1) { keepFail(r, p2->name); return refuse(r, r.why); }
            return fallBack(p2->name);
        }
        r.cur = ti;
    }
    r.phase = 4;
    r.dones++;
    r.res = kDone;
    r.why = tm.odm ? "at the rate: ODM 2:1 + DSC + FEC" : "at the rate";
    return kDone;
}

inline const char *trainRc(const Train &t, unsigned a) { return a < t.tries ? phxdpl::rcName(t.rc[a]) : "-"; }

inline int stat(const Run &r, const Set &s, char *b, int n) {
    int o = 0;
#define PHXODM_OUT(...) do { if (o < n - 1) { const int k_ = snprintf(b + o, static_cast<size_t>(n - o), __VA_ARGS__); o += k_ > 0 ? k_ : 0; if (o > n - 1) o = n - 1; } } while (0)
    static const char *const ph[] = {"gates", "leg1", "leg2", "fall-back", "post"};
    PHXODM_OUT("result=%s target=%u phase=%s why=%s plans=%s,%s fallback=%s\n", kResName[r.res >= 0 && r.res <= kResMax ? r.res : 0],
               r.target, ph[r.phase >= 0 && r.phase <= 4 ? r.phase : 0], r.why ? r.why : "-", r.plan1 ? r.plan1 : "-",
               r.plan2 ? r.plan2 : "-", r.fbPlan ? r.fbPlan : "-");
    PHXODM_OUT("state: where=%s now=%ux%u@%u (key %u) failed=%d attempts=%u dones=%u fell_backs=%u sames=%u refusals=%u last_refusal=%s\n",
               r.where == kLinux ? "linux" : "mac", curMode(r, s) ? curMode(r, s)->w : 0, curMode(r, s) ? curMode(r, s)->h : 0, curHz(r, s),
               curKey(r, s), r.failed, r.attempts, r.dones, r.fellBacks, r.sames, r.refusals,
               r.lastRefusal ? r.lastRefusal : "-");
    PHXODM_OUT("last_fail: target=%u plan=%s step=%u line=%u op=%u reg=0x%05x val=0x%08x got=0x%08x dal_resp=0x%x why=%s\n",
               r.last.target, r.last.plan ? r.last.plan : "-", r.last.step, r.last.line, r.last.op, r.last.reg, r.last.val, r.last.got,
               r.last.dalResp, r.last.why ? r.last.why : "-");
    if (r.last.why)
        PHXODM_OUT("last_fail_post: hz=%u v_total=0x%x h_total=0x%x odm=0x%08x stream=0x%x stream_0.5s=0x%x hubp0=0x%08x hubp1=0x%08x "
                   "det0=0x%x det1=0x%x compbuf=0x%08x\n", r.last.hz, r.last.v, r.last.h, r.last.odm, r.last.stream, r.last.stream2,
                   r.last.hubp0, r.last.hubp1, r.last.det0, r.last.det1, r.last.compbuf);
    if (r.last.why && r.last.train.tries)
        PHXODM_OUT("last_fail_train: link 0x%02x x%u tries=%u rc=%s,%s,%s,%s first: cr=%u eq=%u vs=%u pe=%u status=%02x %02x %02x %02x %02x %02x "
                   "after=%02x %02x %02x %02x %02x %02x\n", r.last.train.rate, r.last.train.lanes, r.last.train.tries,
                   trainRc(r.last.train, 0), trainRc(r.last.train, 1), trainRc(r.last.train, 2), trainRc(r.last.train, 3),
                   r.last.train.first.crLoops, r.last.train.first.eqLoops, r.last.train.first.vs, r.last.train.first.pe,
                   r.last.train.first.status[0], r.last.train.first.status[1], r.last.train.first.status[2], r.last.train.first.status[3],
                   r.last.train.first.status[4], r.last.train.first.status[5], r.last.train.first.after[0], r.last.train.first.after[1],
                   r.last.train.first.after[2], r.last.train.first.after[3], r.last.train.first.after[4], r.last.train.first.after[5]);
    for (unsigned w = 0; w < r.nWins && w < kMaxWins; w++) {
        const Train &t = r.wins[w];
        PHXODM_OUT("train_w%u: plan=%s link 0x%02x x%u tries=%u rc=%s,%s,%s,%s off_to_up_us=%u train_us=%u since_prev_us=%u", w + 1,
                   t.plan ? t.plan : "-", t.rate, t.lanes, t.tries, trainRc(t, 0), trainRc(t, 1), trainRc(t, 2), trainRc(t, 3),
                   t.offToUpUs, t.trainUs, t.sincePrevUs);
        if (t.failedOnce)
            PHXODM_OUT(" first: cr=%u eq=%u vs=%u pe=%u status=%02x %02x %02x %02x %02x %02x", t.first.crLoops, t.first.eqLoops, t.first.vs,
                       t.first.pe, t.first.status[0], t.first.status[1], t.first.status[2], t.first.status[3], t.first.status[4],
                       t.first.status[5]);
        PHXODM_OUT("\n");
    }
    PHXODM_OUT("recovery: recoveries=%u/%u recovered=%u train_retried=%u retry_why=%s\n", r.recoveries, kMaxRecoveries, r.recovered,
               r.trainRetried, r.retryWhy ? r.retryWhy : "-");
    PHXODM_OUT("gates: otg0 %u Hz pclk=%u kHz scratch1=0x%08x link_now 0x%02x fec_cap=0x%02x dsc_cap=0x%02x hubp1_clk=0x%x dom1=0x%08x\n",
               r.g.hz, r.g.pclkKhz, r.g.scr1, r.g.link0[0], r.fecCap, r.dscCap, r.hubp1Clk, r.dom1);
    PHXODM_OUT("stop: step=%u line=%u op=%u reg=0x%05x val=0x%08x got=0x%08x back=%u\n", r.step, r.line, r.op, r.reg, r.val, r.got, r.backRes);
    PHXODM_OUT("io: effects=%u writes=%u copies=%u waits=%u wait_max_us=%u dal=%u dal_resp=%u dpcd=%u stray=%u train=%s cr=%u eq=%u tries=%u\n",
               r.effects, r.writes, r.copies, r.waits, r.waitMaxUs, r.dals, r.dalResp, r.dpcd, r.strayWr, phxdpl::rcName(r.trc),
               r.rep.crLoops, r.rep.eqLoops, r.train.tries);
    PHXODM_OUT("det: swaits=%u swait_timeouts=%u swait_max_us=%u det0=0x%x det1=0x%x compbuf=0x%08x\n", r.swaits, r.swaitTimeouts,
               r.swaitMaxUs, r.postDet0, r.postDet1, r.postCompbuf);
    PHXODM_OUT("post: hz=%u odm=0x%08x v_total=0x%x hubp0=0x%08x hubp1=0x%08x stream=0x%x stream_0.5s=0x%x pixfmt=0x%08x pixdiv=0x%x "
               "dphy=0x%x%s%s\n", r.postHz, r.postOdm, r.postV, r.postHubp0, r.postHubp1, r.postStream, r.postStream2, r.postPixFmt,
               r.postPixDiv, r.postDphy, r.pipeWhy ? " changed=" : "", r.pipeWhy ? r.pipeWhy : "");
    PHXODM_OUT("underflow: stale_cleared=0x%x post_retries=%u\n", r.staleUf, r.postRetries);
#undef PHXODM_OUT
    return o;
}

}  // namespace phxodm
