// head0_dig4_test: the Navi 32 (PHX_PIPE_DIG=4) head-0 mode table and its sync table must stay consistent.
//
// Why this exists: on 2026-11-24 the DIG4 head-0 ids were renumbered twice (3,2,4,5,6 -> 19..23 -> 1..5) while
// kHead0Sync still carried the XTX machine's LG/AOC values for ids 2..6. CoreDisplay displayed the rate from one table
// and read the blanking/sync from the other, so a 144 Hz request reached the pipe as 120 and the desktop fell back -
// the black screens of that evening. The ids must be the list order 1..N, each row must have its own detailed timing,
// and the rate that timing reports must be the row's rate.
#define PHX_PIPE_DIG 4
#include "phxhead.h"
#include "phxhp0.h"
#include "phxrotate.h"

#include <cstdio>

static int fails;
static void ck(bool ok, const char *what) {
    if (!ok) { std::printf("  FAIL %s\n", what); fails++; }
}

static void replayEdges() {
    using namespace phxhp0;
    const Raw lit{kSenseDelayed, 0, kBeOn, kLinkTrained, kVidEnable | kVidStatus};
    ReplayEdge edge;
    Sample sample{};
    sample.c[2] = lit;
    ck(replayEdge(edge, sample, 0) < 0 && edge.active == 2, "arming on a lit connector is not a reconnect");
    sample.c[2] = {};
    ck(replayEdge(edge, sample, 250) < 0 && !edge.lost, "first low sample is not an unplug");
    sample.c[2] = lit;
    ck(replayEdge(edge, sample, 500) < 0 && !edge.lost, "short HPD glitch is ignored");
    sample.c[2] = {};
    ck(replayEdge(edge, sample, 1000) < 0 && replayEdge(edge, sample, 7000) < 0 && edge.lost, "settled unplug alone never replays");
    sample.c[2].hpd = kSenseDelayed;
    ck(replayEdge(edge, sample, 8000) < 0, "HPD without an OTG0 stream never replays");
    sample.c[1] = sample.c[2] = lit;
    ck(replayEdge(edge, sample, 9000) < 0, "ambiguous live route never replays");
    sample.c[2] = {};
    ck(replayEdge(edge, sample, 10000) < 0, "alternate port must settle");
    sample.bad = true;
    ck(replayEdge(edge, sample, 11000) < 0, "bad sample breaks the settle window");
    sample.bad = false;
    ck(replayEdge(edge, sample, 11250) < 0 && replayEdge(edge, sample, 12250) == 1, "settled alternate OTG0 route is eligible");
    edge = ReplayEdge{};
    edge.active = 1;
    ck(replayEdge(edge, sample, 12500) < 0, "successful replay consumes the reconnect");
    sample.c[1] = {};
    ck(replayEdge(edge, sample, 13000) < 0 && replayEdge(edge, sample, 19000) < 0, "same port can unplug again");
    sample.c[1] = lit;
    ck(replayEdge(edge, sample, 19250) < 0 && replayEdge(edge, sample, 20250) == 1, "same port can reconnect");
}

