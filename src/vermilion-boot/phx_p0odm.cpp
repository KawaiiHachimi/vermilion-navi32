// SPDX-License-Identifier: MIT
// phx_p0odm.cpp: the pipe-0 replay engine's glue (PhxP0Odm.h), for the XTX machine's 2560x1440 monitor.
//   The plan set: Linux's 144 Hz as the hub, every other mode as a to / from pair (kOdmModes).
//   System Settings' modes through phxPipe0SetMode (p0OdmRoute); PhxAccel resizes the desktop for the other sizes.
//   Adaptive Sync on the lit mode: "phxVrrSet" (PhxAccel, System Settings' Variable), debug.phxvrr, debug.phxvrr_floor,
//     debug.phxvrr_ramp, debug.phxvrr_stat. The per-flip part (the trigger, the ramp's window) is in phx_scan.cpp.
//   "phxScanFmt": HUBP0's scan-out format, for PhxAccel's CPU copy (10-bit HDR).
//   debug.phxp0odm (int): root writes 1 = survey, a 1440p rate (60, 120, 144, 240; aliases 2 = 240, 3 = 144), or any
//     other mode of the set by its key, height * 1000 + Hz (1080060 = 1920x1080@60, 1024075 = 1280x1024@75, 480059 =
//     640x480@59.94, 1440240 = 1440p240 HDR). A key written here does not tell PhxAccel: the desktop keeps its size.
//     Reads return the last selector (-1 = never). Inert until written. debug.phxp0odm_stat: the last run.
// Engine runs and range changes hold gP2Busy and scanLock (no flip while a plan runs; the flip path then follows
// gP0OdmActive); an engine run also holds the EDID/AUX lock.
// Part of the PhxBringup.cpp translation unit, included after phx_dpbretrain.cpp; not compiled on its own.

#include "PhxP0Odm.h"
#include "phx_p0odm_1440p240.inc"        // the entry: macOS's 144 (one pixel per clock) -> 240 (capture v2)
// capture v3: Linux's 144 hub -> each mode and back (the 1440p rates, the other resolutions)
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
// capture v4: Linux's SDR 144 hub -> HDR at each 1440p rate and back (two mode sets each)
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
// HDR: the "to" plans end in HDR at the rate, on Linux's link for 10 bpc (HBR at 60, HBR2 at 120, HBR3 at 144 / 240) and
// on Linux's 10-bit scan-out format; the "from" plans end back in the SDR hub (8 bpc, the 8-bit format, the SDR
// mastering infoframe Linux sends to leave HDR)
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
// Linux's totals per mode (capture v3 regs.txt) and the link Linux trained for each (rate, lanes). Columns: key, width,
// height, Hz, H_TOTAL, V_TOTAL, ODM 2:1, link rate, lanes, to plan, from plan (phxodm::Mode).
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
    // HDR (keys height * 1000 + Hz, PhxAccel's HDR twins): the SDR modes' timings, Linux's 10 bpc link
    {1440060, 2560, 1440, 60, 0xa9f, 0x5c8, 0, 0x0a, 4, &kOdm60hTo, &kOdm60hFrom},
    {1440120, 2560, 1440, 120, 0xa9f, 0x5f4, 0, 0x14, 4, &kOdm120hTo, &kOdm120hFrom},
    {1440144, 2560, 1440, 144, 0xa9f, 0x5eb, 0, 0x1e, 4, &kOdm144hTo, &kOdm144hFrom},
    {1440240, 2560, 1440, 240, 0xa9f, 0x652, 1, 0x1e, 4, &kOdm240hTo, &kOdm240hFrom},
};
static const phxodm::Set kOdmSet = {kOdmModes, sizeof kOdmModes / sizeof kOdmModes[0], 2, &kOdmEntry, 3};   // hub 144, entry 240

static int gOdmLast = -1;   // the last selector written to debug.phxp0odm
static phxodm::Run gOdm;    // where = kMac until the first run lands (cur is read only in kLinux)

