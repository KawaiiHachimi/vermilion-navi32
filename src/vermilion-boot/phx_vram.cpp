// SPDX-License-Identifier: MIT
// phx_vram.cpp: VRAM-backed client BOs (b156, thread 02 part B, M1): the pool, the GPU clear, the self-test, sysctls.
// Part of the PhxBringup.cpp translation unit (included there, in order); not compiled on its own.
//
// Design: .agents/findings/2026-11-23-vram-bo-design.md. A BOCREATE in the VRAM domain with NO_CPU_ACCESS (and not
// CPU_ACCESS_REQUIRED), from a process named in debug.phxbo_vram_proc while debug.phxbo_vram=1, gets a range of real
// VRAM from the pool (PhxVramPool.h) instead of system pages. Before the handle is returned the GPU fills the range
// with zeros: CP DMA fill packets (PhxCpDma.h, the packet RADV uses for vkCmdFillBuffer) in an IB of the kext's own at
// PHX_VRAM_CLEARIB_OFF, run at VMID 0 on the compute queue, then the RELEASE_MEM fence every submission ends with.
// No register write, SMU, PSP or DAL message: PM4 on the adopted queue, as gpuSubmit does.
// debug.phxbo_vram=1 is refused until debug.phxbo_vram_selftest passed in this boot.

static int gBoVram = 0;                 // 0 off, 1 the processes named in gBoVramProc
static char gBoVramProc[128] = "";      // comma-separated process names (proc_selfname, at most 16 characters each)
static int gBoVramSelftest = 0;         // last self-test: 0 never, 1 passed, < 0 the failing step
static volatile SInt64 gVramClears, gVramClearBytes, gVramClearNs, gVramClearMaxNs, gVramClearFails, gVramFallbacks,
                       gVramNoSpace, gVramCpuMapRefused, gVramScanRefused, gVramLeaked;
static bool gVramClearBroken = false;   // a clear that did not finish: no VRAM BO until the next boot
static volatile SInt64 gVramVisBos = 0;   // b159: CPU-visible VRAM BOs handed out

// The pool's end, the lower of:
// - the top of VRAM minus 112 MiB: below the firmware there (Linux on the XTX: the PSP TMR, 19 MiB at 24512 MiB);
// - the end of the GFXHUB system aperture, because the clear runs at VMID 0, where VRAM is reached by MC address
//   only inside that window. Up to 2026-11-23 mesboot programmed the XT's 0x00200000 - 0x00213fbf (256 KiB units,
//   20464 MiB) on every card; since thread 02 part C it derives the window from FB_LOCATION as Linux does, 0x00217fbf
//   on the XTX (24560 MiB), so the pool is [512 MiB, 24464 MiB) there (the top-of-VRAM limit wins). A range above
//   the window would not be cleared, so the pool never goes past it.
// Register reads only. GC 11.0.0 GCMC_VM_SYSTEM_APERTURE_LOW_ADDR / _HIGH_ADDR = dwords 0x28DD / 0x28DE (mesboot's
// SEG0(0x167d) / SEG0(0x167e)).
static constexpr uint64_t kVramTopReserve = 112ull << 20;
// b159: the CPU-visible pool starts past the large scan slots (PhxScanLayout.h) and ends at BAR0's end; the BO pool
// starts at the larger of PHX_VRAM_POOL_BASE and BAR0's end, so the two never overlap.
static constexpr uint64_t kVisPoolBase = phxscanlay::kLargeEnd;   // 352 MiB
static constexpr uint64_t kVisPoolMin = 64ull << 20;               // a smaller window is not worth a pool
static constexpr uint64_t kVramMinFb = 1ull << 30;
// The GFXHUB system aperture lives in the same block the GC 11.0.3 die moves by +0x10 dwords: measured on this card
// the registers at 0x28DD/0x28DE read 0, while 0x28ED/0x28EE hold low 0x00200000 and high 0x0020ffff (16 GiB).
// Reading the unshifted pair made the pool end 0, which is why the pool was "[0, 0) MiB" and every VRAM BO failed.
static inline uint32_t gcSysApertureLow()  { return 0x28DDu + gGcVmShift; }
static inline uint32_t gcSysApertureHigh() { return 0x28DEu + gGcVmShift; }

