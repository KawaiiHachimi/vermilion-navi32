// pick_1440_test: the Navi 32 1440p family must return the plan for the rate it is asked for.
//
// Guards the alignment between the published mode table (PhxP0Modes.h) and phxp0::pick: the table lists the plan's
// own info.hz, and pick matches on the same field, so a mismatch here is what makes a 165 Hz request program the 144 Hz
// plan while the framebuffer believes the switch is done.
#define PHX_PIPE_DIG 4
#include "PhxPipe0.h"

#include "phx_p0_plan_1440p60n32.inc"
#include "phx_p0_plan_1440p100n32.inc"
#include "phx_p0_plan_1440p120n32.inc"
#include "phx_p0_plan_1440p144n32.inc"
#include "phx_p0_plan_1440p165n32.inc"

#include <cstdio>
#include <cstring>

static int fails;

int main() {
    using namespace phxp0;
    static const Plan p60{kPhxP0_1440p60n32, sizeof kPhxP0_1440p60n32 / sizeof kPhxP0_1440p60n32[0], kPhxP0_1440p60n32Info, 0, nullptr, 0};
    static const Plan p100{kPhxP0_1440p100n32, sizeof kPhxP0_1440p100n32 / sizeof kPhxP0_1440p100n32[0], kPhxP0_1440p100n32Info, 4, nullptr, 0};
    static const Plan p120{kPhxP0_1440p120n32, sizeof kPhxP0_1440p120n32 / sizeof kPhxP0_1440p120n32[0], kPhxP0_1440p120n32Info, 4, nullptr, 0};
    static const Plan p144{kPhxP0_1440p144n32, sizeof kPhxP0_1440p144n32 / sizeof kPhxP0_1440p144n32[0], kPhxP0_1440p144n32Info, 4, nullptr, 0};
    static const Plan p165{kPhxP0_1440p165n32, sizeof kPhxP0_1440p165n32 / sizeof kPhxP0_1440p165n32[0], kPhxP0_1440p165n32Info, 4, nullptr, 0};
    const Plan *const fam[] = {&p60, &p100, &p120, &p144, &p165};
    const unsigned n = sizeof fam / sizeof fam[0];
    // the family the engine publishes and the FB lists
    for (unsigned i = 0; i < n; i++) {
        std::printf("  plan %u: %s %u Hz pclk %u hT 0x%x vT 0x%x ready %u\n", i, fam[i]->info.name, fam[i]->info.hz,
                    fam[i]->info.pclkKhz, fam[i]->info.hTotal, fam[i]->info.vTotal, fam[i]->info.ready);
        if (!fam[i]->info.ready) { std::printf("  FAIL plan %s is not ready\n", fam[i]->info.name); fails++; }
    }
    // pick must return the plan whose hz is the request, for every rate the table publishes
    const uint32_t rates[] = {60, 100, 120, 144, 165};
    Env e{};
    State st{};
    for (uint32_t hz : rates) {
        const Plan *got = pick(e, st, fam, n, hz);
        if (!got || got->info.hz != hz) {
            std::printf("  FAIL pick(%u) -> %s\n", hz, got ? got->info.name : "null");
            fails++;
        } else {
            std::printf("  pick(%u) -> %s\n", hz, got->info.name);
        }
    }
    // and every plan still has its OTG totals, the field identify() matches on
    for (unsigned i = 0; i < n; i++) {
        bool hasH = false, hasV = false;
        for (unsigned k = 0; k < fam[i]->n; k++) {
            if (fam[i]->s[k].reg == 0x04FEA) hasH = true;
            if (fam[i]->s[k].reg == 0x04FEF) hasV = true;
        }
        if (!hasH || !hasV) { std::printf("  FAIL %s has no OTG totals steps\n", fam[i]->info.name); fails++; }
    }
    std::printf("pick_1440_test: %d failed\n", fails);
    return fails ? 1 : 0;
}