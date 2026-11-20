// SPDX-License-Identifier: MIT
// phx_p0odm.cpp: kext b131, the 240 Hz plan's E1: debug.phxp0odm + debug.phxp0odm_stat (PhxP0Odm.h).
//   debug.phxp0odm (int): root writes 1 = survey, 2 = 1440p 240 Hz (ODM 2:1 + DSC + FEC, HBR3), 3 = back to 144. Reads return
//     the last selector (-1 = never). Inert until written.
// Under gP2Busy, the EDID/AUX lock and scanLock (no flip while the plan runs; the flip path then follows gP0OdmActive).
// Part of the PhxBringup.cpp translation unit, included after phx_dpbretrain.cpp; not compiled on its own.

#include "PhxP0Odm.h"
#include "phx_p0odm_1440p240.inc"
#include "phx_p0odm_1440p144odm.inc"

static int gOdmLast = -1;
static phxodm::Run gOdm;
static const phxodm::Plan kOdmTo = {kPhxP0odm_1440p240, sizeof kPhxP0odm_1440p240 / sizeof kPhxP0odm_1440p240[0], "1440p240", 240, 0x652, 0x1e};
static const phxodm::Plan kOdmBack = {kPhxP0odm_1440p144odm, sizeof kPhxP0odm_1440p144odm / sizeof kPhxP0odm_1440p144odm[0], "1440p144odm", 144, 0x5eb, 0x14};

// One engine run under gP2Busy, the AUX lock and scanLock. -1 = not run (busy / not ready; *err set).
static int odmRunLocked(int sel, int *err) {
    *err = 0;
    if (!p2Up() || !gPhx || !gPhx->scanLockForEngines()) { *err = ENXIO; return -1; }
    if (!OSCompareAndSwap(0, 1, &gP2Busy)) { *err = EBUSY; return -1; }
    if (!p2I2cLock(gPhx, 1)) { OSCompareAndSwap(1, 0, &gP2Busy); *err = EBUSY; return -1; }
    gOdmLast = sel;
    IOLockLock(gPhx->scanLockForEngines());
    const phxdpb::Aux aux{nullptr, dplAuxRd, dplAuxWr};
    const int r = phxodm::run(p2Env(), gP2, gOdm, sel, gDmubRs.done, aux, kOdmTo, kOdmBack);
    gP0OdmActive = gOdm.at240 ? 1 : 0;
    IOLockUnlock(gPhx->scanLockForEngines());
    p2I2cLock(gPhx, 0);
    OSCompareAndSwap(1, 0, &gP2Busy);
    IOLog("PhxBringup: phxp0odm %d: %s phase %d: %s (step %u line %u, hz %u, train %s, back %u)\n", sel,
          phxodm::kResName[r >= 0 && r <= phxodm::kTimeout ? r : 0], gOdm.phase, gOdm.why ? gOdm.why : "-", gOdm.step, gOdm.line,
          gOdm.postHz, phxdpl::rcName(gOdm.trc), gOdm.backRes);
    return r;
}

