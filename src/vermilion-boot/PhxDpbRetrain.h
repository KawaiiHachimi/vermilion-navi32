// SPDX-License-Identifier: MIT
// PhxDpbRetrain.h: kext b129, the 240 Hz plan's steps D1 / D2 (.agents/findings/2026-11-20-240hz-plan.md): retrain the
// GOP's own DP B link (pipe 0, the desktop: OTG0 -> DIG1 -> PHY B) in place, at HBR2 x4 (D1: the rate the GOP trained) or
// HBR3 x4 (D2), with the OTG, the pixel clock, the plane and the stream encoder front end left running. The screen is dark
// for the retrain (about 0.1-1 s) and comes back at the same mode.
//
// Source of every step:
//   - the link part is PhxDpLink.h's trainer (Linux v6.8 link_dp_training_8b_10b.c order; box-proven by upstream on DP A /
//     DP E with DMCUB 0x07008742), with the TRANSMITTER_CONTROL bytes Linux 7.2 sent for this PHY in the 144 -> 240 Hz
//     capture (2026-11-20: phyid 1, hpdsel 3, symclk 54000 / 81000: kConnDpB);
//   - the stream part is Linux's blank / unblank of the stream encoder (enc1_stream_encoder_dp_blank,
//     enc32_stream_encoder_dp_unblank: stream off, steer FIFO reset, DIG FIFO reset pulse, M/N, stream on), as PhxDpbFix.h
//     and the b106 pipe-0 shell run it on DP B, with the GOP's one-pixel-per-clock values read live (DP_VID_TIMING N_MUL 0,
//     DIG FIFO 0x1d, DP_VID_STREAM_CNTL DIS_DEFER 2 without CHANGE_KEEPOUT): every value written back is the value the gate
//     read, except DP_VID_M (computed for the new link rate; the hardware then measures it) and the link registers the
//     trainer writes (Linux's values, equal to the GOP's on this card: DP_CONFIG 3, scrambler 0x0101ff10, framing 0x10102000).
// Deviations from Linux, on purpose: no DIG / DP front-end re-setup (FE_CNTL, BE_CNTL, pixel format, MSA, SDP are the GOP's
// and stay); no FEC / DSC (D3 / E steps); no DPCD 0x107 MSA_TIMING_PAR_IGNORE; one QUERY_FEATURE_CAPS ping first (Linux sends
// it at DMUB init) so a dead ring is found before the picture is touched.
//
// Selector (root writes debug.phxdpb_retrain): 1 = survey (reads + DPCD reads over AUX2 only), 2 = retrain HBR2 x4,
// 3 = retrain HBR3 x4. A failed training at the target falls back to HBR2 x4 once (kFellBack); a failed fall-back latches
// `failed` (the link stays off: dark until the next boot, where the GOP trains HBR2 again).
//
// Writes: MMIO only the DIG1 / DP1 registers in writeOk (the trainer's link registers, DP1 stream / steer / M/N / timing,
// DIG1 FIFO, and DIG1 BE_CNTL only to put the gate's value back); DMUB only the ping and TRANSMITTER_CONTROL for PHY B
// (phxdpl::txAllowed with kConnDpB); DPCD writes only 0x100-0x108 and 0x600. After every MMIO write and DMUB command the
// pipe-0 fingerprint below (OTG0, pixel clock, DENTIST, DIG1 FE, MPCC0, OTG1; HUBP0 / ODM0 underflow not newly set): a change
// = `halted` (the shared latch: nothing more by any engine until reboot). DIG1 BE_CNTL / BE_EN are not in it (the
// transmitter DISABLE / ENABLE may toggle the back end, unverified) and are checked at the end instead.
// Kernel-free; glue phx_dpbretrain.cpp; host test tests/kext/dpb_retrain_test.cpp.
#pragma once
#include <stdint.h>
#include "PhxPipe2.h"
#include "PhxDpLink.h"