// ---- Adaptive Sync -----------------------------------------------------------------------------------------------------
// OTG0's V_TOTAL range as Linux's DCN 3.2 register path writes it (PhxP0Odm.h vrrRegs), only on a 1440p mode the replay
// engine lit. Off puts back the values the first on found (the GOP's); every engine run turns the range off first (the
// plans were captured without one). Callers hold gP2Busy and scanLock.
//   gVrrWant (phx_sysctl.cpp)   System Settings' Variable, or debug.phxvrr, asked for the range
//   gVrrOn   (phx_sysctl.cpp)   the range is on. "phxVrrSet" keeps it on only while GPU present scans: its flips fire
//                               the trigger that ends each stretched frame
//   gVrrMinHz                   the floor: 48 until PhxAccel sends its own (debug.phxfb0_vrrmin) or debug.phxvrr_floor
static uint32_t gVrrBoot[3];       // OTG0 V_TOTAL_MIN, MAX, CONTROL before the first on
static uint32_t gVrrBootDbuf = 0;  // OTG0's double-buffer control before the first on
static phxodm::VrrRegs gVrrSet{};  // the range last written
static uint32_t gVrrMinHz = 48, gVrrOns = 0, gVrrOffs = 0, gVrrRefusals = 0;
static const char *gVrrWhy = "never run";
static uint32_t gVrrOffWaitUs = 0, gVrrOffWaitMaxUs = 0, gVrrOffTimeouts = 0;   // off's wait for the frame-start latch
// b155: debug.phxp0odm_dbuf (root, live; 0 at boot = b150's rule). 1 = Linux's OTG_DRR_TIMING_DBUF_UPDATE_MODE 2 for the
// whole boot, as optc3_tg_init sets it at driver load (decisions 2026-11-22, "Revisit": a plan run under mode 2 tried on
// the box): every replay run and the range's off keep mode 2. 0 again puts back the mode found at the first 1 (VRR off).
static int gDbufLinux = 0;
static bool gDbufSaved = false;
static uint32_t gDbufGop = 0;      // OTG0's double-buffer control before the first 1
static uint32_t gDbufSets = 0;     // mode-2 writes made for it
static void dbufLinuxLocked(const phxp2::Env &e) {
    if (!gDbufLinux) return;
    const uint32_t db = phxp2::rd(e, phxodm::kOtg0Dbuf);
    if (!gDbufSaved) { gDbufGop = db; gDbufSaved = true; }
    if ((db & phxodm::kDbufDrrMode) != phxodm::kDbufDrrFrameStart) {
        e.io.wr(e.io.c, phxodm::kOtg0Dbuf, phxodm::dbufWith(db, phxodm::kDbufDrrFrameStart));
        gDbufSets++;
    }
}
static void vrrOffLocked(const phxp2::Env &e) {
    if (!gVrrOn) return;
    gVrrOn = 0;   // first: no flip writes a window or a trigger from here (scanPresent runs under the same scanLock)
    bool timedOut = false;
    gVrrOffWaitUs = phxodm::vrrWriteOff(e, gVrrBoot, gDbufLinux ? phxodm::kDbufDrrFrameStart : gDbufSaved ? gDbufGop : gVrrBootDbuf, &timedOut);   // latched at a frame start, then the boot mode (b155: Linux's 2 while debug.phxp0odm_dbuf)
    if (gVrrOffWaitUs > gVrrOffWaitMaxUs) gVrrOffWaitMaxUs = gVrrOffWaitUs;
    if (timedOut) gVrrOffTimeouts++;
    gVrrOffs++; gVrrWhy = "off";
    gVrrRampVMin = gVrrRampVMax = gVrrRampHz = 0; gVrrRampPrimed = false;   // no ramp while off
}
static int vrrOnLocked(const phxp2::Env &e) {
    const phxodm::Mode *m = phxodm::curMode(gOdm, kOdmSet);
    if (!m || m->h != 1440) { gVrrWhy = "not on a 1440p mode the replay engine lit (pick one in System Settings first)"; return EINVAL; }
    if (phxp2::rd(e, phxodm::kOtg0V) != m->vTotal) { gVrrWhy = "OTG0 V_TOTAL is not the mode's"; return EINVAL; }
    const uint32_t ctl = phxp2::rd(e, phxodm::kOtg0VCtl);
    phxodm::VrrRegs v{};
    if (!phxodm::vrrRegs(*m, gVrrMinHz, ctl, &v)) { gVrrWhy = "no range for this mode"; return EINVAL; }
    if (!gVrrOn) { gVrrBoot[0] = phxp2::rd(e, phxodm::kOtg0VMin); gVrrBoot[1] = phxp2::rd(e, phxodm::kOtg0VMax); gVrrBoot[2] = ctl; }
    uint32_t dbufBefore = 0;
    phxodm::vrrWriteOn(e, v, &dbufBefore);   // Linux's DRR double buffer mode first, then MAX, MIN, the CONTROL fields
    if (!gVrrOn) gVrrBootDbuf = dbufBefore;
    gVrrSet = v; gVrrOn = 1; gVrrOns++; gVrrWhy = "on";
    gVrrRampVMin = v.vmin; gVrrRampVMax = v.vmax; gVrrRampHz = m->hz;   // the ramp starts from the full range
    gVrrRampLo = v.vmin; gVrrRampHi = v.vmax; gVrrRampPrimed = false;
    return 0;
}
// GPU present stopped / started (phx_scan.cpp, scanLock held): the range follows it, the want stays.
static uint32_t gVrrDrops = 0, gVrrResumes = 0;
static void phxVrrDropLocked() {
    if (!gVrrOn || !p2Up()) return;
    vrrOffLocked(p2Env());
    gVrrDrops++; gVrrWhy = "off: GPU present stopped (wanted)";
}
static void phxVrrResumeLocked() {
    if (gVrrOn || !gVrrWant || !p2Up()) return;
    if (!vrrOnLocked(p2Env())) gVrrResumes++;
}
// "phxVrrSet": PhxAccel head 0 calls it after each landed switch.
//   in   *io bit 0      1 = System Settings' Variable (a VRR twin), 0 = a fixed rate
//        *io bits 8-15  the floor in Hz (debug.phxfb0_vrrmin), 24 or more; 0 = keep the current one (48 at boot; what a
//                       PhxAccel before the floor sends). The range runs from the mode's V_TOTAL to that rate.
//   out  *io            1 when the range is on
// On needs GPU present (scanAcquiredLocked); otherwise the want is remembered and the range comes on with it.
static IOReturn phxVrrSetFn(uint32_t *io) {
    if (!io || (*io & ~0xff01u)) return kIOReturnBadArgument;
    const uint32_t floorHz = (*io >> 8) & 0xffu;
    if (floorHz && floorHz < 24) return kIOReturnBadArgument;
    if (!p2Up() || !gPhx || !gPhx->scanLockForEngines()) return kIOReturnNotReady;
    if (!OSCompareAndSwap(0, 1, &gP2Busy)) return kIOReturnBusy;
    IOLockLock(gPhx->scanLockForEngines());
    const phxp2::Env e = p2Env();
    gVrrWant = static_cast<int>(*io & 1u);
    if (floorHz && floorHz != gVrrMinHz) { vrrOffLocked(e); gVrrMinHz = floorHz; }   // a new floor: set again below
    if (!gVrrWant) vrrOffLocked(e);
    else if (gPhx->scanAcquiredLocked()) { if (vrrOnLocked(e)) gVrrRefusals++; }
    else gVrrWhy = "wanted: comes on with GPU present";
    *io = static_cast<uint32_t>(gVrrOn);
    IOLockUnlock(gPhx->scanLockForEngines());
    OSCompareAndSwap(1, 0, &gP2Busy);
    return kIOReturnSuccess;
}