bool PhxBringup::vramPoolUpLocked() {
    if (vpool && vpool->end)
        return true;
    if (!mmio || !fbSize || fbSize < kVramMinFb || !fbAper || fbAperLen < PHX_VRAM_SELFTEST_OFF + PHX_VRAM_SELFTEST_SIZE)
        return false;
    const uint64_t apLo = static_cast<uint64_t>(rreg(gcSysApertureLow()) & 0x3FFFFFFF) << 18;
    const uint64_t apEnd = (static_cast<uint64_t>(rreg(gcSysApertureHigh()) & 0x3FFFFFFF) + 1) << 18;
    if (apLo > fbMcBase || apEnd <= fbMcBase + PHX_VRAM_POOL_BASE)
        return false;
    uint64_t end = fbSize - kVramTopReserve;
    if (apEnd - fbMcBase < end)
        end = apEnd - fbMcBase;
    const uint64_t win = fbBarLen < fbAperLen ? fbBarLen : fbAperLen;   // the part of BAR0 this kext reaches
    if (!vispool && win >= kVisPoolBase + kVisPoolMin && win <= end) {   // b159
        vispool = static_cast<phxvram::Pool *>(IOMallocZero(sizeof(phxvram::Pool)));
        if (vispool && phxvram::init(*vispool, kVisPoolBase, win)) {
            visZCap = static_cast<uint32_t>((vispool->end - vispool->base) / phxvram::kGranule);
            visZ = static_cast<VisZombie *>(IOMallocZero(visZCap * sizeof(VisZombie)));
        }
        if (vispool && !visZ) { IOFree(vispool, sizeof(phxvram::Pool)); vispool = nullptr; visZCap = 0; }   // no pool without its parking
    }
    const uint64_t base = vispool && vispool->end > PHX_VRAM_POOL_BASE ? vispool->end : PHX_VRAM_POOL_BASE;
    if (!vpool)
        vpool = static_cast<phxvram::Pool *>(IOMallocZero(sizeof(phxvram::Pool)));
    return vpool && phxvram::init(*vpool, base, end);
}

// b159: a freed CPU-visible BO whose BAR pages a CPU map still holds (its md is retained by the map) keeps its range
// until the map goes: the range would otherwise go to another BO while the old process can still write it.
// Called on every CPU-visible allocation and stat read: a process's maps go when it exits, after its client's BOs.
void PhxBringup::visReapLocked() {
    for (uint32_t i = 0; i < visZn;) {
        VisZombie &z = visZ[i];
        if (z.md->getRetainCount() > 1) { i++; continue; }
        z.md->release();
        if (vispool) phxvram::release(*vispool, z.off, z.size);
        z = visZ[--visZn];   // the last one moves into this slot (checked next)
        visZ[visZn] = VisZombie{};
    }
}

uint64_t PhxBringup::vramVisAlloc(uint64_t bytes) {
    if (!vramLock || gVramClearBroken)
        return 0;
    IOLockLock(vramLock);
    uint64_t off = 0;
    if (vramPoolUpLocked() && vispool) {
        visReapLocked();
        off = phxvram::alloc(*vispool, bytes);
    }
    IOLockUnlock(vramLock);
    if (!off)
        OSIncrementAtomic64(&gVramNoSpace);
    return off;
}

void PhxBringup::vramVisRelease(IOMemoryDescriptor *md, uint64_t off, uint64_t bytes) {
    if (!vramLock || !vispool)
        return;
    IOLockLock(vramLock);
    if (md && md->getRetainCount() > 1) {   // the BO's own reference plus a CPU map's: park it
        if (visZn < visZCap) { md->retain(); visZ[visZn++] = VisZombie{md, off, phxvram::sizeFor(bytes)}; }
        else OSIncrementAtomic64(&gVramLeaked);   // cannot happen (one slot per possible BO); never handed out again
    } else {
        phxvram::release(*vispool, off, bytes);
    }
    IOLockUnlock(vramLock);
}