static void publishedRotationPreflight() {
    // Navi 32 publishes 6 modes (five 1440p rates + 1920x1080@60 as id 6), while kHead0Modes only has ids 1..5.
    // CoreDisplay preflights rotated versions of EVERY mode that reported a detailed timing, and without IOFBScalerInfo
    // a single kWhyNoPlan refusal aborts the mode rebuild (90-degree rotation black screen).
    using namespace phxhead;
    const HeadMode pub[6] = {
        {1, 2560, 1440,  60000, 24170, 2720, 1481, 0},
        {2, 2560, 1440, 100000, 40284, 2720, 1481, 0},
        {3, 2560, 1440, 120000, 49776, 2720, 1525, 0},
        {4, 2560, 1440, 144000, 59729, 2720, 1525, 0},
        {5, 2560, 1440, 165000, 65750, 2720, 1465, 0},
        {6, 1920, 1080,  60000, 14850, 2200, 1125, 0},
    };
    const uint32_t ids[6] = {1, 2, 3, 4, 5, 6};
    DetailedTiming dt1080{(uint64_t)148500 * 1000ull, 1920, 2200 - 1920, 88, 44, 1080, 1125 - 1080, 4, 5, true, true};
    // Without the published table, id 6 fails preflight with kWhyNoPlan (reproduces the bug).
    ck(phxrot::check(phxrot::turned(dt1080, phxrot::kRot90, 0), 0, ids, 6, nullptr) == phxrot::kWhyNoPlan,
       "compiled table alone rejects published-only 1080p60 rotated timing");
    // With the published table, every published mode (1..6) passes preflight at 90 / 180 / 270 and installs cleanly.
    phxrot::Table tb{};
    for (unsigned i = 0; i < 6; i++) {
        DetailedTiming d{(uint64_t)pub[i].pclk10kHz * 10000ull, pub[i].w, pub[i].hTotal - pub[i].w, 48, 32,
                         pub[i].h, pub[i].vTotal - pub[i].h, 3, 5, true, true};
        ck(phxrot::rotatable(d, 0, ids, 6, nullptr, pub, 6), "every published Navi 32 mode passes rotation preflight");
        uint32_t plan = 0;
        const phxrot::Timing t90 = phxrot::turned(d, phxrot::kRot90, 0x80001000u + i);
        ck(phxrot::check(t90, 0, ids, 6, &plan, nullptr, pub, 6) == phxrot::kOk && plan == pub[i].id,
           "90-degree check maps back to the exact published mode id");
        ck(phxrot::add(tb, t90, 0, ids, 6, nullptr, pub, 6) == phxrot::kOk, "90-degree timing installs into rotation table");
    }
    ck(tb.n == 6 && tb.skipped == 0, "all 6 published modes installed as rotated modes");

    // Scaled HiDPI timings (unrotated, hScaled/vScaled != hActive/vActive) validate and carry HiRes flags.
    phxrot::Timing hi{};
    hi.id = 0x80002000u;
    hi.pixelClock = (uint64_t)pub[4].pclk10kHz * 10000ull;
    hi.hActive = 2560; hi.hBlank = pub[4].hTotal - 2560;
    hi.vActive = 1440; hi.vBlank = pub[4].vTotal - 1440;
    hi.hScaled = 3840; hi.vScaled = 2160;
    hi.scalerFlags = phxrot::kScaleStretchToFit;
    uint32_t hiPlan = 0;
    ck(phxrot::isScaledMode(hi), "3840x2160 backing on 2560x1440 raster is classified as scaled mode");
    ck(phxrot::check(hi, 0, ids, 6, &hiPlan, nullptr, pub, 6) == phxrot::kOk && hiPlan == 5,
       "scaled HiDPI 3840x2160@165 maps to published 2560x1440@165 mode id 5");
    uint32_t dw = 0, dh = 0;
    phxrot::desktopOf(hi, &dw, &dh);
    ck(dw == 3840 && dh == 2160, "desktopOf reports scaled pixel dimensions");
    ck((phxrot::validatedFlags(0, phxrot::kOptAccelBacked, true) & (phxrot::kAccelBackedFlag | phxrot::kNotPresetFlag | phxrot::kValidForHiResFlag))
           == (phxrot::kAccelBackedFlag | phxrot::kNotPresetFlag | phxrot::kValidForHiResFlag),
       "validatedFlags sets AccelBacked + NotPreset + ValidForHiRes for scaled modes");
    ck((phxrot::infoFlags(true) & phxrot::kAccelBackedFlag) == 0
           && (phxrot::infoFlags(true) & phxrot::kValidForHiResFlag) != 0,
       "infoFlags sets ValidForHiRes without AcceleratorBacked");
    ck(phxrot::add(tb, hi, 0, ids, 6, nullptr, pub, 6) == phxrot::kOk && tb.n == 7,
       "scaled HiDPI timing installs cleanly into table");
    hi.hScaled = 3841;   // odd width rejected
    ck(phxrot::check(hi, 0, ids, 6, &hiPlan, nullptr, pub, 6) == phxrot::kWhyScaled,
       "odd scaled dimensions are rejected");
}

int main() {
    replayEdges();
    publishedRotationPreflight();
    using namespace phxhead;
    uint32_t prev = 0;
    unsigned n = 0;
    for (uint32_t id = 1; id <= 8; id++) {
        const HeadMode *m = headMode(0, id);
        if (!m) break;
        n++;
        ck(m->id == id, "a row's id equals its slot in the list");
        ck(m->milliHz > prev, "the rates ascend with the id");
        prev = m->milliHz;
        ck(m->w == 2560 && m->h == 1440, "the DIG4 family is the 1440p one");
        DetailedTiming t{};
        ck(detailedTiming(0, id, &t), "every DIG4 row has a detailed timing (else CoreDisplay reads no timing)");
        const uint64_t ht = (uint64_t)t.hActive + t.hBlank, vt = (uint64_t)t.vActive + t.vBlank;
        const uint64_t hz = ht && vt ? (t.pixelHz * 1000ull) / (ht * vt) : 0;
        const uint64_t want = m->milliHz, d = hz > want ? hz - want : want - hz;
        ck(want && d * 100 <= want, "the timing reports the row's rate within 1 %");
        ck(switchHz(*m) == (m->milliHz + 500) / 1000, "switchHz rounds the row's rate (the value PhxBringup gets)");
        ck(t.hSyncWidth && t.vSyncWidth, "the sync widths are not zero");
        std::printf("  id %u: %ux%u %u mHz (reported %llu) hb %u hs %u/%u vb %u vs %u/%u %s%s\n", id, m->w, m->h, m->milliHz,
                    (unsigned long long)hz, t.hBlank, t.hSyncOffset, t.hSyncWidth, t.vBlank, t.vSyncOffset, t.vSyncWidth,
                    t.hSyncPos ? "H+" : "H-", t.vSyncPos ? "V+" : "V-");
    }
    ck(n == 5, "the DIG4 family lists all five rates (60/100/120/144/165)");
    std::printf("head0_dig4_test: %u modes, %d failed\n", n, fails);
    return fails ? 1 : 0;
}
