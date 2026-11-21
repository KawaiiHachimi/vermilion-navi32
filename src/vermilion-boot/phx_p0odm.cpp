// SPDX-License-Identifier: MIT
// phx_p0odm.cpp: the pipe-0 replay engine's glue (PhxP0Odm.h): debug.phxp0odm + debug.phxp0odm_stat, and System Settings'
// 1440p rates through phxPipe0SetMode (p0OdmRoute).
//   debug.phxp0odm (int): root writes 1 = survey, a target rate (60, 120, 144, 240), or the b131 aliases 2 = 240, 3 = 144;
//     b137: the other EDID modes as height * 1000 + Hz (1080060 1920x1080@60, 1080120, 1024075 1280x1024@75, 768060,
//     600060, 480060, 480059 = 640x480@59.94). The other resolutions run under macOS's 2560x1440 desktop for now: the
//     monitor gets the real mode and shows the desktop's top-left corner (PhxAccel does not resize the desktop yet).
//     Reads return the last selector (-1 = never). Inert until written.
// Under gP2Busy, the EDID/AUX lock and scanLock (no flip while a plan runs; the flip path then follows gP0OdmActive).
// b135: one set of Linux replays for 60 / 120 / 144 / 240 through Linux's 144 (capture v3), replacing b134's 240-only
// routing (60 / 120 refused after 240, a DAL refusal latched).
// Part of the PhxBringup.cpp translation unit, included after phx_dpbretrain.cpp; not compiled on its own.

#include "PhxP0Odm.h"
#include "phx_p0odm_1440p240.inc"        // b133: the entry, macOS's 144 (one pixel per clock) -> 240 (capture v2)
// capture v3: Linux's 144 hub -> each mode and back (b135 the 1440p rates, b137 the other resolutions)
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