uint64_t PhxBringup::vramVisTotal() {
    if (!vramLock)
        return 0;
    IOLockLock(vramLock);
    const uint64_t t = vramPoolUpLocked() && vispool ? vispool->end - vispool->base : 0;
    IOLockUnlock(vramLock);
    return t;
}

uint64_t PhxBringup::vramPoolAlloc(uint64_t bytes) {
    if (!vramLock || gVramClearBroken)
        return 0;
    IOLockLock(vramLock);
    const uint64_t off = vramPoolUpLocked() ? phxvram::alloc(*vpool, bytes) : 0;
    IOLockUnlock(vramLock);
    if (!off)
        OSIncrementAtomic64(&gVramNoSpace);
    return off;
}

void PhxBringup::vramPoolFree(uint64_t off, uint64_t bytes) {
    if (!vramLock || !vpool)
        return;
    IOLockLock(vramLock);
    phxvram::release(*vpool, off, bytes);
    IOLockUnlock(vramLock);
}

// The GPU fills [off, off + bytes) of VRAM with `value` and the call waits for it (at most 2 s per IB of up to
// kClearChunk). The IB area is the kext's own, so clears are serialized (clearLock).
static constexpr uint64_t kClearChunk = 1ull << 30;
IOReturn PhxBringup::vramFill(uint64_t off, uint64_t bytes, uint32_t value, uint64_t *nsOut) {
    if (!clearLock || !fbAper || !gpuReady() || gVramClearBroken || (off & 3) || (bytes & 3) || !bytes ||
        off + bytes > fbSize || fbAperLen < PHX_VRAM_CLEARIB_OFF + PHX_VRAM_CLEARIB_SIZE)
        return kIOReturnNotReady;
    const uint64_t t0 = phxNowNs();
    IOReturn r = kIOReturnSuccess;
    IOLockLock(clearLock);
    for (uint64_t done = 0; done < bytes && r == kIOReturnSuccess;) {
        const uint64_t n = bytes - done > kClearChunk ? kClearChunk : bytes - done;
        // compute queue first (the MEC syncs every CP DMA packet), the gfx queue when it is the only one
        GpuQueue &q = gq[0].ready ? gq[0] : gq[1];
        uint32_t *ib = reinterpret_cast<uint32_t *>(fbAper->getVirtualAddress() + PHX_VRAM_CLEARIB_OFF);
        const uint32_t dw = phxcpdma::fill(ib, static_cast<uint32_t>(PHX_VRAM_CLEARIB_SIZE / 4), fbMcBase + off + done, n,
                                           value, q.gfx);
        if (!dw) { r = kIOReturnInternalError; break; }
        __asm__ __volatile__("sfence" ::: "memory");   // BAR0 is write-combined (as gpuSubmit's ring writes)
        (void)ib[dw - 1];
        hdpFlush();
        uint64_t seq = 0;
        r = gpuSubmitKernelIb(q, fbMcBase + PHX_VRAM_CLEARIB_OFF, dw, &seq);
        if (r != kIOReturnSuccess)
            break;
        const uint32_t bit = &q == &gq[0] ? 1u : 2u;
        const uint64_t ts = phxNowNs();
        while (gpuRetiredMask(bit) < seq) {
            if (phxNowNs() - ts > 2000000000ull) { r = kIOReturnTimeout; break; }
            if (phxNowNs() - ts < 200000ull) IODelay(10); else IOSleep(1);
        }
        if (r != kIOReturnSuccess) {
            // the IB area may still be read by the CP: never write it again in this boot
            gVramClearBroken = true;
            IOLog("PhxBringup: VRAM fill of %llu MiB at %llu MiB did not finish in 2 s (seq %llu, retired %llu): VRAM BOs off\n",
                  n >> 20, (off + done) >> 20, seq, gpuRetiredMask(bit));
            break;
        }
        done += n;
    }
    IOLockUnlock(clearLock);
    const uint64_t ns = phxNowNs() - t0;
    if (nsOut) *nsOut = ns;
    return r;
}