namespace phxdpb {

using phxp2::Env;
using phxp2::State;
using phxp2::rd;
using phxdpl::kConnDpB;

enum Sel : int { kSelSurvey = 1, kSelHbr2 = 2, kSelHbr3 = 3 };
enum Res : int { kNone = 0, kRefused, kSurveyed, kPass, kFellBack, kTrainFail, kTimeout, kNotSent, kHalted, kPostFail };
static const char *const kResName[] = {"none", "refused", "surveyed", "PASS", "FELL-BACK-HBR2", "TRAIN-FAIL", "timeout",
                                       "not-sent", "HALTED", "POST-FAIL"};

// GOP DMCUB builds that run TRANSMITTER_CONTROL: the XT's (upstream, box-proven) and this XTX's (read 2026-11-19; that it
// executes TRANSMITTER_CONTROL the same way is what D1 shows).
constexpr uint32_t kFwXt = 0x07008742u, kFwXtx = 0x07008748u;
constexpr uint32_t kPingHdr = 0x0c000006u;   // QUERY_FEATURE_CAPS (type 6, sub 0, payload 12), all-zero payload

// ---- registers (absolute dwords, dcn_3_2_0_offset.h) ----
constexpr uint32_t kOtg0Ctl = 0x05001, kOtg0H = 0x04FEA, kOtg0V = 0x04FEF, kOtg0Frame = 0x0500C, kOtg1Ctl = 0x05081,
                   kPixRate0 = 0x00140, kPixDiv = 0x0012F, kDto0Phase = 0x00141, kDto0Mod = 0x00142, kDentist = 0x00124,
                   kHubp0Cntl = 0x03AB3, kOdm0 = 0x04F8A, kMpcc0Opp = 0x09002;
constexpr uint32_t kHpd2Status = phxddc::kHpd0IntStatus + phxddc::kHpdStride * kConnDpB.hpd;
constexpr phxdpl::Regs kR = phxdpl::regs(kConnDpB.dig);
static_assert(kR.feCntl == 0x0564B && kR.fifo == 0x05651 && kR.beCntl == 0x05671 && kR.beEn == 0x05672 &&
              kR.linkCntl == 0x056C8 && kR.pixFmt == 0x056C9 && kR.config == 0x056CB && kR.vidStream == 0x056CC &&
              kR.steer == 0x056CD && kR.vidTiming == 0x056D0 && kR.vidN == 0x056D1 && kR.vidM == 0x056D2,
              "DIG1 / DP1 = the macOS read of 2026-11-20");

// The GOP's DP B state on the XTX machine (macOS read 2026-11-20 at 1440p144 after b128, and at 60 before): the gate
// requires exactly these, so every write-back is known.
constexpr uint32_t kGopBeCntl = 0x20000200u;   // DIG_HPD_SELECT 2, FE source DIG1
constexpr uint32_t kGopFifo = 0x1du;            // READ_START_LEVEL 7, OUTPUT_PIXEL_MODE 0 (1 ppc), enabled
constexpr uint32_t kFifoStatusBits = 0x30100000u;   // DIG_FIFO_ERROR, RESET_DONE (status, ignored by the gate)
constexpr uint32_t kGopVidStream = 0x201u;      // ENABLE, DIS_DEFER 2, no CHANGE_KEEPOUT (STATUS bit 16 masked)
constexpr uint32_t kGopSteer = 0x3f000000u, kGopVidTiming = 0x100u, kGopVidN = 0x8000u, kGopConfig = 3u;
// DP_STEER_FIFO status bits (sticky, never written): OVERFLOW_FLAG / _INT, TU_OVERFLOW_FLAG (b130: seen set after a boot
// with macOS restoring 144 Hz, 2026-11-20 07:20). The gate compares only the configuration fields.
constexpr uint32_t kSteerStatusBits = 0x00000130u;
constexpr uint32_t kPix2ppc = 0x40000000u, kPixDepthMask = 0x07000000u;
constexpr uint32_t kDtoModulo = 716666667u;     // DP_DTO0_MODULO the GOP sets (0x2ab7772b)
static_assert(kGopBeCntl == phxdpl::beCntl(kConnDpB, true) && kGopFifo == (phxdpl::kFifoBase1ppc | phxdpl::kFifoEnable) &&
              kGopSteer == phxdpl::kSteerTu && kGopVidTiming == phxdpl::kVidTimingGen && kGopVidN == phxdpl::kVidN,
              "the GOP's values = the trainer's one-pixel-per-clock values");
// 1440p modes of the pipe-0 engine's plans (phx_p0_plan_1440p*.inc: H 0xa9f; V 60 / 120 / 144).
struct Mode { uint32_t vTotal, hz; };
static const Mode kModes[] = {{0x5c8, 60}, {0x5f4, 120}, {0x5eb, 144}};
constexpr uint32_t kHTotal = 0xa9f;

// The registers a retrain may write (a subset of phxdpl::writeAllowed(kConnDpB)).
inline bool writeOk(uint32_t r) {
    const uint32_t ok[] = {kR.fifo, kR.beCntl, kR.linkCntl, kR.config, kR.vidStream, kR.steer, kR.internal, kR.vidTiming,
                           kR.vidN, kR.vidM, kR.framing, kR.dphyCntl, kR.tpsSel, kR.prbs, kR.scram};
    for (uint32_t a : ok) if (a == r) return true;
    return false;
}

// Pipe 0 outside the link: must not change during a run.
struct Fp { uint32_t reg, mask; const char *name; };
static const Fp kFp[] = {
    {kOtg0Ctl, 0xffffffffu, "OTG0_OTG_CONTROL"},
    {kOtg0H, 0xffffffffu, "OTG0_OTG_H_TOTAL"},
    {kOtg0V, 0xffffffffu, "OTG0_OTG_V_TOTAL"},
    {kPixRate0, 0x0000333bu, "OTG0_PIXEL_RATE_CNTL"},
    {kPixDiv, 0x00000007u, "OTG_PIXEL_RATE_DIV(OTG0)"},
    {kDto0Phase, 0xffffffffu, "DP_DTO0_PHASE"},
    {kDto0Mod, 0xffffffffu, "DP_DTO0_MODULO"},
    {kDentist, 0x7f007f7fu, "DENTIST_DISPCLK_CNTL(dividers)"},
    {0x0564B, 0xfef7ffffu, "DIG1_DIG_FE_CNTL"},   // SYMCLK_FE_ON / metadata status masked
    {kMpcc0Opp, 0x0000000fu, "MPCC0_MPCC_OPP_ID"},
    {kOtg1Ctl, 0xffffffffu, "OTG1_OTG_CONTROL"},
};
static constexpr unsigned kNFp = sizeof kFp / sizeof kFp[0];

struct Run {
    int failed;                     // latch: a fall-back failed (the link is off) or something else went wrong
    uint32_t attempts, refusals, passes, fellBack;
    int sel, res, phase;            // phase: 0 gates, 1 ping, 2 stream off, 3 train, 4 stream on, 5 post
    const char *why, *lastRefusal, *pipeWhy, *ringWhy;
    int ringRc;
    // gates
    uint32_t hpd, beCntl, beEn, feCntl, fifo, linkCntl, vidStream, steer, vidTiming, vidN, vidM0, pixFmt, config;
    uint32_t otgH, otgV, dtoPhase, dtoMod, scr0, scr1, cntl, otg1Ctl, pclkKhz, hz, bpp;
    uint8_t caps[16], ext[16], link0[2];
    int capsRc, extRc, extUsed;
    phxdpl::Link target, done;
    // run
    phxdpl::Report rep, rep2;
    int trc, trc2;
    uint32_t pingUs, pingReply, dmubSent, mmioWr, strayWr, blocked, auxRdN, auxWrN, auxErr, dpcdStray;
    uint32_t offUs, onUs;
    // post
    uint32_t postBeCntl, postBeEn, postLink, postStream, postVidM, wantVidM, beRestored;
    int pipe0Same;
    uint32_t fp[kNFp], odm0, hubp0;
};

inline int refuse(Run &r, const char *why) { r.res = kRefused; r.why = r.lastRefusal = why; r.refusals++; return kRefused; }
inline int end(Run &r, int res, const char *why) {
    r.res = res; r.why = why;
    if (res == kPass) r.passes++;
    else if (res == kFellBack) r.fellBack++;
    else r.failed = 1;
    return res;
}

inline bool pipe0Same(const Env &e, const Run &r, const char **why) {
    for (unsigned k = 0; k < kNFp; k++)
        if ((rd(e, kFp[k].reg) & kFp[k].mask) != (r.fp[k] & kFp[k].mask)) { *why = kFp[k].name; return false; }
    if (rd(e, kOdm0) & phxp2::kOdmUnderflowBits & ~r.odm0) { *why = "ODM0 underflow bit newly set"; return false; }
    if (rd(e, kHubp0Cntl) & phxp2::kUnderflow & ~r.hubp0) { *why = "HUBP0 underflow newly set"; return false; }
    return true;
}
inline bool guard(const Env &e, State &st, Run &r) {
    const char *w = nullptr;
    if (!pipe0Same(e, r, &w)) { st.halted = 1; r.pipe0Same = 0; r.pipeWhy = w; return false; }
    return true;
}

// The glue's AUX path for AUX engine 2 (phxddc under the EDID/AUX lock): 0 = ACK with n bytes.
struct Aux {
    void *c;
    int (*rd)(void *c, unsigned eng, uint32_t addr, uint8_t *buf, unsigned n);
    int (*wr)(void *c, unsigned eng, uint32_t addr, const uint8_t *buf, unsigned n);
};

// The sink: base caps, or the extended block 0x2200 when 0x00E bit 7 says it exists (the LG: HBR2 at 0x001, HBR3 at
// 0x2201). Linux dp_retrieve_lttpr_cap / retrieve_link_cap read it the same way.
inline bool sinkOf(const Run &r, phxdpl::Sink *s) {
    if (!phxdpl::parseCaps(r.caps, s)) return false;
    if (r.extUsed) {
        phxdpl::Sink x{};
        if (!phxdpl::parseCaps(r.ext, &x)) return false;
        s->rev = x.rev; s->maxRate = x.maxRate; s->maxLanes = x.maxLanes; s->tps3 = x.tps3; s->tps4 = x.tps4;
        s->enhanced = x.enhanced; s->downspread = x.downspread;   // the training interval stays the base block's (0x00E)
    }
    return true;
}

// ---- gates: reads only (MMIO + DPCD reads over AUX2) ----
inline const char *gates(const Env &e, State &st, Run &r, int restartDone, const Aux &aux) {
    using namespace phxdmub;
    if (r.sel != kSelSurvey && r.sel != kSelHbr2 && r.sel != kSelHbr3) return "selector: 1 survey, 2 retrain HBR2 x4, 3 retrain HBR3 x4";
    if (r.failed && r.sel != kSelSurvey) return "a retrain failed in this boot: nothing more until reboot (survey still reads)";
    if (st.halted) return "halted: pipe 0 changed earlier in this boot";
    if (st.dmub.dead) return "debug.phxdmub_dead = 1";
    if (restartDone) return "debug.phxdmub_restart ran in this boot";
    if (st.switching) return "a pipe-1 mode switch is in progress";
    r.hpd = rd(e, kHpd2Status);
    if (!(r.hpd & 2u)) return "no sink: HPD2 (DP B) sense low";
    r.beCntl = rd(e, kR.beCntl); r.beEn = rd(e, kR.beEn); r.feCntl = rd(e, kR.feCntl); r.fifo = rd(e, kR.fifo);
    r.linkCntl = rd(e, kR.linkCntl); r.vidStream = rd(e, kR.vidStream); r.steer = rd(e, kR.steer);
    r.vidTiming = rd(e, kR.vidTiming); r.vidN = rd(e, kR.vidN); r.vidM0 = rd(e, kR.vidM); r.pixFmt = rd(e, kR.pixFmt);
    r.config = rd(e, kR.config);
    if (!(r.beEn & 1u) || r.beCntl != kGopBeCntl) return "DIG1 back end not the GOP's (BE_EN bit 0 = 1, BE_CNTL 0x20000200)";
    if (r.feCntl & 0xfu) return "DIG1 front end not fed by OTG0";
    if ((r.fifo & ~kFifoStatusBits) != kGopFifo) return "DIG1 FIFO not the GOP's one-pixel-per-clock 0x1d";
    if (!(r.linkCntl & phxdpl::kLinkTrained)) return "DP1 link not trained";
    if ((r.vidStream & ~phxdpl::kStreamStatus) != kGopVidStream || !(r.vidStream & phxdpl::kStreamStatus))
        return "DP1 stream not the GOP's running 0x201 (ENABLE, DIS_DEFER 2) with STATUS 1";
    if ((r.steer & ~kSteerStatusBits) != kGopSteer || r.vidTiming != kGopVidTiming || r.vidN != kGopVidN || r.config != kGopConfig)
        return "DP1 steer FIFO / VID_TIMING / VID_N / DP_CONFIG not the GOP's (0x3f000000 / 0x100 / 0x8000 / 3 = 4 lanes)";
    if (r.pixFmt & kPix2ppc) return "DP1 pixel format is two pixels per clock (not the GOP's convention)";
    const uint32_t depth = (r.pixFmt & kPixDepthMask) >> 24;
    r.bpp = depth == 1 ? 24 : depth == 2 ? 30 : depth == 3 ? 36 : 0;
    if (!r.bpp) return "DP1 component depth not 8 / 10 / 12 bpc";
    // pipe 0: a 1440p mode of the pipe-0 engine, the GOP's pixel clock convention (phase = pclk in Hz)
    r.otgH = rd(e, kOtg0H); r.otgV = rd(e, kOtg0V);
    r.dtoPhase = rd(e, kDto0Phase); r.dtoMod = rd(e, kDto0Mod);
    if (!(rd(e, kOtg0Ctl) & 1u)) return "OTG0 not enabled";
    r.hz = 0;
    for (const Mode &m : kModes) if (r.otgH == kHTotal && r.otgV == m.vTotal) r.hz = m.hz;
    if (!r.hz) return "OTG0 is not at a 1440p mode of the pipe-0 engine (H 0xa9f, V 0x5c8 / 0x5f4 / 0x5eb)";
    if (r.dtoMod != kDtoModulo || r.dtoPhase % 1000u || r.dtoPhase >= r.dtoMod) return "DP_DTO0 not the GOP's convention (modulo 716666667, phase = pclk in Hz)";
    r.pclkKhz = r.dtoPhase / 1000u;
    if (rd(e, kHubp0Cntl) & phxp2::kUnderflow) return "HUBP0 underflow status set";
    r.otg1Ctl = rd(e, kOtg1Ctl);
    if (r.otg1Ctl & 1u) return "OTG1 enabled (a second display): not with this step";
    // DMCUB
    r.cntl = rd(e, kCntl);
    if ((r.cntl & (kCntlEnable | kCntlPwait)) != (kCntlEnable | kCntlPwait)) return "DMCUB not enabled and idle (CNTL bit 16 / bit 20)";
    if (rd(e, kCntl2) & kSoftReset) return "DMCUB held in soft reset";
    r.scr0 = rd(e, kScratch0); r.scr1 = rd(e, kScratch1);
    if (!(r.scr0 & 2u)) return "SCRATCH0 bit 1 (mailbox ready) not set";
    if (r.scr1 != kFwXt && r.scr1 != kFwXtx) return "SCRATCH1 is not a GOP DMCUB build this step knows (0x07008742 / 0x07008748)";
    if (rd(e, kGpintIn1)) return "GPINT_DATAIN1 not 0 (a GPINT in flight)";
    if ((static_cast<uint64_t>(rd(e, kDcnVmFbBase) & 0xffffff) << 24) != e.fbMcBase) return "DCN FB base != the kext's FB base";
    Ring ring;
    const char *w = nullptr;
    if (!ringGeometry(e.io, e.vramBytes, &ring, &w)) return w;
    if (ring.wptr != ring.rptr || ring.wptr >= ring.size || ring.wptr % kCmdBytes) return "ring not idle (INBOX1 WPTR != RPTR)";
    for (unsigned j = 0; j < kNFp; j++) r.fp[j] = rd(e, kFp[j].reg);
    r.odm0 = rd(e, kOdm0) & phxp2::kOdmUnderflowBits;
    r.hubp0 = rd(e, kHubp0Cntl) & phxp2::kUnderflow;
    if (!phxp2::frameMoves(e, kOtg0Frame)) return "OTG0 frame counter not moving";
    // the sink (AUX reads only)
    if ((r.capsRc = aux.rd(aux.c, kConnDpB.aux, 0, r.caps, 16))) return "DPCD caps 0x000 not readable over AUX2";
    if (r.caps[0xe] & 0x80) {
        r.extUsed = 1;
        if ((r.extRc = aux.rd(aux.c, kConnDpB.aux, 0x2200, r.ext, 16))) return "DPCD extended caps 0x2200 not readable";
    }
    if (aux.rd(aux.c, kConnDpB.aux, 0x100, r.link0, 2)) return "DPCD 0x100 (link configuration) not readable";
    phxdpl::Sink s{};
    if (!sinkOf(r, &s)) return "DPCD caps not a DP sink's";
    if ((r.link0[0] != phxdpl::kHbr2 && r.link0[0] != phxdpl::kHbr3) || (r.link0[1] & 0x1f) != 4)
        return "the link now is not HBR2 / HBR3 x4 (DPCD 0x100 / 0x101)";
    r.target = phxdpl::Link{r.sel == kSelHbr3 ? phxdpl::kHbr3 : phxdpl::kHbr2, 4};
    if (s.maxLanes < 4 || s.maxRate < r.target.rate) return "the sink cannot take this link (DPCD max rate / lanes)";
    if (phxdpl::linkKbps(r.target) < static_cast<uint64_t>(r.pclkKhz) * r.bpp) return "the mode does not fit this link at its colour depth";
    if (phxdpl::linkKbps(phxdpl::Link{phxdpl::kHbr2, 4}) < static_cast<uint64_t>(r.pclkKhz) * r.bpp)
        return "the mode does not fit the HBR2 fall-back";
    return nullptr;
}

// ---- the run's Io for the trainer ----
struct Ctx { const Env *e; State *st; Run *r; Aux aux; };
inline uint32_t cRd(void *c, uint32_t reg) { Ctx &x = *static_cast<Ctx *>(c); return rd(*x.e, reg); }
inline void cWr(void *c, uint32_t reg, uint32_t v) {
    Ctx &x = *static_cast<Ctx *>(c);
    if (x.st->halted) { x.r->blocked++; return; }
    if (!writeOk(reg)) { x.r->strayWr++; return; }
    x.e->io.wr(x.e->io.c, reg, v);
    x.r->mmioWr++;
    guard(*x.e, *x.st, *x.r);
}
inline bool txCheck(const uint32_t *c, const void *, const char **why) { return phxdpl::txAllowed(c, kConnDpB, why); }
inline bool pingCheck(const uint32_t *c, const void *, const char **why) {
    if (c[0] != kPingHdr) { *why = "not QUERY_FEATURE_CAPS"; return false; }
    for (unsigned i = 1; i < phxdmub::kCmdDw; i++) if (c[i]) { *why = "QUERY_FEATURE_CAPS payload not zero"; return false; }
    return true;
}
inline int cDmub(void *c, const uint32_t cmd[16]) {
    Ctx &x = *static_cast<Ctx *>(c);
    if (x.st->halted) { x.r->blocked++; return -1; }
    const char *w = nullptr;
    const int rc = phxdmub::submitWith(x.e->io, x.st->dmub, x.e->vramBytes, cmd, txCheck, nullptr, &w);
    x.r->ringRc = rc;
    if (rc != phxdmub::kOk) { x.r->ringWhy = w ? w : phxdmub::rcName(rc); return -1; }
    x.r->dmubSent++;
    return guard(*x.e, *x.st, *x.r) ? 0 : -1;
}
inline int cAuxRd(void *c, uint32_t a, uint8_t *b, unsigned n) {
    Ctx &x = *static_cast<Ctx *>(c);
    x.r->auxRdN++;
    const int rc = x.aux.rd(x.aux.c, kConnDpB.aux, a, b, n);
    if (rc) x.r->auxErr++;
    return rc;
}
inline int cAuxWr(void *c, uint32_t a, const uint8_t *b, unsigned n) {
    Ctx &x = *static_cast<Ctx *>(c);
    if (x.st->halted) { x.r->blocked++; return -1; }
    if (!phxdpl::dpcdWriteAllowed(a, n)) { x.r->dpcdStray++; return -1; }
    x.r->auxWrN++;
    const int rc = x.aux.wr(x.aux.c, kConnDpB.aux, a, b, n);
    if (rc) x.r->auxErr++;
    return rc;
}
inline void cDelay(void *c, uint32_t us) { Ctx &x = *static_cast<Ctx *>(c); x.e->io.delayUs(x.e->io.c, us); }

// Wait for DP_VID_STREAM_STATUS to read `on`, up to maxUs in 10 us steps; returns the time taken or ~0u.
inline uint32_t waitStream(phxdpl::Ctx &x, bool on, uint32_t maxUs) {
    for (uint32_t t = 0; t <= maxUs; t += 10) {
        if (((phxdpl::R(x, kR.vidStream) & phxdpl::kStreamStatus) != 0) == on) return t;
        phxdpl::D(x, 10);
    }
    return ~0u;
}
// Stream off (enc1_stream_encoder_dp_blank, the GOP's DIS_DEFER kept): ENABLE 0, wait STATUS 0, steer FIFO reset.
inline int streamOff(phxdpl::Ctx &x, Run &r) {
    phxdpl::W(x, kR.vidStream, kGopVidStream & ~phxdpl::kStreamEnable);
    r.offUs = waitStream(x, false, 102000);
    phxdpl::W(x, kR.steer, kGopSteer | phxdpl::kSteerReset);
    return r.offUs == ~0u ? phxdpl::kErrStream : phxdpl::kOk;
}
// Stream on after training (enc32_stream_encoder_dp_unblank, one pixel per clock): FIFO pulse, M/N for the new link rate,
// steer FIFO, FIFO pulse, ENABLE, wait STATUS 1.
inline int streamOn(phxdpl::Ctx &x, Run &r, phxdpl::Link l) {
    int rc = phxdpl::fifoResetPulse(x);
    if (rc) return rc;
    r.wantVidM = phxdpl::vidM1(r.pclkKhz, l.rate);
    phxdpl::W(x, kR.vidTiming, 0);
    phxdpl::W(x, kR.vidN, kGopVidN);
    phxdpl::W(x, kR.vidM, r.wantVidM);
    phxdpl::W(x, kR.vidTiming, kGopVidTiming);
    phxdpl::W(x, kR.steer, kGopSteer | phxdpl::kSteerReset);
    phxdpl::W(x, kR.steer, kGopSteer);
    if ((rc = phxdpl::fifoResetPulse(x))) return rc;
    phxdpl::W(x, kR.vidStream, kGopVidStream);
    r.onUs = waitStream(x, true, 200000);
    return r.onUs == ~0u ? phxdpl::kErrStream : phxdpl::kOk;
}
// One link bring-up at l: link setup, ENABLE, training (the transmitter was disabled before).
inline int linkUp(phxdpl::Ctx &x, const phxdpl::Sink &s, phxdpl::Link l) {
    phxdpl::linkSetup(x, l);
    int rc = phxdpl::phyOn(x, l);
    if (rc == phxdpl::kOk) rc = phxdpl::trainOnce(x, s, l);
    return rc;
}

// ---- the run ----
inline int run(const Env &e, State &st, Run &r, int sel, int restartDone, const Aux &aux) {
    const int failed = r.failed;
    const uint32_t att = r.attempts + 1, refs = r.refusals, passes = r.passes, fb = r.fellBack;
    const char *lastRef = r.lastRefusal;
    r = Run{};
    r.failed = failed; r.attempts = att; r.refusals = refs; r.passes = passes; r.fellBack = fb; r.lastRefusal = lastRef;
    r.sel = sel; r.pipe0Same = 1;
    const char *g = gates(e, st, r, restartDone, aux);
    if (g) return refuse(r, g);
    if (sel == kSelSurvey) { r.res = kSurveyed; r.why = "gates pass (reads only, nothing written)"; return kSurveyed; }

    // 1. ping: the ring and this firmware answer before the picture is touched
    r.phase = 1;
    uint32_t ping[phxdmub::kCmdDw] = {kPingHdr};
    const char *w = nullptr;
    r.ringRc = phxdmub::submitWith(e.io, st.dmub, e.vramBytes, ping, pingCheck, nullptr, &w);
    if (r.ringRc == phxdmub::kTimeout) { r.ringWhy = w; return end(r, kTimeout, "the ping timed out: debug.phxdmub_dead latched; the link was not touched"); }
    if (r.ringRc != phxdmub::kOk) { r.ringWhy = w ? w : phxdmub::rcName(r.ringRc); return end(r, kNotSent, "the ping was not sent; the link was not touched"); }
    r.dmubSent++;
    r.pingUs = st.dmub.lastUs; r.pingReply = st.dmub.reply[0];
    if (!guard(e, st, r)) return end(r, kHalted, "pipe 0 changed after the ping: halted until reboot");

    phxdpl::Sink s{};
    sinkOf(r, &s);
    Ctx cx{&e, &st, &r, aux};
    const phxdpl::Io io{&cx, cRd, cWr, cDmub, cAuxRd, cAuxWr, cDelay};
    phxdpl::Opts o;
    o.onePpc = true;       // the GOP's convention: DIG FIFO 0x1c/0x1d in the FIFO pulse
    o.attempts = 1;
    phxdpl::Ctx x{io, kConnDpB, o, &r.rep};

    // 2. stream off, link off (D3, TRANSMITTER_CONTROL DISABLE, link registers 0)
    r.phase = 2;
    int rc = streamOff(x, r);
    if (st.halted) return end(r, kHalted, "pipe 0 changed while the stream went off: halted, nothing more (dark until reboot)");
    if (rc) return end(r, kPostFail, "DP1 stream status did not drop within 102 ms; the link was left on");
    rc = phxdpl::linkOff(x);
    if (st.dmub.dead) return end(r, kTimeout, "TRANSMITTER_CONTROL DISABLE timed out: debug.phxdmub_dead latched (dark until reboot)");
    if (st.halted) return end(r, kHalted, "pipe 0 changed at the link off: halted (dark until reboot)");
    if (rc == phxdpl::kErrDmub || rc == phxdpl::kErrArg) return end(r, kNotSent, "TRANSMITTER_CONTROL DISABLE not sent (dark until reboot)");

    // 3. train at the target; on failure once more at HBR2 x4
    r.phase = 3;
    r.trc = linkUp(x, s, r.target);
    r.done = r.target;
    if (st.dmub.dead) return end(r, kTimeout, "a TRANSMITTER_CONTROL timed out: debug.phxdmub_dead latched (dark until reboot)");
    if (st.halted) return end(r, kHalted, "pipe 0 changed during the training: halted (dark until reboot)");
    bool fell = false;
    if (r.trc != phxdpl::kOk) {
        if (r.trc == phxdpl::kErrDmub || r.trc == phxdpl::kErrArg) return end(r, kNotSent, "a TRANSMITTER_CONTROL was not sent (dark until reboot)");
        phxdpl::linkOff(x);
        if (st.dmub.dead || st.halted) return end(r, st.halted ? kHalted : kTimeout, "the link off before the fall-back failed (dark until reboot)");
        phxdpl::Ctx x2{io, kConnDpB, o, &r.rep2};
        phxdpl::D(x2, 10000);
        r.done = phxdpl::Link{phxdpl::kHbr2, 4};
        r.trc2 = linkUp(x2, s, r.done);
        if (st.dmub.dead || st.halted) return end(r, st.halted ? kHalted : kTimeout, "the HBR2 fall-back stopped (dark until reboot)");
        if (r.trc2 != phxdpl::kOk) {
            phxdpl::linkOff(x2);
            return end(r, kTrainFail, "training failed at the target and at the HBR2 fall-back: the link is off (dark until reboot)");
        }
        fell = true;
    }

    // 4. back end as the gate read it, stream on
    r.phase = 4;
    if (phxdpl::R(x, kR.beCntl) != kGopBeCntl) { phxdpl::W(x, kR.beCntl, kGopBeCntl); r.beRestored = 1; }
    rc = streamOn(x, r, r.done);
    if (st.halted) return end(r, kHalted, "pipe 0 changed while the stream came back: halted");
    if (rc == phxdpl::kErrFifo) return end(r, kPostFail, "DIG1 FIFO reset did not complete");
    if (rc) return end(r, kPostFail, "DP1 stream status did not rise within 200 ms");

    // 5. post: back end on, link trained, stream up, M within 1 %, pipe 0 as before, frames moving
    r.phase = 5;
    phxdpl::D(x, 30000);
    r.postBeCntl = rd(e, kR.beCntl); r.postBeEn = rd(e, kR.beEn); r.postLink = rd(e, kR.linkCntl);
    r.postStream = rd(e, kR.vidStream); r.postVidM = rd(e, kR.vidM);
    if (r.strayWr || r.dpcdStray) return end(r, kPostFail, "a write off the lists was dropped (stray): not the planned sequence");
    if (r.postBeCntl != kGopBeCntl || !(r.postBeEn & 1u)) return end(r, kPostFail, "DIG1 back end not back on as before");
    if (!(r.postLink & phxdpl::kLinkTrained) || !(r.postStream & phxdpl::kStreamStatus)) return end(r, kPostFail, "DP1 link / stream not up");
    const uint32_t m = r.postVidM & 0xffffff, lo = r.wantVidM - r.wantVidM / 100, hi = r.wantVidM + r.wantVidM / 100;
    if (m < lo || m > hi) return end(r, kPostFail, "DP_VID_M not within 1 % of the new link rate's");
    if (!guard(e, st, r)) return end(r, kHalted, "pipe 0 changed after the retrain: halted");
    if (!phxp2::frameMoves(e, kOtg0Frame)) { st.halted = 1; r.pipe0Same = 0; r.pipeWhy = "OTG0 frame counter stopped"; return end(r, kHalted, "OTG0 stopped"); }
    return fell ? end(r, kFellBack, "the target failed; retrained at HBR2 x4 and the stream is back")
                : end(r, kPass, "retrained at the target, stream back, pipe 0 unchanged");
}

inline int stat(const Run &r, char *b, int n) {
    int o = 0;
#define PHXDPB_OUT(...) do { if (o < n - 1) { const int k_ = snprintf(b + o, static_cast<size_t>(n - o), __VA_ARGS__); o += k_ > 0 ? k_ : 0; if (o > n - 1) o = n - 1; } } while (0)
    static const char *const ph[] = {"gates", "ping", "stream-off", "training", "stream-on", "post"};
    PHXDPB_OUT("result=%s sel=%d phase=%s why=%s\n", kResName[r.res >= 0 && r.res <= kPostFail ? r.res : 0], r.sel,
               ph[r.phase >= 0 && r.phase <= 5 ? r.phase : 0], r.why ? r.why : "-");
    PHXDPB_OUT("boot: attempts=%u passes=%u fell_back=%u refusals=%u failed=%d last_refusal=%s\n", r.attempts, r.passes,
               r.fellBack, r.refusals, r.failed, r.lastRefusal ? r.lastRefusal : "-");
    PHXDPB_OUT("gates: hpd=0x%x be_cntl=0x%08x be_en=0x%x fe=0x%08x fifo=0x%08x link=0x%x stream=0x%x steer=0x%08x timing=0x%x "
               "n=0x%x m=0x%x pixfmt=0x%08x config=%u | otg0 0x%x/0x%x %u Hz pclk=%u kHz bpp=%u dto=%u/%u | dmcub cntl=0x%x "
               "scratch0=0x%x scratch1=0x%08x otg1=0x%x\n", r.hpd, r.beCntl, r.beEn, r.feCntl, r.fifo, r.linkCntl, r.vidStream,
               r.steer, r.vidTiming, r.vidN, r.vidM0, r.pixFmt, r.config, r.otgH, r.otgV, r.hz, r.pclkKhz, r.bpp, r.dtoPhase,
               r.dtoMod, r.cntl, r.scr0, r.scr1, r.otg1Ctl);
    PHXDPB_OUT("dpcd: caps");
    for (int i = 0; i < 16; i++) PHXDPB_OUT(" %02x", r.caps[i]);
    PHXDPB_OUT(" | ext(%d)", r.extUsed);
    for (int i = 0; i < 16; i++) PHXDPB_OUT(" %02x", r.ext[i]);
    PHXDPB_OUT(" | link_now 0x%02x/0x%02x target 0x%02x x%u done 0x%02x x%u\n", r.link0[0], r.link0[1], r.target.rate,
               r.target.lanes, r.done.rate, r.done.lanes);
    PHXDPB_OUT("ping: rc=%d(%s) us=%u reply=0x%08x | stream off_us=%u on_us=%u be_restored=%u\n", r.ringRc,
               r.ringWhy ? r.ringWhy : phxdmub::rcName(r.ringRc), r.pingUs, r.pingReply, r.offUs, r.onUs, r.beRestored);
    for (int k = 0; k < 2; k++) {
        const phxdpl::Report &p = k ? r.rep2 : r.rep;
        if (k && !r.trc && !p.crLoops) break;
        PHXDPB_OUT("train%s: rc=%s cr_loops=%u eq_loops=%u vs=%u pe=%u pattern=0x%02x status=%02x %02x %02x %02x %02x %02x "
                   "after=%02x %02x %02x %02x %02x %02x delay_us=%u\n", k ? "(hbr2 fall-back)" : "", phxdpl::rcName(k ? r.trc2 : r.trc),
                   p.crLoops, p.eqLoops, p.vs, p.pe, p.pattern, p.status[0], p.status[1], p.status[2], p.status[3], p.status[4],
                   p.status[5], p.after[0], p.after[1], p.after[2], p.after[3], p.after[4], p.after[5], p.delayUs);
    }
    PHXDPB_OUT("io: mmio_wr=%u dmub_sent=%u aux_rd=%u aux_wr=%u aux_err=%u stray_wr=%u dpcd_stray=%u blocked=%u\n", r.mmioWr,
               r.dmubSent, r.auxRdN, r.auxWrN, r.auxErr, r.strayWr, r.dpcdStray, r.blocked);
    PHXDPB_OUT("post: be_cntl=0x%08x be_en=0x%x link=0x%x stream=0x%x vid_m=0x%x want=0x%x | pipe0 same=%d%s%s\n", r.postBeCntl,
               r.postBeEn, r.postLink, r.postStream, r.postVidM, r.wantVidM, r.pipe0Same, r.pipeWhy ? " changed=" : "",
               r.pipeWhy ? r.pipeWhy : "");
#undef PHXDPB_OUT
    return o;
}

}  // namespace phxdpb
