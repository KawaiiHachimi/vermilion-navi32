// SPDX-License-Identifier: MIT
// phx_cursor.cpp: opt-in HUBP0 / DPP0 hardware cursor plane in PhxBringup (b163).
// Part of the PhxBringup.cpp translation unit (included there, in order); not compiled on its own.
//
// Inert unless PhxAMDFB calls "phxCursorImage" / "phxCursorState" (gated by debug.phxcursor=1 in VermilionAccel,
// default 0). Re-programs cursor attributes automatically after a landed pipe-0 mode switch (cursorReprogram).

static IOLock *gCurLock = nullptr;
static bool gCurHave = false, gCurAttrsDone = false, gCurVisible = false;
static uint32_t gCurW = 0, gCurH = 0, gCurDispW = 0, gCurDispH = 0;
static int32_t gCurX = 0, gCurY = 0;
static uint64_t gCurImages = 0, gCurMoves = 0, gCurReprograms = 0;

void PhxBringup::cursorProgramLocked(bool attributes) {
    if (!mmio || !fbAper || !fbMcBase) return;
    if (PHX_VRAM_CURSOR_OFF + PHX_VRAM_CURSOR_SIZE > fbAperLen) return;
    if (attributes) {
        const uint64_t mc = fbMcBase + PHX_VRAM_CURSOR_OFF;
        wreg(phxcursor::kRegCursorAddrHi, static_cast<uint32_t>(mc >> 32));
        wreg(phxcursor::kRegCursorAddrLo, static_cast<uint32_t>(mc));
        wreg(phxcursor::kRegCursorSize, phxcursor::kSize64x64);
        wreg(phxcursor::kRegCursorControl, phxcursor::hubpCtlAttrs(rreg(phxcursor::kRegCursorControl)));
        wreg(phxcursor::kRegCursorSettings, phxcursor::kSettingsVal);
        wreg(phxcursor::kRegCnvcCursorControl, phxcursor::cnvcCtlAttrs(rreg(phxcursor::kRegCnvcCursorControl)));
        gCurAttrsDone = true;
    }
    uint32_t dw = gCurDispW, dh = gCurDispH;
    const uint32_t vp = rreg(phxcursor::kRegPriViewportDim);
    if ((vp & 0xFFFFu) && (vp >> 16)) { dw = vp & 0xFFFFu; dh = vp >> 16; }
    const uint32_t ratio = rreg(phxcursor::kRegRefFreqToPixFreq);
    const phxcursor::PosRegs p = phxcursor::computePos(gCurX, gCurY, gCurW, gCurH, dw, dh, ratio, gCurVisible && gCurHave);
    if (p.enabled) {
        wreg(phxcursor::kRegCursorPosition, p.pos);
        wreg(phxcursor::kRegCursorHotSpot, p.hot);
        wreg(phxcursor::kRegCursorDstOffset, p.dstX);
    }
    wreg(phxcursor::kRegCursorControl, phxcursor::hubpCtlEnable(rreg(phxcursor::kRegCursorControl), p.enabled));
    wreg(phxcursor::kRegCnvcCursorControl, phxcursor::cnvcCtlEnable(rreg(phxcursor::kRegCnvcCursorControl), p.enabled));
}

IOReturn PhxBringup::cursorImage(const phx_cursor_image *req) {
    if (!req || req->version != PHX_CURSOR_ABI_VERSION || !req->argb ||
        !req->width || !req->height || req->width > phxcursor::kMaxDim || req->height > phxcursor::kMaxDim)
        return kIOReturnBadArgument;
    if (!mmio || !fbAper || !fbMcBase || PHX_VRAM_CURSOR_OFF + PHX_VRAM_CURSOR_SIZE > fbAperLen)
        return kIOReturnNotReady;
    if (!gCurLock) {
        IOLock *l = IOLockAlloc();
        if (l && !OSCompareAndSwapPtr(nullptr, l, reinterpret_cast<void *volatile *>(&gCurLock))) IOLockFree(l);
        if (!gCurLock) return kIOReturnNoMemory;
    }
    IOLockLock(gCurLock);
    const bool wasOn = gCurVisible && gCurHave;
    if (wasOn) { gCurVisible = false; cursorProgramLocked(false); gCurVisible = true; }
    volatile uint32_t *dst = reinterpret_cast<volatile uint32_t *>(fbAper->getVirtualAddress() + PHX_VRAM_CURSOR_OFF);
    for (uint32_t i = 0; i < phxcursor::kPitchPixels * phxcursor::kMaxDim; i++) dst[i] = req->argb[i];
    __asm__ __volatile__("sfence" ::: "memory");
    (void)dst[phxcursor::kPitchPixels * phxcursor::kMaxDim - 1];
    gCurW = req->width; gCurH = req->height;
    if (req->disp_w && req->disp_h) { gCurDispW = req->disp_w; gCurDispH = req->disp_h; }
    gCurHave = true;
    gCurImages++;
    cursorProgramLocked(true);
    IOLockUnlock(gCurLock);
    return kIOReturnSuccess;
}