IOReturn PhxBringup::vramClear(uint64_t off, uint64_t bytes) {
    uint64_t ns = 0;
    const IOReturn r = vramFill(off, phxvram::sizeFor(bytes), 0, &ns);
    if (r != kIOReturnSuccess) {
        OSIncrementAtomic64(&gVramClearFails);
        return r;
    }
    OSIncrementAtomic64(&gVramClears);
    OSAddAtomic64(static_cast<SInt64>(phxvram::sizeFor(bytes)), &gVramClearBytes);
    OSAddAtomic64(static_cast<SInt64>(ns), &gVramClearNs);
    if (static_cast<SInt64>(ns) > gVramClearMaxNs) gVramClearMaxNs = static_cast<SInt64>(ns);
    return kIOReturnSuccess;
}

// INDIRECT_BUFFER at VMID 0 + the RELEASE_MEM fence, exactly as gpuSubmit writes them (gfx_v11_0_ring_emit_ib_*,
// gfx_v11_0_ring_emit_fence); VMID 0 is how amdgpu runs its own IBs (the ring IB test). ibMc: an MC address in VRAM.
IOReturn PhxBringup::gpuSubmitKernelIb(GpuQueue &q, uint64_t ibMc, uint32_t ibDw, uint64_t *seqOut) {
    if (!q.ready || !ibDw || ibDw > 0xFFFFFu || (ibMc & 3))
        return kIOReturnBadArgument;
    const uint32_t need = 4 + 8;
    IOLockLock(gpuLock);
    for (int spin = 0;; spin++) {
        retireLocked(q);
        if (q.tail - q.head < 63 && q.wptr + need - q.doneEnd <= q.ringDw - 16)
            break;
        if (spin > 40000) {
            IOLockUnlock(gpuLock);
            return kIOReturnBusy;
        }
        IODelay(50);
    }
    uint32_t k = 0;
    ringPut(q, k++, 0xC0023F00);                                   // INDIRECT_BUFFER
    ringPut(q, k++, static_cast<uint32_t>(ibMc) & ~3u);
    ringPut(q, k++, static_cast<uint32_t>(ibMc >> 32));
    ringPut(q, k++, ibDw | (q.gfx ? 0u : (1u << 23)));             // size, VALID on compute, VMID 0
    const uint64_t seq = ++gSeq;
    const uint64_t fenceMc = fbMcBase + q.fenceOff;
    ringPut(q, k++, 0xC0064900);                                   // RELEASE_MEM, as gpuSubmit
    ringPut(q, k++, 0x06603514);
    ringPut(q, k++, gEopIrq ? 0x42000000u : 0x40000000u);
    ringPut(q, k++, static_cast<uint32_t>(fenceMc));
    ringPut(q, k++, static_cast<uint32_t>(fenceMc >> 32));
    ringPut(q, k++, static_cast<uint32_t>(seq));
    ringPut(q, k++, static_cast<uint32_t>(seq >> 32));
    ringPut(q, k++, gEopIrq ? static_cast<uint32_t>(seq) : 0u);
    q.wptr += k;
    q.fseq[q.tail & 63] = seq;
    q.fend[q.tail & 63] = q.wptr;
    q.fctx[q.tail & 63] = phxgputime::kNoCtx;   // b157: no stamps on the kext's own IB
    __asm__ __volatile__("" ::: "memory");
    q.tail = q.tail + 1;
    __asm__ __volatile__("sfence" ::: "memory");
    (void)*reinterpret_cast<volatile uint32_t *>(fbAper->getVirtualAddress() + q.ringOff);
    hdpFlush();
    volatile uint32_t *wb = reinterpret_cast<volatile uint32_t *>(fbAper->getVirtualAddress() + q.wbOff + 16);
    wb[0] = static_cast<uint32_t>(q.wptr);
    wb[1] = static_cast<uint32_t>(q.wptr >> 32);
    __asm__ __volatile__("sfence" ::: "memory");
    (void)wb[0];
    hdpFlush();
    __asm__ __volatile__("mfence" ::: "memory");
    *reinterpret_cast<volatile uint64_t *>(doorbellMap->getVirtualAddress() + q.dbByte) = q.wptr;
    IOLockUnlock(gpuLock);
    *seqOut = seq;
    return kIOReturnSuccess;
}

