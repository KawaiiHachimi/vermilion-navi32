// SPDX-License-Identifier: MIT
// phx_p0odm.cpp: the pipe-0 replay engine's glue (PhxP0Odm.h): debug.phxp0odm + debug.phxp0odm_stat, and System Settings'
// 1440p rates through phxPipe0SetMode (p0OdmRoute).
//   debug.phxp0odm (int): root writes 1 = survey, a target rate (60, 120, 144, 240), or the b131 aliases 2 = 240, 3 = 144.
//     Reads return the last selector (-1 = never). Inert until written.
// Under gP2Busy, the EDID/AUX lock and scanLock (no flip while a plan runs; the flip path then follows gP0OdmActive).
// b135: one set of Linux replays for 60 / 120 / 144 / 240 through Linux's 144 (capture v3), replacing b134's 240-only
// routing (60 / 120 refused after 240, a DAL refusal latched).
// Part of the PhxBringup.cpp translation unit, included after phx_dpbretrain.cpp; not compiled on its own.

#include "PhxP0Odm.h"
#include "phx_p0odm_1440p240.inc"        // b133: the entry, macOS's 144 (one pixel per clock) -> 240 (capture v2)
#include "phx_p0odm_1440p60_to.inc"      // b135: capture v3, Linux's 144 hub -> 60 and back
#include "phx_p0odm_1440p60_from.inc"
#include "phx_p0odm_1440p120_to.inc"
#include "phx_p0odm_1440p120_from.inc"
#include "phx_p0odm_1440p240_to.inc"
#include "phx_p0odm_1440p240_from.inc"

#define PHX_ODM_PLAN(sym, nm, hz, v, rate) {sym, sizeof sym / sizeof sym[0], nm, hz, v, rate}
static const phxodm::Plan kOdmEntry = PHX_ODM_PLAN(kPhxP0odm_1440p240, "1440p240-entry", 240, 0x652, 0x1e);
static const phxodm::Plan kOdm60To = PHX_ODM_PLAN(kPhxP0odm_1440p60_to, "1440p60-to", 60, 0x5c8, 0x0a);
static const phxodm::Plan kOdm60From = PHX_ODM_PLAN(kPhxP0odm_1440p60_from, "1440p60-from", 144, 0x5eb, 0x14);
static const phxodm::Plan kOdm120To = PHX_ODM_PLAN(kPhxP0odm_1440p120_to, "1440p120-to", 120, 0x5f4, 0x14);
static const phxodm::Plan kOdm120From = PHX_ODM_PLAN(kPhxP0odm_1440p120_from, "1440p120-from", 144, 0x5eb, 0x14);
static const phxodm::Plan kOdm240To = PHX_ODM_PLAN(kPhxP0odm_1440p240_to, "1440p240-to", 240, 0x652, 0x1e);
static const phxodm::Plan kOdm240From = PHX_ODM_PLAN(kPhxP0odm_1440p240_from, "1440p240-from", 144, 0x5eb, 0x14);
#undef PHX_ODM_PLAN
// Linux's totals per rate (capture v3 regs.txt); the link rate Linux trained for each.
static const phxodm::Mode kOdmModes[] = {
    {60, 0x5c8, 0, 0x0a, &kOdm60To, &kOdm60From},
    {120, 0x5f4, 0, 0x14, &kOdm120To, &kOdm120From},
    {144, 0x5eb, 0, 0x14, nullptr, nullptr},   // the hub
    {240, 0x652, 1, 0x1e, &kOdm240To, &kOdm240From},
};
static const phxodm::Set kOdmSet = {kOdmModes, sizeof kOdmModes / sizeof kOdmModes[0], 2, &kOdmEntry, 3};

static int gOdmLast = -1;
static phxodm::Run gOdm;   // where = kMac until the first run lands (cur is read only in kLinux)

// One engine run under gP2Busy, the AUX lock and scanLock. -1 = not run (busy / not ready; *err set).
static int odmRunLocked(int sel, int *err) {
    *err = 0;
    if (!p2Up() || !gPhx || !gPhx->scanLockForEngines()) { *err = ENXIO; return -1; }
    if (!OSCompareAndSwap(0, 1, &gP2Busy)) { *err = EBUSY; return -1; }
    if (!p2I2cLock(gPhx, 1)) { OSCompareAndSwap(1, 0, &gP2Busy); *err = EBUSY; return -1; }
    gOdmLast = sel;
    IOLockLock(gPhx->scanLockForEngines());
    const phxdpb::Aux aux{nullptr, dplAuxRd, dplAuxWr};
    const int r = phxodm::run(p2Env(), gP2, gOdm, sel, gDmubRs.done, aux, kOdmSet);
    gP0OdmActive = phxodm::odmActive(gOdm, kOdmSet) ? 1 : 0;
    IOLockUnlock(gPhx->scanLockForEngines());
    p2I2cLock(gPhx, 0);
    OSCompareAndSwap(1, 0, &gP2Busy);
    IOLog("PhxBringup: phxp0odm %d: %s phase %d now %u Hz: %s (plans %s,%s step %u line %u, hz %u, train %s, back %u, dal_resp 0x%x, "
          "dal_busy_retries %u)\n", sel, phxodm::kResName[r >= 0 && r <= phxodm::kResMax ? r : 0], gOdm.phase, phxodm::curHz(gOdm, kOdmSet),
          gOdm.why ? gOdm.why : "-", gOdm.plan1 ? gOdm.plan1 : "-", gOdm.plan2 ? gOdm.plan2 : "-", gOdm.step, gOdm.line, gOdm.postHz,
          phxdpl::rcName(gOdm.trc), gOdm.backRes, gOdm.dalResp, gDalBusyRetries);
    return r;
}