#define PHX_ODM_PLAN(sym, nm, hz, v, rate, lanes) {sym, sizeof sym / sizeof sym[0], nm, hz, v, rate, lanes}
#define PHX_ODM_BACK(sym, nm) PHX_ODM_PLAN(sym, nm, 144, 0x5eb, 0x14, 4)
static const phxodm::Plan kOdmEntry = PHX_ODM_PLAN(kPhxP0odm_1440p240, "1440p240-entry", 240, 0x652, 0x1e, 4);
static const phxodm::Plan kOdm60To = PHX_ODM_PLAN(kPhxP0odm_1440p60_to, "1440p60-to", 60, 0x5c8, 0x0a, 4);
static const phxodm::Plan kOdm60From = PHX_ODM_BACK(kPhxP0odm_1440p60_from, "1440p60-from");
static const phxodm::Plan kOdm120To = PHX_ODM_PLAN(kPhxP0odm_1440p120_to, "1440p120-to", 120, 0x5f4, 0x14, 4);
static const phxodm::Plan kOdm120From = PHX_ODM_BACK(kPhxP0odm_1440p120_from, "1440p120-from");
static const phxodm::Plan kOdm240To = PHX_ODM_PLAN(kPhxP0odm_1440p240_to, "1440p240-to", 240, 0x652, 0x1e, 4);
static const phxodm::Plan kOdm240From = PHX_ODM_BACK(kPhxP0odm_1440p240_from, "1440p240-from");
static const phxodm::Plan kOdm1080p60To = PHX_ODM_PLAN(kPhxP0odm_1080p60_to, "1080p60-to", 60, 0x464, 0x06, 4);
static const phxodm::Plan kOdm1080p60From = PHX_ODM_BACK(kPhxP0odm_1080p60_from, "1080p60-from");
static const phxodm::Plan kOdm1080p120To = PHX_ODM_PLAN(kPhxP0odm_1080p120_to, "1080p120-to", 120, 0x464, 0x0a, 4);
static const phxodm::Plan kOdm1080p120From = PHX_ODM_BACK(kPhxP0odm_1080p120_from, "1080p120-from");
static const phxodm::Plan kOdm1024p75To = PHX_ODM_PLAN(kPhxP0odm_1024p75_to, "1280x1024p75-to", 75, 0x429, 0x06, 4);
static const phxodm::Plan kOdm1024p75From = PHX_ODM_BACK(kPhxP0odm_1024p75_from, "1280x1024p75-from");
static const phxodm::Plan kOdm768p60To = PHX_ODM_PLAN(kPhxP0odm_768p60_to, "1024x768p60-to", 60, 0x325, 0x06, 2);
static const phxodm::Plan kOdm768p60From = PHX_ODM_BACK(kPhxP0odm_768p60_from, "1024x768p60-from");
static const phxodm::Plan kOdm600p60To = PHX_ODM_PLAN(kPhxP0odm_600p60_to, "800x600p60-to", 60, 0x273, 0x06, 1);
static const phxodm::Plan kOdm600p60From = PHX_ODM_BACK(kPhxP0odm_600p60_from, "800x600p60-from");
static const phxodm::Plan kOdm480p60To = PHX_ODM_PLAN(kPhxP0odm_480p60_to, "640x480p60-to", 60, 0x20c, 0x06, 1);
static const phxodm::Plan kOdm480p60From = PHX_ODM_BACK(kPhxP0odm_480p60_from, "640x480p60-from");
static const phxodm::Plan kOdm480p59To = PHX_ODM_PLAN(kPhxP0odm_480p59_to, "640x480p59.94-to", 60, 0x20c, 0x06, 1);
static const phxodm::Plan kOdm480p59From = PHX_ODM_BACK(kPhxP0odm_480p59_from, "640x480p59.94-from");
// b145 HDR: the "to" plans end in HDR at the rate on Linux's link for 10 bpc (HBR at 60, HBR2 at 120, HBR3 at 144 / 240),
// the "from" plans back in the SDR hub (8 bpc, the SDR mastering infoframe Linux sends to leave HDR)
static const phxodm::Plan kOdm60hTo = PHX_ODM_PLAN(kPhxP0odm_1440p60hdr_to, "1440p60hdr-to", 60, 0x5c8, 0x0a, 4);
static const phxodm::Plan kOdm60hFrom = PHX_ODM_BACK(kPhxP0odm_1440p60hdr_from, "1440p60hdr-from");
static const phxodm::Plan kOdm120hTo = PHX_ODM_PLAN(kPhxP0odm_1440p120hdr_to, "1440p120hdr-to", 120, 0x5f4, 0x14, 4);
static const phxodm::Plan kOdm120hFrom = PHX_ODM_BACK(kPhxP0odm_1440p120hdr_from, "1440p120hdr-from");
static const phxodm::Plan kOdm144hTo = PHX_ODM_PLAN(kPhxP0odm_1440p144hdr_to, "1440p144hdr-to", 144, 0x5eb, 0x1e, 4);
static const phxodm::Plan kOdm144hFrom = PHX_ODM_BACK(kPhxP0odm_1440p144hdr_from, "1440p144hdr-from");
static const phxodm::Plan kOdm240hTo = PHX_ODM_PLAN(kPhxP0odm_1440p240hdr_to, "1440p240hdr-to", 240, 0x652, 0x1e, 4);
static const phxodm::Plan kOdm240hFrom = PHX_ODM_BACK(kPhxP0odm_1440p240hdr_from, "1440p240hdr-from");
#undef PHX_ODM_BACK
#undef PHX_ODM_PLAN
// Linux's totals per mode (capture v3 regs.txt) and the link Linux trained for each (rate, lanes).
static const phxodm::Mode kOdmModes[] = {
    {60, 2560, 1440, 60, 0xa9f, 0x5c8, 0, 0x0a, 4, &kOdm60To, &kOdm60From},
    {120, 2560, 1440, 120, 0xa9f, 0x5f4, 0, 0x14, 4, &kOdm120To, &kOdm120From},
    {144, 2560, 1440, 144, 0xa9f, 0x5eb, 0, 0x14, 4, nullptr, nullptr},   // the hub
    {240, 2560, 1440, 240, 0xa9f, 0x652, 1, 0x1e, 4, &kOdm240To, &kOdm240From},
    {1080060, 1920, 1080, 60, 0x897, 0x464, 0, 0x06, 4, &kOdm1080p60To, &kOdm1080p60From},
    {1080120, 1920, 1080, 120, 0x897, 0x464, 0, 0x0a, 4, &kOdm1080p120To, &kOdm1080p120From},
    {1024075, 1280, 1024, 75, 0x697, 0x429, 0, 0x06, 4, &kOdm1024p75To, &kOdm1024p75From},
    {768060, 1024, 768, 60, 0x53f, 0x325, 0, 0x06, 2, &kOdm768p60To, &kOdm768p60From},
    {600060, 800, 600, 60, 0x41f, 0x273, 0, 0x06, 1, &kOdm600p60To, &kOdm600p60From},
    {480060, 640, 480, 60, 0x31f, 0x20c, 0, 0x06, 1, &kOdm480p60To, &kOdm480p60From},
    {480059, 640, 480, 60, 0x31f, 0x20c, 0, 0x06, 1, &kOdm480p59To, &kOdm480p59From},
    // b145 HDR (keys height * 1000 + Hz, PhxAccel's HDR twins): the SDR modes' timings, Linux's 10 bpc link
    {1440060, 2560, 1440, 60, 0xa9f, 0x5c8, 0, 0x0a, 4, &kOdm60hTo, &kOdm60hFrom},
    {1440120, 2560, 1440, 120, 0xa9f, 0x5f4, 0, 0x14, 4, &kOdm120hTo, &kOdm120hFrom},
    {1440144, 2560, 1440, 144, 0xa9f, 0x5eb, 0, 0x1e, 4, &kOdm144hTo, &kOdm144hFrom},
    {1440240, 2560, 1440, 240, 0xa9f, 0x652, 1, 0x1e, 4, &kOdm240hTo, &kOdm240hFrom},
};
static const phxodm::Set kOdmSet = {kOdmModes, sizeof kOdmModes / sizeof kOdmModes[0], 2, &kOdmEntry, 3};