// debug.phxbo_vram_selftest=1. Step 1: inside BAR0 (PHX_VRAM_SELFTEST_OFF, 1 MiB), the CPU writes a pattern, the
// GPU zeroes it, the CPU checks every dword (also that the dwords around it are untouched). Step 2: above BAR0, at
// the pool's first and last MiB (only while the pool hands out nothing), the GPU fills a pattern, then zeros; 64
// dwords of each read back through MM_INDEX / MM_DATA after each fill. Returns 1 or the failing step (negative).
int PhxBringup::vramSelftest() {
    if (!fbAper || !gpuReady() || !vramLock || gBoVram)
        return -1;   // also while VRAM BOs are on: step 2 needs the pool's first MiB unused
    volatile uint32_t *t = reinterpret_cast<volatile uint32_t *>(fbAper->getVirtualAddress() + PHX_VRAM_SELFTEST_OFF);
    const uint32_t n = static_cast<uint32_t>(PHX_VRAM_SELFTEST_SIZE / 4);
    const uint64_t inner = 4096, len = PHX_VRAM_SELFTEST_SIZE - 2 * inner;   // the clear covers the middle only
    for (uint32_t i = 0; i < n; i++) t[i] = 0xA5000000u | i;
    __asm__ __volatile__("sfence" ::: "memory");
    hdpFlush();
    if (vramFill(PHX_VRAM_SELFTEST_OFF + inner, len, 0, nullptr) != kIOReturnSuccess)
        return -2;
    hdpFlush();
    for (uint32_t i = 0; i < n; i++) {
        const bool mid = i >= inner / 4 && i < (inner + len) / 4;
        if (t[i] != (mid ? 0u : (0xA5000000u | i)))
            return mid ? -3 : -4;
    }
    IOLockLock(vramLock);
    const bool up = vramPoolUpLocked() && vpool->used == 0;
    const uint64_t at[2] = {up ? vpool->base : 0, up ? vpool->end - (1ull << 20) : 0};
    IOLockUnlock(vramLock);
    if (!up)
        return -5;
    for (int k = 0; k < 2; k++) {
        if (vramFill(at[k], 1ull << 20, 0x5A5AA5A5u, nullptr) != kIOReturnSuccess)
            return -6 - 10 * k;
        for (uint32_t i = 0; i < 64; i++)
            if (mmIdxRead(at[k] + i * 16384ull) != 0x5A5AA5A5u)
                return -7 - 10 * k;
        if (vramFill(at[k], 1ull << 20, 0, nullptr) != kIOReturnSuccess)
            return -8 - 10 * k;
        for (uint32_t i = 0; i < 64; i++)
            if (mmIdxRead(at[k] + i * 16384ull) != 0)
                return -9 - 10 * k;
    }
    // b159, step 3: the CPU-visible pool's first and last MiB, when there is one: the GPU fills a pattern at the MC
    // address, the CPU reads it back through the kernel's BAR0 map (proves BAR offset == VRAM offset above 256 MiB),
    // then the GPU zeroes it and the CPU checks again
    IOLockLock(vramLock);
    const bool visUp = vispool && vispool->used == 0;
    const uint64_t visAt[2] = {visUp ? vispool->base : 0, visUp ? vispool->end - (1ull << 20) : 0};
    IOLockUnlock(vramLock);
    for (int k = 0; visUp && k < 2; k++) {
        const volatile uint32_t *p = reinterpret_cast<const volatile uint32_t *>(barKva(visAt[k]));
        if (!p || !barKva(visAt[k] + (1ull << 20) - 4))
            return -31;
        if (vramFill(visAt[k], 1ull << 20, 0xC3C33C3Cu, nullptr) != kIOReturnSuccess)
            return -26 - 10 * k;
        hdpFlush();
        for (uint32_t i = 0; i < 64; i++)
            if (p[i * 4096] != 0xC3C33C3Cu)
                return -27 - 10 * k;
        if (vramFill(visAt[k], 1ull << 20, 0, nullptr) != kIOReturnSuccess)
            return -28 - 10 * k;
        hdpFlush();
        for (uint32_t i = 0; i < 64; i++)
            if (p[i * 4096] != 0)
                return -29 - 10 * k;
    }
    return 1;
}

