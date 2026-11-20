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

static int phxP0OdmSysctl SYSCTL_HANDLER_ARGS {
    int v = gOdmLast;
    const int err = sysctl_handle_int(oidp, &v, 0, req);
    if (err || !req->newptr) return err;
    if (proc_suser(req->p)) return EPERM;
    if (!p2Up() || !gPhx || !gPhx->scanLockForEngines()) return ENXIO;
    if (!OSCompareAndSwap(0, 1, &gP2Busy)) return EBUSY;
    if (!p2I2cLock(gPhx, 1)) { OSCompareAndSwap(1, 0, &gP2Busy); return EBUSY; }
    gOdmLast = v;
    IOLockLock(gPhx->scanLockForEngines());
    const phxdpb::Aux aux{nullptr, dplAuxRd, dplAuxWr};
    const int r = phxodm::run(p2Env(), gP2, gOdm, v, gDmubRs.done, aux, kOdmTo, kOdmBack);
    gP0OdmActive = gOdm.at240 ? 1 : 0;
    IOLockUnlock(gPhx->scanLockForEngines());
    p2I2cLock(gPhx, 0);
    OSCompareAndSwap(1, 0, &gP2Busy);
    IOLog("PhxBringup: debug.phxp0odm=%d: %s phase %d: %s (step %u line %u, hz %u, train %s, back %u)\n", v,
          phxodm::kResName[r >= 0 && r <= phxodm::kTimeout ? r : 0], gOdm.phase, gOdm.why ? gOdm.why : "-", gOdm.step, gOdm.line,
          gOdm.postHz, phxdpl::rcName(gOdm.trc), gOdm.backRes);
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
