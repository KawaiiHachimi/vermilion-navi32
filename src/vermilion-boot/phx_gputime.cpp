// SPDX-License-Identifier: MIT
// phx_gputime.cpp: GPU time per context (b157, thread 02 part C). Packets and accounting: PhxGpuTime.h.
// Part of the PhxBringup.cpp translation unit (included there, in order); not compiled on its own.
//
// debug.phxgpu_time_on=1 stamps every user submission from then on; debug.phxgpu_time lists, per context, the time it
// held each queue since debug.phxgpu_time_reset (or the switch), with its share of the wall time; 0 stamps nothing
// (the default: the packets cost two end-of-pipe events per submission).

static int gGpuTimeOn = 0;

void PhxBringup::gpuTimeAccount(GpuQueue &q, uint32_t idx) {
    const uint16_t c = q.fctx[idx & 63];
    if (c == phxgputime::kNoCtx || c > kMaxCtx || !fbAper)
        return;
    const uint32_t qi = static_cast<uint32_t>(&q - gq);
    const volatile uint32_t *ts = reinterpret_cast<const volatile uint32_t *>(
        fbAper->getVirtualAddress() + PHX_GPUTIME_OFF + phxgputime::slotOff(qi, idx));
    const uint64_t start = static_cast<uint64_t>(ts[0]) | (static_cast<uint64_t>(ts[1]) << 32);
    const uint64_t end = static_cast<uint64_t>(ts[2]) | (static_cast<uint64_t>(ts[3]) << 32);
    const uint64_t b = phxgputime::account(q.tsLastEnd, start, end);
    if (!b && (!start || !end))
        gpuTimeLost++;
    ctxGpuTicks[qi][c] += b;
    ctxGpuSubs[qi][c]++;
}

void PhxBringup::retireLocked(GpuQueue &q) {
    const uint64_t done = queueFence(q);
    while (q.head != q.tail && q.fseq[q.head & 63] <= done) {
        gpuTimeAccount(q, q.head);
        q.doneEnd = q.fend[q.head & 63];
        q.head++;
    }
}

void PhxBringup::gpuTimeReset() {
    if (!gpuLock)
        return;
    IOLockLock(gpuLock);
    for (uint32_t qi = 0; qi < 3; qi++) {
        if (gq[qi].ready)
            retireLocked(gq[qi]);
        for (uint32_t c = 0; c <= kMaxCtx; c++)
            ctxGpuTicks[qi][c] = ctxGpuSubs[qi][c] = 0;
    }
    gpuTimeLost = 0;
    gpuTimeResetAbs = mach_absolute_time();
    IOLockUnlock(gpuLock);
}