int PhxBringup::vramStat(char *b, size_t cap) {
    uint64_t base = 0, end = 0, used = 0, peak = 0, fr = 0, big = 0, allocs = 0, frees = 0, fails = 0, lost = 0;
    uint64_t visBase = 0, visEnd = 0, visUsed = 0, visPeak = 0, visAllocs = 0, visFrees = 0, visFails = 0, visHeld = 0;
    uint32_t ext = 0;
    if (vramLock) {
        IOLockLock(vramLock);
        if (vpool && vpool->end) {
            base = vpool->base; end = vpool->end; used = vpool->used; peak = vpool->peak; fr = phxvram::freeBytes(*vpool);
            big = phxvram::largest(*vpool); allocs = vpool->allocs; frees = vpool->frees; fails = vpool->fails;
            lost = vpool->lost; ext = vpool->n;
        }
        if (vispool && vispool->end) {   // b159
            visReapLocked();   // b160: the stat shows what is still held, not what went at the last exit
            visBase = vispool->base; visEnd = vispool->end; visUsed = vispool->used; visPeak = vispool->peak;
            visAllocs = vispool->allocs; visFrees = vispool->frees; visFails = vispool->fails; visHeld = visZn;
        }
        IOLockUnlock(vramLock);
    }
    const uint64_t c = static_cast<uint64_t>(gVramClears);
    return snprintf(b, cap,
        "mode=%d procs=%s selftest=%d broken=%d\n"
        "pool [%llu, %llu) MiB: used %llu MiB (peak %llu), free %llu MiB, largest %llu MiB, %u extents; allocs %llu frees %llu "
        "fails %llu lost %llu KiB\n"
        "clears %llu, %llu MiB, avg %llu us, max %llu us, failed %lld; fallbacks to system memory %lld (no space %lld); "
        "refused: CPU map %lld, scanout %lld; leaked ranges %lld\n"
        "CPU-visible pool (BAR0 %llu MiB) [%llu, %llu) MiB: used %llu MiB (peak %llu), allocs %llu frees %llu fails %llu, "
        "held by CPU maps %llu; BOs handed out %lld",
        gBoVram, gBoVramProc[0] ? gBoVramProc : "-", gBoVramSelftest, gVramClearBroken ? 1 : 0,
        base >> 20, end >> 20, used >> 20, peak >> 20, fr >> 20, big >> 20, ext, allocs, frees, fails, lost >> 10,
        c, static_cast<uint64_t>(gVramClearBytes) >> 20, c ? static_cast<uint64_t>(gVramClearNs) / c / 1000 : 0,
        static_cast<uint64_t>(gVramClearMaxNs) / 1000, (long long)gVramClearFails, (long long)gVramFallbacks,
        (long long)gVramNoSpace, (long long)gVramCpuMapRefused, (long long)gVramScanRefused, (long long)gVramLeaked,
        fbBarLen >> 20, visBase >> 20, visEnd >> 20, visUsed >> 20, visPeak >> 20, visAllocs, visFrees, visFails, visHeld,
        (long long)gVramVisBos);
}