// "phxScanFmt": HUBP0's scan-out format now (8 ARGB8888; 10 ARGB2101010 after an HDR plan), for PhxAMDFB's CPU copy
static IOReturn phxScanFmtFn(uint32_t *io) {
    if (!io) return kIOReturnBadArgument;
    if (!p2Up()) return kIOReturnNotReady;
    *io = phxp2::rd(p2Env(), kHubp0SurfaceConfig) & 0x7fu;
    return kIOReturnSuccess;
}

// One engine run under gP2Busy, the AUX lock and scanLock. -1 = not run (busy / not ready; *err set).
static int odmRunLocked(int sel, int *err) {
    *err = 0;
    if (!p2Up() || !gPhx || !gPhx->scanLockForEngines()) { *err = ENXIO; return -1; }
    if (!OSCompareAndSwap(0, 1, &gP2Busy)) { *err = EBUSY; return -1; }
    if (!p2I2cLock(gPhx, 1)) { OSCompareAndSwap(1, 0, &gP2Busy); *err = EBUSY; return -1; }
    gOdmLast = sel;
    IOLockLock(gPhx->scanLockForEngines());
    vrrOffLocked(p2Env());   // the plans run without a V_TOTAL range
    dbufLinuxLocked(p2Env());   // b155: under Linux's DRR double buffer when debug.phxp0odm_dbuf is 1
    {   // a size change first takes the plane back from GPU present (sel: 1 survey, 2 = 240, 3 = 144, else a key)
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
//   PhxAccel's other resolutions and HDR twins (keys height * 1000 + Hz): from macOS's state the entry first, then the key.
static bool p0OdmKnows(uint32_t key) { return phxodm::findMode(kOdmSet, key) >= 0; }
static bool p0OdmRoute(phx_pipe0_setmode *m, IOReturn *ret) {
    const uint32_t t = m->target_hz;
    // macOS's own 60 / 120 / 144 stay with the pipe-0 engine; 240 and every keyed mode start with the entry
    // navi32 (2026-11-24): every rate the pipe-0 family has a plan for stays with the pipe-0 engine. The old
    // whitelist named the XTX machine rates (60 / 120 / 144), so this board 100 and 165 fell into the ODM engine
    // and came back as "240: the step to 144 first" or "no sink: HPD2 (DP B) sense low": the ODM path is for
    // 240 Hz and the keyed modes of other sizes. Asking the family itself keeps the two engines apart.
    if (gOdm.where == phxodm::kMac) {
#if PHX_PIPE_DIG == 4
        // navi32 (b164): every key resolved by the pipe-0 catalog (verified static plans + dynamically synthesized
        // EDID plans, whether kKeyIdxBase + idx, height * 1000 + Hz, or bare Hz) stays with the pipe-0 engine instead
        // of falling into the Navi 31 ODM path.
        if (p0PickKeyed(t)) return false;
#else
        const P0Fam f = p0Family(p0Env());
        for (unsigned i = 0; i < f.n; i++) if (f.pl[i] && f.pl[i]->info.hz == t) return false;
#endif
    }
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
    if (gOdm.where == phxodm::kMac && t != 240) {   // a keyed mode from macOS's state: the entry (240) first
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
    static constexpr int kN = 4096;
    char *b = static_cast<char *>(IOMalloc(kN));
    if (!b) return ENOMEM;
    int len = gOdm.attempts ? phxodm::stat(gOdm, kOdmSet, b, kN)
                            : snprintf(b, kN, "never run (root: sysctl -w debug.phxp0odm=1 survey, or 60 / 120 / 144 / 240)\n");
    if (len < 0) len = 0;
    if (len < kN - 1)   // b155: the boot's trainer DPCD accesses (phx_dplink.cpp, drm's 32-try rule)
        len += snprintf(b + len, static_cast<size_t>(kN - len), "dpcd_aux: calls=%u ok=%u retried=%u gave_up=%u timeouts=%u nacks=%u defers=%u "
                        "other=%u max_tries=%u last_first_err=%d\n", gDplDpcd.calls, gDplDpcd.ok, gDplDpcd.retried, gDplDpcd.gaveUp,
                        gDplDpcd.timeouts, gDplDpcd.nacks, gDplDpcd.defers, gDplDpcd.other, gDplDpcd.maxTries, gDplDpcd.lastFirstErr);
    if (len < 0) len = 0;
    if (len > kN - 1) len = kN - 1;
    b[len] = 0;
    const int e = SYSCTL_OUT(req, b, static_cast<size_t>(len) + 1);
    IOFree(b, kN);
    return e;
}
// debug.phxvrr: root writes 1 (OTG0's range from the lit 1440p mode's V_TOTAL down to the floor) or 0 (back to the boot
// values); reads 1 while on. For tests by hand: System Settings' Variable goes through "phxVrrSet".
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
static int phxP0OdmDbufSysctl SYSCTL_HANDLER_ARGS {
    int v = gDbufLinux;
    const int err = sysctl_handle_int(oidp, &v, 0, req);
    if (err || !req->newptr) return err;
    if (proc_suser(req->p)) return EPERM;
    if (v != 0 && v != 1) return EINVAL;
    if (!p2Up() || !gPhx || !gPhx->scanLockForEngines()) return ENXIO;
    if (!OSCompareAndSwap(0, 1, &gP2Busy)) return EBUSY;
    IOLockLock(gPhx->scanLockForEngines());
    const phxp2::Env e = p2Env();
    gDbufLinux = v;
    if (v) dbufLinuxLocked(e);
    else if (gDbufSaved && !gVrrOn) {   // back to the mode found before (the range, when on, keeps 2 until its off)
        const uint32_t db = phxp2::rd(e, phxodm::kOtg0Dbuf);
        if ((db & phxodm::kDbufDrrMode) != (gDbufGop & phxodm::kDbufDrrMode)) e.io.wr(e.io.c, phxodm::kOtg0Dbuf, phxodm::dbufWith(db, gDbufGop));
    }
    const uint32_t now = phxp2::rd(e, phxodm::kOtg0Dbuf);
    IOLockUnlock(gPhx->scanLockForEngines());
    OSCompareAndSwap(1, 0, &gP2Busy);
    IOLog("PhxBringup: debug.phxp0odm_dbuf=%d: OTG0 DOUBLE_BUFFER_CONTROL 0x%08x (before the first 1: 0x%08x, mode-2 writes %u)\n", v, now,
          gDbufGop, gDbufSets);
    return 0;
}
SYSCTL_PROC(_debug, OID_AUTO, phxp0odm_dbuf, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, nullptr, 0, phxP0OdmDbufSysctl, "I",
            "b155 root: 1 = Linux's DRR double buffer (OTG0 OTG_DRR_TIMING_DBUF_UPDATE_MODE 2) for every replay run and after the range's off; 0 = b150's rule (2 only while the range is on)");
SYSCTL_PROC(_debug, OID_AUTO, phxvrr, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, nullptr, 0, phxVrrSysctl, "I",
            "b142 Adaptive Sync on OTG0 (root): 1 = V_TOTAL range to the floor (48 Hz; b146: PhxAccel's) on the lit 1440p mode (Linux's DCN 3.2 register path), 0 = off");
// debug.phxvrr_ramp: the ramp (PhxVrrRamp.h), % per flip (0 = off: the mode's full range; 1-50); takes effect at the
// next flip. Root.
static int phxVrrRampSysctl SYSCTL_HANDLER_ARGS {
    int v = gVrrRampPct;
    const int err = sysctl_handle_int(oidp, &v, 0, req);
    if (err || !req->newptr) return err;
    if (proc_suser(req->p)) return EPERM;
    if (v < 0 || v > 50) return EINVAL;
    gVrrRampPct = v;   // an int store; scanPresent reads it once per flip
    IOLog("PhxBringup: debug.phxvrr_ramp=%d\n", v);
    return 0;
}
SYSCTL_PROC(_debug, OID_AUTO, phxvrr_ramp, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, nullptr, 0, phxVrrRampSysctl, "I",
            "b147 Adaptive Sync ramp (root): the next frames may be at most this % shorter or longer than the last ones (1-50, default 5; 0 = off, the full range)");
// debug.phxvrr_floor: the floor in Hz, live (root; 24 or more, below the lit mode's rate). While on, the range is set
// again at once. PhxAccel's next "phxVrrSet" (a mode switch) puts back its own floor (debug.phxfb0_vrrmin), and System
// Settings' label stays PhxAccel's. For tests by hand; the lasting knob is ~/gpu/phxboot-vrrmin.
static int phxVrrFloorSysctl SYSCTL_HANDLER_ARGS {
    int v = static_cast<int>(gVrrMinHz);
    const int err = sysctl_handle_int(oidp, &v, 0, req);
    if (err || !req->newptr) return err;
    if (proc_suser(req->p)) return EPERM;
    if (v < 24 || v > 255) return EINVAL;
    if (!p2Up() || !gPhx || !gPhx->scanLockForEngines()) return ENXIO;
    if (!OSCompareAndSwap(0, 1, &gP2Busy)) return EBUSY;
    IOLockLock(gPhx->scanLockForEngines());
    const phxp2::Env e = p2Env();
    int r = 0;
    const bool was = gVrrOn != 0;
    gVrrMinHz = static_cast<uint32_t>(v);
    if (was) { vrrOffLocked(e); r = vrrOnLocked(e); if (r) gVrrRefusals++; }
    IOLockUnlock(gPhx->scanLockForEngines());
    OSCompareAndSwap(1, 0, &gP2Busy);
    IOLog("PhxBringup: debug.phxvrr_floor=%d: %s\n", v, gVrrWhy);
    return r;
}
SYSCTL_PROC(_debug, OID_AUTO, phxvrr_floor, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, nullptr, 0, phxVrrFloorSysctl, "I",
            "b147 Adaptive Sync floor in Hz (root, live; 24 or more): OTG0's V_TOTAL_MAX goes to it; a mode switch puts back PhxAccel's");
// debug.phxvrr_stat: the registers now, the range set, the triggers fired, the ramp's window, the counters, OTG0's frame
// rate over 250 ms (the read sleeps that long), the boot values, the double buffer, HUBP0's control (underflow 30:28).
static int phxVrrStatSysctl SYSCTL_HANDLER_ARGS {
    (void)oidp; (void)arg1; (void)arg2;
    if (req->newptr) return EPERM;
    char b[1100];
    int n;
    if (!p2Up()) n = snprintf(b, sizeof b, "PhxBringup not ready");
    else {
        const phxp2::Env e = p2Env();
        const uint32_t f0 = phxp2::rd(e, phxodm::kOtg0Frame);
        IOSleep(250);
        const uint32_t f1 = phxp2::rd(e, phxodm::kOtg0Frame);
        n = snprintf(b, sizeof b,
                     "on=%d why=%s | now V_TOTAL 0x%x MIN 0x%x MAX 0x%x CONTROL 0x%08x | set MIN 0x%x MAX 0x%x CONTROL 0x%08x | "
                     "triggers %lld | ramp %d %% window 0x%x-0x%x last %u lines writes %lld | "
                     "floor %u Hz want %d ons %u offs %u refusals %u drops %u resumes %u | OTG0 %u frames in 250 ms (%u Hz) | "
                     "boot MIN 0x%x MAX 0x%x CONTROL 0x%x | dbuf now 0x%08x boot 0x%08x off wait %u us max %u timeouts %u | "
                     "HUBP0 0x%08x",
                     gVrrOn, gVrrWhy, phxp2::rd(e, phxodm::kOtg0V), phxp2::rd(e, phxodm::kOtg0VMin), phxp2::rd(e, phxodm::kOtg0VMax),
                     phxp2::rd(e, phxodm::kOtg0VCtl), gVrrSet.vmin, gVrrSet.vmax, gVrrSet.ctl,
                     (long long)gVrrTrigs, gVrrRampPct, gVrrRampLo, gVrrRampHi, gVrrRampLines, (long long)gVrrRampWrites,
                     gVrrMinHz, gVrrWant, gVrrOns, gVrrOffs, gVrrRefusals, gVrrDrops, gVrrResumes,
                     (f1 - f0) & 0xffffff, ((f1 - f0) & 0xffffff) * 4,
                     gVrrBoot[0], gVrrBoot[1], gVrrBoot[2],
                     phxp2::rd(e, phxodm::kOtg0Dbuf), gVrrBootDbuf, gVrrOffWaitUs, gVrrOffWaitMaxUs, gVrrOffTimeouts,
                     phxp2::rd(e, phxodm::kHubp0Cntl));
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