int PhxBringup::gpuTimeStat(char *b, size_t cap) {
    if (!gpuLock)
        return snprintf(b, cap, "no GPU");
    struct Snap { uint64_t t[3][kMaxCtx + 1], s[3][kMaxCtx + 1]; };   // a snapshot, copied under gpuLock
    Snap *sn = static_cast<Snap *>(IOMalloc(sizeof(Snap)));
    if (!sn)
        return snprintf(b, cap, "no memory");
    uint64_t (&t)[3][kMaxCtx + 1] = sn->t;
    uint64_t (&s)[3][kMaxCtx + 1] = sn->s;
    IOLockLock(gpuLock);
    for (uint32_t qi = 0; qi < 3; qi++)
        if (gq[qi].ready && !gq[qi].vcn)
            retireLocked(gq[qi]);   // account what has finished, without waiting
    memcpy(t, ctxGpuTicks, sizeof t);
    memcpy(s, ctxGpuSubs, sizeof s);
    const uint64_t lost = gpuTimeLost, since = gpuTimeResetAbs;
    IOLockUnlock(gpuLock);
    uint64_t wallNs = 0;
    if (since)
        absolutetime_to_nanoseconds(mach_absolute_time() - since, &wallNs);
    const uint64_t wallUs = wallNs / 1000;
    // queue roles as gpuSubmit picks them: gq[1] gfx, gq[0] compute (when both are up)
    int o = snprintf(b, cap, "on=%d since reset %llu ms; unstamped %llu; per context: gfx us (%% of wall) subs | compute us subs\n",
                     gGpuTimeOn, wallUs / 1000, lost);
    uint64_t tot[3] = {0, 0, 0};
    for (uint32_t c = 0; c <= kMaxCtx && o > 0 && static_cast<size_t>(o) < cap; c++) {
        if (!s[0][c] && !s[1][c])
            continue;
        const uint64_t g = phxgputime::ticksToUs(t[1][c]), k = phxgputime::ticksToUs(t[0][c]);
        tot[1] += g; tot[0] += k;
        const uint64_t pm = wallUs ? g * 1000 / wallUs : 0;   // per mille
        o += snprintf(b + o, cap - static_cast<size_t>(o), "%s%u pid %d %s: gfx %llu us (%llu.%llu %%) %llu | compute %llu us %llu\n",
                      c == kMaxCtx ? "closed " : "ctx ", c == kMaxCtx ? 0 : c + 1, c == kMaxCtx ? 0 : ctxPid[c],
                      c == kMaxCtx ? "-" : ctxName[c], g, pm / 10, pm % 10, s[1][c], k, s[0][c]);
    }
    if (o > 0 && static_cast<size_t>(o) < cap) {
        const uint64_t pg = wallUs ? tot[1] * 1000 / wallUs : 0, pc = wallUs ? tot[0] * 1000 / wallUs : 0;
        o += snprintf(b + o, cap - static_cast<size_t>(o), "total: gfx %llu us (%llu.%llu %%), compute %llu us (%llu.%llu %%)",
                      tot[1], pg / 10, pg % 10, tot[0], pc / 10, pc % 10);
    }
    IOFree(sn, sizeof(Snap));
    return o < 0 ? 0 : (static_cast<size_t>(o) >= cap ? static_cast<int>(cap) - 1 : o);
}

static int phxGpuTimeOnSysctl SYSCTL_HANDLER_ARGS {
    int v = gGpuTimeOn;
    const int err = sysctl_handle_int(oidp, &v, 0, req);
    if (err || !req->newptr)
        return err;
    if (v != 0 && v != 1)
        return EINVAL;
    if (v == 1 && !gGpuTimeOn && gPhx)
        gPhx->gpuTimeReset();
    gGpuTimeOn = v;
    return 0;
}
SYSCTL_PROC(_debug, OID_AUTO, phxgpu_time_on, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, nullptr, 0, phxGpuTimeOnSysctl, "I",
            "b157: 1 = stamp each submission with GPU timestamps (GPU time per context in debug.phxgpu_time)");
static int phxGpuTimeResetSysctl SYSCTL_HANDLER_ARGS {
    int v = 0;
    const int err = sysctl_handle_int(oidp, &v, 0, req);
    if (err || !req->newptr)
        return err;
    if (v != 1)
        return EINVAL;
    if (gPhx)
        gPhx->gpuTimeReset();
    return 0;
}
SYSCTL_PROC(_debug, OID_AUTO, phxgpu_time_reset, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, nullptr, 0, phxGpuTimeResetSysctl,
            "I", "b157: write 1 to zero the GPU time counters");
static int phxGpuTimeSysctl SYSCTL_HANDLER_ARGS {
    constexpr size_t kCap = 8192;
    char *b = static_cast<char *>(IOMalloc(kCap));
    if (!b)
        return ENOMEM;
    const int len = gPhx ? gPhx->gpuTimeStat(b, kCap) : snprintf(b, kCap, "no PhxBringup");
    const int err = SYSCTL_OUT(req, b, static_cast<size_t>(len) + 1);
    IOFree(b, kCap);
    return err;
}
SYSCTL_PROC(_debug, OID_AUTO, phxgpu_time, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, nullptr, 0, phxGpuTimeSysctl, "A",
            "b157: GPU time per context and queue since the last reset");