static int gOdmLast = -1;
static phxodm::Run gOdm;

// b142 Adaptive Sync (debug.phxvrr): OTG0's V_TOTAL range as Linux's DCN 3.2 register path writes it (PhxP0Odm.h
// vrrRegs), only on a 1440p mode the replay engine lit. Off puts back the three values the first on found (the GOP's: 0);
// every engine run turns it off first (the plans were captured without a range). Under gP2Busy and scanLock.
static uint32_t gVrrBoot[3];   // OTG0 V_TOTAL_MIN, MAX, CONTROL before the first on
static phxodm::VrrRegs gVrrSet{};
static uint32_t gVrrMinHz = 48, gVrrOns = 0, gVrrOffs = 0, gVrrRefusals = 0;
static const char *gVrrWhy = "never run";
static void vrrOffLocked(const phxp2::Env &e) {
    if (!gVrrOn) return;
    e.io.wr(e.io.c, phxodm::kOtg0VCtl, gVrrBoot[2]);
    e.io.wr(e.io.c, phxodm::kOtg0VMin, gVrrBoot[0]);
    e.io.wr(e.io.c, phxodm::kOtg0VMax, gVrrBoot[1]);
    gVrrOn = 0; gVrrOffs++; gVrrWhy = "off";
}
static int vrrOnLocked(const phxp2::Env &e) {
    const phxodm::Mode *m = phxodm::curMode(gOdm, kOdmSet);
    if (!m || m->h != 1440) { gVrrWhy = "not on a 1440p mode the replay engine lit (pick one in System Settings first)"; return EINVAL; }
    if (phxp2::rd(e, phxodm::kOtg0V) != m->vTotal) { gVrrWhy = "OTG0 V_TOTAL is not the mode's"; return EINVAL; }
    const uint32_t ctl = phxp2::rd(e, phxodm::kOtg0VCtl);
    phxodm::VrrRegs v{};
    if (!phxodm::vrrRegs(*m, gVrrMinHz, ctl, &v)) { gVrrWhy = "no range for this mode"; return EINVAL; }
    if (!gVrrOn) { gVrrBoot[0] = phxp2::rd(e, phxodm::kOtg0VMin); gVrrBoot[1] = phxp2::rd(e, phxodm::kOtg0VMax); gVrrBoot[2] = ctl; }
    e.io.wr(e.io.c, phxodm::kOtg0VMax, v.vmax);   // Linux's order: MAX, MIN, then the CONTROL fields
    e.io.wr(e.io.c, phxodm::kOtg0VMin, v.vmin);
    e.io.wr(e.io.c, phxodm::kOtg0VCtl, v.ctl);
    gVrrSet = v; gVrrOn = 1; gVrrOns++; gVrrWhy = "on";
    return 0;
}
static uint32_t gVrrDrops = 0, gVrrResumes = 0;
static void phxVrrDropLocked() { if (!gVrrOn || !p2Up()) return; vrrOffLocked(p2Env()); gVrrDrops++; gVrrWhy = "off: GPU present stopped (wanted)"; }
static void phxVrrResumeLocked() { if (gVrrOn || !gVrrWant || !p2Up()) return; if (!vrrOnLocked(p2Env())) gVrrResumes++; }
// b144: PhxAccel head 0 after each landed switch: *io = 1 (System Settings' Variable, the VRR twin) or 0 (a fixed rate); out:
// 1 when the range is on. On needs GPU present (scanAcquiredLocked); otherwise it is remembered and comes on with it.
static IOReturn phxVrrSetFn(uint32_t *io) {
    if (!io || *io > 1) return kIOReturnBadArgument;
    if (!p2Up() || !gPhx || !gPhx->scanLockForEngines()) return kIOReturnNotReady;
    if (!OSCompareAndSwap(0, 1, &gP2Busy)) return kIOReturnBusy;
    IOLockLock(gPhx->scanLockForEngines());
    const phxp2::Env e = p2Env();
    gVrrWant = static_cast<int>(*io);
    if (!*io) vrrOffLocked(e);
    else if (gPhx->scanAcquiredLocked()) { if (vrrOnLocked(e)) gVrrRefusals++; }
    else gVrrWhy = "wanted: comes on with GPU present";
    *io = static_cast<uint32_t>(gVrrOn);
    IOLockUnlock(gPhx->scanLockForEngines());
    OSCompareAndSwap(1, 0, &gP2Busy);
    return kIOReturnSuccess;
}   // where = kMac until the first run lands (cur is read only in kLinux)

