// SPDX-License-Identifier: MIT
// phx_gputime.cpp: GPU time per context (b157, thread 02 part C). Packets and accounting: PhxGpuTime.h.
// Part of the PhxBringup.cpp translation unit (included there, in order); not compiled on its own.
//
// debug.phxgpu_time_on=1 stamps every user submission from then on; debug.phxgpu_time lists, per context, the time it
// held each queue since debug.phxgpu_time_reset (or the switch), with its share of the wall time; 0 stamps nothing
// (the default: the packets cost two end-of-pipe events per submission).

static int gGpuTimeOn = 0;

static_assert(phxgputime::kAreaBytes <= PHX_GPUTIME_SIZE, "the timestamp slots fit their VRAM area");

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
    if (trace) {   // b158: the last kTraceN retired submissions, in retirement order per queue
        TraceRec &r = trace[traceN % kTraceN];
        r = TraceRec{q.fsub[idx & 63], start, end, q.fdw[idx & 63], c, static_cast<uint8_t>(qi), q.fnib[idx & 63]};
        traceN++;
    }
}

// SMUIO GOLDEN_TSC, the CP's 100 MHz timestamp clock (phx_client.cpp kGoldenTscUpper: upper, then lower)
uint64_t PhxBringup::goldenTsc() const {
    uint32_t hi = rreg(0x16a2d), lo = rreg(0x16a2e);
    const uint32_t hi2 = rreg(0x16a2d);
    if (hi2 != hi) { hi = hi2; lo = rreg(0x16a2e); }
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

int PhxBringup::gpuTraceSet(int on) {
    if (!gpuLock)
        return ENXIO;
    TraceRec *fresh = on ? static_cast<TraceRec *>(IOMallocZero(kTraceBytes)) : nullptr;
    if (on && !fresh)
        return ENOMEM;
    IOLockLock(gpuLock);
    for (uint32_t qi = 0; qi < 3; qi++)
        if (gq[qi].ready && !gq[qi].vcn)
            retireLocked(gq[qi]);
    TraceRec *old = trace;
    trace = fresh;
    traceN = 0;
    IOLockUnlock(gpuLock);
    if (old)
        IOFree(old, kTraceBytes);
    return 0;
}

// The trace as text, oldest first: times in us relative to the first record's start (negative = before it).
char *PhxBringup::gpuTraceDump(size_t *len) {
    *len = 0;
    if (!gpuLock)
        return nullptr;
    TraceRec *snap = static_cast<TraceRec *>(IOMalloc(kTraceBytes));
    if (!snap)
        return nullptr;
    IOLockLock(gpuLock);
    for (uint32_t qi = 0; qi < 3; qi++)
        if (gq[qi].ready && !gq[qi].vcn)
            retireLocked(gq[qi]);
    const uint32_t n = trace ? (traceN < kTraceN ? traceN : kTraceN) : 0, first = traceN - n;
    for (uint32_t i = 0; i < n; i++)
        snap[i] = trace[(first + i) % kTraceN];
    IOLockUnlock(gpuLock);
    const size_t cap = 96 + static_cast<size_t>(n) * 112;
    char *b = static_cast<char *>(IOMalloc(cap));
    if (!b) {
        IOFree(snap, kTraceBytes);
        return nullptr;
    }
    const uint64_t t0 = n ? snap[0].start : 0;
    constexpr long long kPerUs = static_cast<long long>(phxgputime::kTicksPerUs);
    auto rel = [t0](uint64_t t) -> long long { return t ? (static_cast<long long>(t) - static_cast<long long>(t0)) / kPerUs : 0; };
    int o = snprintf(b, cap, "records %u (of %u); q ctx pid name | sub start end (us from first start) | dur us | ib dw | ibs\n", n, traceN);
    for (uint32_t i = 0; i < n && o > 0 && static_cast<size_t>(o) < cap; i++) {
        const TraceRec &r = snap[i];
        const bool closed = r.ctx >= kMaxCtx;
        o += snprintf(b + o, cap - static_cast<size_t>(o), "%u %u %d %s | %lld %lld %lld | %lld | %u | %u\n", r.q,
                      closed ? 0 : r.ctx + 1, closed ? 0 : ctxPid[r.ctx], closed ? "-" : ctxName[r.ctx], rel(r.sub),
                      rel(r.start), rel(r.end), (r.start && r.end >= r.start) ? static_cast<long long>(phxgputime::ticksToUs(r.end - r.start)) : -1LL,
                      r.dw, r.nib);
    }
    IOFree(snap, kTraceBytes);
    *len = cap;
    if (o < 0) o = 0;
    if (static_cast<size_t>(o) >= cap) o = static_cast<int>(cap) - 1;
    b[o] = 0;
    return b;
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

static int gGpuTraceOn = 0;
static int phxGpuTraceSysctl SYSCTL_HANDLER_ARGS {
    int v = gGpuTraceOn;
    const int err = sysctl_handle_int(oidp, &v, 0, req);
    if (err || !req->newptr)
        return err;
    if (v != 0 && v != 1)
        return EINVAL;
    if (!gPhx)
        return ENXIO;
    const int e = gPhx->gpuTraceSet(v);   // 1 again restarts the trace (cleared)
    if (!e)
        gGpuTraceOn = v;
    return e;
}
SYSCTL_PROC(_debug, OID_AUTO, phxgpu_trace, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, nullptr, 0, phxGpuTraceSysctl, "I",
            "b158: 1 = (re)start the per-submission trace (needs debug.phxgpu_time_on=1), 0 = stop and free it");
static int phxGpuTraceDumpSysctl SYSCTL_HANDLER_ARGS {
    size_t cap = 0;
    char *b = gPhx ? gPhx->gpuTraceDump(&cap) : nullptr;
    if (!b)
        return SYSCTL_OUT(req, "no trace", 9);
    const int err = SYSCTL_OUT(req, b, strlen(b) + 1);
    IOFree(b, cap);
    return err;
}
SYSCTL_PROC(_debug, OID_AUTO, phxgpu_trace_dump, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, nullptr, 0,
            phxGpuTraceDumpSysctl, "A", "b158: the last 4096 stamped submissions, oldest first");