// System Settings' rate (PhxAccel head 0 -> phxPipe0SetMode, phx_pipe0.cpp): true = handled here (*ret set).
//   macOS's own state (the GOP's / the pipe-0 engine's): 60 / 120 / 144 stay with the pipe-0 engine; 240 = the pipe-0
//   engine to 144 first when needed (the entry plan's start), then the entry.
//   After the first 240 (Linux's state): every rate here, through Linux's 144.
static bool p0OdmRoute(phx_pipe0_setmode *m, IOReturn *ret) {
    const uint32_t t = m->target_hz;
    if (gOdm.where == phxodm::kMac && t != 240) return false;   // the pipe-0 engine's own business
    int err = 0;
    *ret = kIOReturnSuccess;
    if (gOdm.where == phxodm::kMac && phxp2::rd(p2Env(), phxodm::kOtg0V) != kOdmModes[2].vTotal) {   // 60 / 120 -> 144 first
        phxp0::MacOut o{};
        const int r0 = p0MacLocked(144, &o, &err);
        if (r0 != phxp0::kDone) {
            m->result = r0 < 0 ? -1 : r0; m->stage = o.stage; m->cur_hz = o.curHz;
            snprintf(m->why, sizeof m->why, "240: the step to 144 first: %s", r0 < 0 ? "busy" : gP0.why);
            *ret = r0 < 0 ? (err == EBUSY ? kIOReturnBusy : kIOReturnNotReady) : kIOReturnSuccess;
            return true;
        }
    }
    const int r = odmRunLocked(static_cast<int>(t), &err);
    if (r < 0) { strlcpy(m->why, err == EBUSY ? "busy" : "not ready", sizeof m->why); *ret = err == EBUSY ? kIOReturnBusy : kIOReturnNotReady; return true; }
    m->result = r == phxodm::kDone ? phxp0::kDone : r == phxodm::kSame ? phxp0::kSame : r == phxodm::kRefused ? phxp0::kRefused
              : r == phxodm::kFellBack ? phxp0::kFellBack : phxp0::kDark;
    m->cur_hz = gOdm.where == phxodm::kLinux ? phxodm::curHz(gOdm, kOdmSet) : 0;
    snprintf(m->why, sizeof m->why, "%s: %s", phxodm::kResName[r >= 0 && r <= phxodm::kResMax ? r : 0], gOdm.why ? gOdm.why : "-");
    return true;
}

static int phxP0OdmSysctl SYSCTL_HANDLER_ARGS {
    int v = gOdmLast;
    const int err = sysctl_handle_int(oidp, &v, 0, req);
    if (err || !req->newptr) return err;
    if (proc_suser(req->p)) return EPERM;
    int e2 = 0;
    const int r = odmRunLocked(v, &e2);
    if (r < 0) return e2;
    return r == phxodm::kDone || r == phxodm::kSurveyed || r == phxodm::kSame ? 0 : r == phxodm::kRefused ? EINVAL
         : r == phxodm::kTimeout ? ETIMEDOUT : EIO;
}
SYSCTL_PROC(_debug, OID_AUTO, phxp0odm, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, nullptr, 0, phxP0OdmSysctl, "I",
            "b135 root: 1 survey, or the 1440p rate 60 / 120 / 144 / 240 (2 = 240, 3 = 144); reads the last selector");

static int phxP0OdmStatSysctl SYSCTL_HANDLER_ARGS {
    (void)oidp; (void)arg1; (void)arg2;
    if (req->newptr) return EPERM;
    static constexpr int kN = 2048;
    char *b = static_cast<char *>(IOMalloc(kN));
    if (!b) return ENOMEM;
    int len = gOdm.attempts ? phxodm::stat(gOdm, kOdmSet, b, kN)
                            : snprintf(b, kN, "never run (root: sysctl -w debug.phxp0odm=1 survey, or 60 / 120 / 144 / 240)\n");
    if (len < 0) len = 0;
    if (len > kN - 1) len = kN - 1;
    b[len] = 0;
    const int e = SYSCTL_OUT(req, b, static_cast<size_t>(len) + 1);
    IOFree(b, kN);
    return e;
}
SYSCTL_PROC(_debug, OID_AUTO, phxp0odm_stat, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, nullptr, 0, phxP0OdmStatSysctl, "A",
            "b135 the replay engine: result, state (mac / linux, rate), the last failure, gates, writes, waits, training, post");