// One engine run under gP2Busy, the AUX lock and scanLock. -1 = not run (busy / not ready; *err set).
static int odmRunLocked(int sel, int *err) {
    *err = 0;
    if (!p2Up() || !gPhx || !gPhx->scanLockForEngines()) { *err = ENXIO; return -1; }
    if (!OSCompareAndSwap(0, 1, &gP2Busy)) { *err = EBUSY; return -1; }
    if (!p2I2cLock(gPhx, 1)) { OSCompareAndSwap(1, 0, &gP2Busy); *err = EBUSY; return -1; }
    gOdmLast = sel;
    IOLockLock(gPhx->scanLockForEngines());
    vrrOffLocked(p2Env());   // b142: the plans run without a V_TOTAL range
    {   // b139: a size change first takes the plane back from GPU present (sel: 1 survey, 2 = 240, 3 = 144, else a key)
        const uint32_t key = sel == 2 ? 240u : sel == 3 ? 144u : static_cast<uint32_t>(sel);
        const int mi = sel == 1 ? -1 : phxodm::findMode(kOdmSet, key);
        if (mi >= 0) gPhx->scanYieldForModeLocked(kOdmSet.m[mi].h);
    }
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
//   b138: PhxAccel's other resolutions (keys height * 1000 + Hz): from macOS's state the entry first, then the key.
static bool p0OdmKnows(uint32_t key) { return phxodm::findMode(kOdmSet, key) >= 0; }
static bool p0OdmRoute(phx_pipe0_setmode *m, IOReturn *ret) {
    const uint32_t t = m->target_hz;
    // macOS's own 60 / 120 / 144 stay with the pipe-0 engine; 240 and (b138) every other resolution start with the entry
    if (gOdm.where == phxodm::kMac && (t == 60 || t == 120 || t == 144)) return false;
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
    int r;
    if (gOdm.where == phxodm::kMac && t != 240) {   // b138: another resolution from macOS's state: the entry (240) first
        r = odmRunLocked(240, &err);
        if (r == phxodm::kDone || (r == phxodm::kFellBack && gOdm.where == phxodm::kLinux)) r = odmRunLocked(static_cast<int>(t), &err);
    } else {
        r = odmRunLocked(static_cast<int>(t), &err);
    }
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
            "b137 root: 1 survey, the 1440p rate 60 / 120 / 144 / 240 (2 = 240, 3 = 144), or another mode as height * 1000 + Hz (1080060, 1080120, 1024075, 768060, 600060, 480060, 480059); reads the last selector");

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
// b142: root writes 1 (OTG0's range from the lit 1440p mode's V_TOTAL down to 48 Hz) or 0 (back to the boot values); reads
// 1 while on. debug.phxvrr_stat: the registers now, the range set, the triggers fired, OTG0's frame rate over 250 ms.
static int phxVrrSysctl SYSCTL_HANDLER_ARGS {
    int v = gVrrOn;
    const int err = sysctl_handle_int(oidp, &v, 0, req);
    if (err || !req->newptr) return err;
    if (proc_suser(req->p)) return EPERM;
    if (v != 0 && v != 1) return EINVAL;
    if (!p2Up() || !gPhx || !gPhx->scanLockForEngines()) return ENXIO;
    if (!OSCompareAndSwap(0, 1, &gP2Busy)) return EBUSY;
    IOLockLock(gPhx->scanLockForEngines());
    const phxp2::Env e = p2Env();
    int r = 0;
    gVrrWant = v;
    if (v) r = vrrOnLocked(e); else vrrOffLocked(e);
    if (r) gVrrRefusals++;
    IOLockUnlock(gPhx->scanLockForEngines());
    OSCompareAndSwap(1, 0, &gP2Busy);
    IOLog("PhxBringup: debug.phxvrr=%d: %s (V_TOTAL_MIN 0x%x MAX 0x%x CONTROL 0x%x)\n", v, gVrrWhy, gVrrSet.vmin, gVrrSet.vmax, gVrrSet.ctl);
    return r;
}
SYSCTL_PROC(_debug, OID_AUTO, phxvrr, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, nullptr, 0, phxVrrSysctl, "I",
            "b142 Adaptive Sync on OTG0 (root): 1 = V_TOTAL range to 48 Hz on the lit 1440p mode (Linux's DCN 3.2 register path), 0 = off");
static int phxVrrStatSysctl SYSCTL_HANDLER_ARGS {
    (void)oidp; (void)arg1; (void)arg2;
    if (req->newptr) return EPERM;
    char b[512];
    int n;
    if (!p2Up()) n = snprintf(b, sizeof b, "PhxBringup not ready");
    else {
        const phxp2::Env e = p2Env();
        const uint32_t f0 = phxp2::rd(e, phxodm::kOtg0Frame);
        IOSleep(250);
        const uint32_t f1 = phxp2::rd(e, phxodm::kOtg0Frame);
        n = snprintf(b, sizeof b, "on=%d why=%s | now V_TOTAL 0x%x MIN 0x%x MAX 0x%x CONTROL 0x%08x | set MIN 0x%x MAX 0x%x CONTROL 0x%08x | "
                     "triggers %lld | want %d ons %u offs %u refusals %u drops %u resumes %u | OTG0 %u frames in 250 ms (%u Hz) | boot MIN 0x%x MAX 0x%x CONTROL 0x%x",
                     gVrrOn, gVrrWhy, phxp2::rd(e, phxodm::kOtg0V), phxp2::rd(e, phxodm::kOtg0VMin), phxp2::rd(e, phxodm::kOtg0VMax),
                     phxp2::rd(e, phxodm::kOtg0VCtl), gVrrSet.vmin, gVrrSet.vmax, gVrrSet.ctl, (long long)gVrrTrigs, gVrrWant, gVrrOns, gVrrOffs,
                     gVrrRefusals, gVrrDrops, gVrrResumes, (f1 - f0) & 0xffffff, ((f1 - f0) & 0xffffff) * 4, gVrrBoot[0], gVrrBoot[1], gVrrBoot[2]);
    }
    if (n < 0) n = 0;
    if (n > (int)sizeof b - 1) n = (int)sizeof b - 1;
    b[n] = 0;
    return SYSCTL_OUT(req, b, static_cast<size_t>(n) + 1);
}
SYSCTL_PROC(_debug, OID_AUTO, phxvrr_stat, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, nullptr, 0, phxVrrStatSysctl, "A",
            "b142 Adaptive Sync: OTG0's V_TOTAL range registers, the range set, triggers fired, OTG0's frame rate over 250 ms");

SYSCTL_PROC(_debug, OID_AUTO, phxp0odm_stat, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, nullptr, 0, phxP0OdmStatSysctl, "A",
            "b135 the replay engine: result, state (mac / linux, rate), the last failure, gates, writes, waits, training, post");
