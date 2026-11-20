// SPDX-License-Identifier: MIT
// phx_dpbretrain.cpp: kext b129, the 240 Hz plan's D1 / D2: debug.phxdpb_retrain + debug.phxdpb_retrain_stat.
//   debug.phxdpb_retrain (int): root writes 1 = survey (reads + DPCD reads only), 2 = retrain the GOP's DP B link at HBR2 x4,
//     3 = at HBR3 x4 (PhxDpbRetrain.h). Reads return the last selector (-1 = never); a read touches no register. Nothing
//     runs at boot or until root writes it (inert).
//   debug.phxdpb_retrain_stat (string): the last run.
// Under gP2Busy (the display lock of the pipe-0 / pipe-1 engines) and the EDID/AUX lock (p2I2cLock) for the whole run; the
// AUX path is phx_dplink.cpp's (dplAuxRd / dplAuxWr: PhxDdc.h through the AUX gate). Part of the PhxBringup.cpp translation
// unit, included after phx_dplink.cpp; not compiled on its own.

#include "PhxDpbRetrain.h"

static int gDpbLast = -1;
static phxdpb::Run gDpb;

static int phxDpbRetrainSysctl SYSCTL_HANDLER_ARGS {
    int v = gDpbLast;
    const int err = sysctl_handle_int(oidp, &v, 0, req);
    if (err || !req->newptr) return err;
    if (proc_suser(req->p)) return EPERM;
    if (!p2Up()) return ENXIO;
    if (!OSCompareAndSwap(0, 1, &gP2Busy)) return EBUSY;
    if (!p2I2cLock(gPhx, 1)) { OSCompareAndSwap(1, 0, &gP2Busy); return EBUSY; }
    gDpbLast = v;
    const phxdpb::Aux aux{nullptr, dplAuxRd, dplAuxWr};
    const int r = phxdpb::run(p2Env(), gP2, gDpb, v, gDmubRs.done, aux);
    p2I2cLock(gPhx, 0);
    OSCompareAndSwap(1, 0, &gP2Busy);
    IOLog("PhxBringup: debug.phxdpb_retrain=%d: %s phase %d: %s (target 0x%02x done 0x%02x, cr %u eq %u, m 0x%x, aux_stray %u)\n", v,
          phxdpb::kResName[r >= 0 && r <= phxdpb::kPostFail ? r : 0], gDpb.phase, gDpb.why ? gDpb.why : "-", gDpb.target.rate,
          gDpb.done.rate, gDpb.rep.crLoops, gDpb.rep.eqLoops, gDpb.postVidM, gDplAuxStray);
    return r == phxdpb::kPass || r == phxdpb::kSurveyed || r == phxdpb::kFellBack ? 0 : r == phxdpb::kRefused ? EINVAL
         : r == phxdpb::kTimeout ? ETIMEDOUT : EIO;
}
SYSCTL_PROC(_debug, OID_AUTO, phxdpb_retrain, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, nullptr, 0, phxDpbRetrainSysctl, "I",
            "b129 root: retrain the GOP's DP B link in place: 1 survey, 2 HBR2 x4, 3 HBR3 x4 (falls back to HBR2); reads the last selector");

static int phxDpbRetrainStatSysctl SYSCTL_HANDLER_ARGS {
    (void)oidp; (void)arg1; (void)arg2;
    if (req->newptr) return EPERM;
    static constexpr int kN = 2048;
    char *b = static_cast<char *>(IOMalloc(kN));
    if (!b) return ENOMEM;
    int len = 0;
    if (!gDpb.attempts) len = snprintf(b, kN, "never run (root: sysctl -w debug.phxdpb_retrain=1 survey, 2 HBR2, 3 HBR3)\n");
    else len = phxdpb::stat(gDpb, b, kN);
    if (len < 0) len = 0;
    if (len > kN - 1) len = kN - 1;
    b[len] = 0;
    const int e = SYSCTL_OUT(req, b, static_cast<size_t>(len) + 1);
    IOFree(b, kN);
    return e;
}
SYSCTL_PROC(_debug, OID_AUTO, phxdpb_retrain_stat, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, nullptr, 0, phxDpbRetrainStatSysctl, "A",
            "b129 the last debug.phxdpb_retrain: result, phase, gates, DPCD caps, ping, training, writes, post checks");