// b134: System Settings' 240 Hz (PhxAccel head 0 -> phxPipe0SetMode, phx_pipe0.cpp): true = handled here (*ret set).
//   240 from 60 / 120: the pipe-0 engine to 144 first (the start the ODM plan is tested from), then the ODM plan;
//   240 from 144 (the GOP's, or the back plan's after an earlier 240): the ODM plan;  144 at 240: the back plan;
//   60 / 120 at 240 or on the back plan's 144: refused (nothing changes; the 1 ppc plans need a reboot for now).
static bool p0OdmRoute(phx_pipe0_setmode *m, IOReturn *ret) {
    const uint32_t t = m->target_hz;
    if (t != 240 && !gOdm.at240 && !gOdm.linux144) return false;   // the pipe-0 engine's own business
    int err = 0, r = -1;
    *ret = kIOReturnSuccess;
    if (t == 240) {
        if (gOdm.at240) { m->result = phxp0::kSame; m->cur_hz = 240; strlcpy(m->why, "already at 240 Hz", sizeof m->why); return true; }
        if (!gOdm.linux144 && phxp2::rd(p2Env(), phxodm::kOtg0V) != kOdmBack.vTotal) {   // 60 / 120 -> 144 first
            phxp0::MacOut o{};
            const int r0 = p0MacLocked(144, &o, &err);
            if (r0 != phxp0::kDone) {
                m->result = r0 < 0 ? -1 : r0; m->stage = o.stage; m->cur_hz = o.curHz;
                snprintf(m->why, sizeof m->why, "240: the step to 144 first: %s", r0 < 0 ? "busy" : gP0.why);
                *ret = r0 < 0 ? (err == EBUSY ? kIOReturnBusy : kIOReturnNotReady) : kIOReturnSuccess;
                return true;
            }
        }
        r = odmRunLocked(phxodm::kSel240, &err);
    } else if (gOdm.at240 && t == 144) {
        r = odmRunLocked(phxodm::kSelBack, &err);
    } else if (t == 144) {   // linux144 and 144: the same mode
        m->result = phxp0::kSame; m->cur_hz = 144; strlcpy(m->why, "at 144 (after 240)", sizeof m->why); return true;
    } else {
        m->result = phxp0::kRefused; m->cur_hz = gOdm.at240 ? 240 : 144;
        strlcpy(m->why, "60 / 120 after 240 Hz need a reboot (b134)", sizeof m->why);
        return true;
    }
    if (r < 0) { strlcpy(m->why, err == EBUSY ? "busy" : "not ready", sizeof m->why); *ret = err == EBUSY ? kIOReturnBusy : kIOReturnNotReady; return true; }
    m->result = r == phxodm::kDone ? phxp0::kDone : r == phxodm::kRefused ? phxp0::kRefused
              : r == phxodm::kFellBack ? phxp0::kFellBack : phxp0::kDark;
    m->cur_hz = gOdm.at240 ? 240 : (r == phxodm::kRefused ? 0 : 144);
    snprintf(m->why, sizeof m->why, "%s: %s", phxodm::kResName[r >= 0 && r <= phxodm::kTimeout ? r : 0], gOdm.why ? gOdm.why : "-");
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
    return r == phxodm::kDone || r == phxodm::kSurveyed ? 0 : r == phxodm::kRefused ? EINVAL : r == phxodm::kTimeout ? ETIMEDOUT : EIO;
}
SYSCTL_PROC(_debug, OID_AUTO, phxp0odm, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, nullptr, 0, phxP0OdmSysctl, "I",
            "b131 root: 1 survey, 2 = 1440p 240 Hz (ODM 2:1, DSC, FEC, HBR3), 3 = back to 144; reads the last selector");

static int phxP0OdmStatSysctl SYSCTL_HANDLER_ARGS {
    (void)oidp; (void)arg1; (void)arg2;
    if (req->newptr) return EPERM;
    static constexpr int kN = 2048;
    char *b = static_cast<char *>(IOMalloc(kN));
    if (!b) return ENOMEM;
    int len = gOdm.attempts ? phxodm::stat(gOdm, b, kN) : snprintf(b, kN, "never run (root: sysctl -w debug.phxp0odm=1 survey, 2 = 240 Hz, 3 = back)\n");
    if (len < 0) len = 0;
    if (len > kN - 1) len = kN - 1;
    b[len] = 0;
    const int e = SYSCTL_OUT(req, b, static_cast<size_t>(len) + 1);
    IOFree(b, kN);
    return e;
}
SYSCTL_PROC(_debug, OID_AUTO, phxp0odm_stat, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, nullptr, 0, phxP0OdmStatSysctl, "A",
            "b131 the last debug.phxp0odm: result, phase, gates, the step a failure stopped at, writes, waits, training, post");