IOReturn PhxBringup::cursorState(const phx_cursor_state *req) {
    if (!req || req->version != PHX_CURSOR_ABI_VERSION) return kIOReturnBadArgument;
    if (!mmio || !fbAper || !fbMcBase || PHX_VRAM_CURSOR_OFF + PHX_VRAM_CURSOR_SIZE > fbAperLen)
        return kIOReturnNotReady;
    if (!gCurLock) {
        IOLock *l = IOLockAlloc();
        if (l && !OSCompareAndSwapPtr(nullptr, l, reinterpret_cast<void *volatile *>(&gCurLock))) IOLockFree(l);
        if (!gCurLock) return kIOReturnNoMemory;
    }
    IOLockLock(gCurLock);
    gCurX = req->x; gCurY = req->y; gCurVisible = req->visible != 0;
    if (req->disp_w && req->disp_h) { gCurDispW = req->disp_w; gCurDispH = req->disp_h; }
    gCurMoves++;
    cursorProgramLocked(!gCurAttrsDone && gCurHave);
    IOLockUnlock(gCurLock);
    return kIOReturnSuccess;
}

void PhxBringup::cursorReprogram() {
    if (!gCurHave || !gCurLock) return;
    IOLockLock(gCurLock);
    if (gCurHave) { gCurReprograms++; cursorProgramLocked(true); }
    IOLockUnlock(gCurLock);
}

void PhxBringup::cursorStop() {
    if (gCurLock) {
        IOLockLock(gCurLock);
        if (gCurHave) { gCurVisible = false; cursorProgramLocked(false); gCurHave = false; gCurAttrsDone = false; }
        IOLockUnlock(gCurLock);
        IOLockFree(gCurLock);
        gCurLock = nullptr;
    }
}

int PhxBringup::cursorStat(char *b, size_t n) const {
    const uint32_t ctl = mmio ? rreg(phxcursor::kRegCursorControl) : 0u;
    const uint32_t pos = mmio ? rreg(phxcursor::kRegCursorPosition) : 0u;
    const uint32_t dst = mmio ? rreg(phxcursor::kRegCursorDstOffset) : 0u;
    const uint32_t cnvc = mmio ? rreg(phxcursor::kRegCnvcCursorControl) : 0u;
    const uint32_t ratio = mmio ? rreg(phxcursor::kRegRefFreqToPixFreq) : 0u;
    return snprintf(b, n,
                    "have=%d vis=%d size=%ux%u pos=%d,%d images=%llu moves=%llu reprograms=%llu "
                    "hubp_ctl=0x%08x hubp_pos=0x%08x dst_x=0x%x cnvc=0x%08x ref2pix=0x%06x",
                    gCurHave ? 1 : 0, gCurVisible ? 1 : 0, gCurW, gCurH, gCurX, gCurY,
                    (unsigned long long)gCurImages, (unsigned long long)gCurMoves, (unsigned long long)gCurReprograms,
                    ctl, pos, dst, cnvc, ratio);
}

static int phxCursorStatSysctl SYSCTL_HANDLER_ARGS {
    (void)oidp; (void)arg1; (void)arg2;
    if (req->newptr) return EPERM;
    char b[512];
    int len = gPhx ? gPhx->cursorStat(b, sizeof b) : snprintf(b, sizeof b, "no PhxBringup");
    if (len < 0) len = 0;
    if (len >= static_cast<int>(sizeof b)) len = static_cast<int>(sizeof b) - 1;
    return SYSCTL_OUT(req, b, static_cast<size_t>(len) + 1);
}
SYSCTL_PROC(_debug, OID_AUTO, phxcursor_stat, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, nullptr, 0, phxCursorStatSysctl, "A",
            "b163 HUBP0 hardware cursor plane state, counters and live registers (read only)");
