// SPDX-License-Identifier: MIT
// vrr_ramp_test.cpp: host test of src/vermilion-boot/PhxVrrRamp.h (Adaptive Sync's frame-length ramp, kext b147).
//   c++ -std=c++17 -Wall -Wextra -Werror -I src/vermilion-boot -o /tmp/vrr_ramp_test tests/kext/vrr_ramp_test.cpp && /tmp/vrr_ramp_test
#include "PhxVrrRamp.h"
#include <cstdio>
static int gN, gF;
#define CHECK(c) do { gN++; if (!(c)) { gF++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
using namespace phxvrr;
int main() {
    // 1440p 240 (V_TOTAL 0x652 = 1618, 1619 lines), floor 60: V_TOTAL_MAX 6475
    const uint32_t vmin = 0x652, vmax = 6475;
    CHECK(linesFromPeriod(1000000000ull / 240, vmin, 240) == 1618 || linesFromPeriod(1000000000ull / 240, vmin, 240) == 1619);
    CHECK(linesFromPeriod(1000000000ull / 60, vmin, 240) == 6476 || linesFromPeriod(1000000000ull / 60, vmin, 240) == 6475);
    CHECK(linesFromPeriod(0, vmin, 240) == 0 && linesFromPeriod(1, vmin, 0) == 0);
    uint32_t lo, hi;
    window(0, vmin, vmax, 10, &lo, &hi); CHECK(lo == vmin && hi == vmax);          // no measurement
    window(3000, vmin, vmax, 0, &lo, &hi); CHECK(lo == vmin && hi == vmax);        // ramp off
    window(3000, vmin, vmax, 51, &lo, &hi); CHECK(lo == vmin && hi == vmax);       // out of range
    window(3001, vmin, vmax, 10, &lo, &hi); CHECK(lo == 2700 && hi == 3300);       // +-10 %
    window(1619, vmin, vmax, 10, &lo, &hi); CHECK(lo == vmin && hi == 1780);       // at 240: can't be shorter
    window(6476, vmin, vmax, 10, &lo, &hi); CHECK(lo == 5827 && hi == vmax);       // at the floor: can't be longer
    window(9000, vmin, vmax, 10, &lo, &hi); CHECK(lo == vmax && hi == vmax);       // beyond the range: clamped
    window(3000, vmin, vmin, 10, &lo, &hi); CHECK(lo == vmin && hi == vmin);       // empty range
    // From the floor to 240 at 10 %: frames shorten step by step, never more than 10 % per step, and get there
    uint32_t l = 6476, steps = 0, prev = 6475;
    while (steps < 100) {
        window(l, vmin, vmax, 10, &lo, &hi);
        if (prev && (uint64_t)lo * 100 < (uint64_t)prev * 89) { CHECK(false); break; }
        prev = lo; l = lo + 1; steps++;
        if (lo == vmin) break;
    }
    CHECK(lo == vmin && steps >= 10 && steps <= 20);
    {   // b148's default, 5 %: about twice the steps of 10 %
        uint32_t l5 = 6476, n5 = 0;
        do { window(l5, vmin, vmax, 5, &lo, &hi); l5 = lo + 1; n5++; } while (lo != vmin && n5 < 100);
        CHECK(lo == vmin && n5 >= 25 && n5 <= 30);
    }
    CHECK(maxFirst(2700, 3300, 2000) && !maxFirst(1000, 1200, 2000));
    std::printf("vrr_ramp_test: %d/%d checks passed\n", gN - gF, gN);
    return gF ? 1 : 0;
}