// Is the calling process one of debug.phxbo_vram_proc, with VRAM BOs on?
static bool phxBoVramProcOn() {
    if (gBoVram != 1 || gBoVramSelftest != 1 || gVramClearBroken || !gBoVramProc[0])
        return false;
    char me[32] = {0};
    proc_selfname(me, sizeof me);
    const size_t ml = strlen(me);
    if (!ml)
        return false;
    for (const char *p = gBoVramProc; *p;) {
        const char *e = p;
        while (*e && *e != ',') e++;
        if (static_cast<size_t>(e - p) == ml && !strncmp(p, me, ml))
            return true;
        p = *e ? e + 1 : e;
    }
    return false;
}

// Is this BOCREATE (in the calling process) one for the pool?
static bool phxBoVramWanted(uint64_t domains, uint64_t flags) {
    return (domains & PHX_AMDGPU_GEM_DOMAIN_VRAM) && (flags & PHX_AMDGPU_GEM_CREATE_NO_CPU_ACCESS) &&
           !(flags & PHX_AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED) && phxBoVramProcOn();
}

// b159: one for the CPU-visible pool inside BAR0 (a VRAM BO the CPU may map)?
static bool phxBoVisVramWanted(uint64_t domains, uint64_t flags) {
    return (domains & PHX_AMDGPU_GEM_DOMAIN_VRAM) && !(flags & PHX_AMDGPU_GEM_CREATE_NO_CPU_ACCESS) && phxBoVramProcOn();
}

static int phxBoVramSysctl SYSCTL_HANDLER_ARGS {
    int v = gBoVram;
    const int err = sysctl_handle_int(oidp, &v, 0, req);
    if (err || !req->newptr)
        return err;
    if (v != 0 && v != 1)
        return EINVAL;
    if (v == 1 && (gBoVramSelftest != 1 || gVramClearBroken))
        return EPERM;   // debug.phxbo_vram_selftest first, in this boot
    gBoVram = v;
    return 0;
}
SYSCTL_PROC(_debug, OID_AUTO, phxbo_vram, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, nullptr, 0, phxBoVramSysctl, "I",
            "b156: 1 = VRAM-backed BOs (NO_CPU_ACCESS) for the processes in debug.phxbo_vram_proc; needs the self-test");
SYSCTL_STRING(_debug, OID_AUTO, phxbo_vram_proc, CTLFLAG_RW | CTLFLAG_LOCKED, gBoVramProc, sizeof gBoVramProc,
              "b156: comma-separated process names that get VRAM-backed BOs while debug.phxbo_vram=1");
static int phxBoVramSelftestSysctl SYSCTL_HANDLER_ARGS {
    int v = gBoVramSelftest;
    const int err = sysctl_handle_int(oidp, &v, 0, req);
    if (err || !req->newptr)
        return err;
    if (v != 1)
        return EINVAL;
    if (!gPhx)
        return ENXIO;
    gBoVramSelftest = gPhx->vramSelftest();
    return 0;
}
SYSCTL_PROC(_debug, OID_AUTO, phxbo_vram_selftest, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, nullptr, 0,
            phxBoVramSelftestSysctl, "I", "b156: write 1 to run the VRAM clear self-test; reads 1 passed, <0 the failing step, 0 never run");
static int phxBoVramStatSysctl SYSCTL_HANDLER_ARGS {
    char b[768];
    const int len = gPhx ? gPhx->vramStat(b, sizeof b) : snprintf(b, sizeof b, "no PhxBringup");
    return SYSCTL_OUT(req, b, static_cast<size_t>(len) + 1);
}
SYSCTL_PROC(_debug, OID_AUTO, phxbo_vram_stat, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, nullptr, 0, phxBoVramStatSysctl,
            "A", "b156: VRAM BO pool, clears and fallbacks");
