// SPDX-License-Identifier: MIT
// cpuclk.c: the effective core clock, measured from a chain of dependent 64-bit adds (one per cycle on Zen 4).
// The OS needs no sensor or MSR for it, so the same binary compares macOS and Linux on the XTX machine.
//
//   cc -O2 -o cpuclk tools/sensors/cpuclk.c -lpthread
//   ./cpuclk [threads=1] [seconds=5]        one GHz value per thread per 0.5 s window, then min / median / max
//   PER=1 ./cpuclk 24 6                     also print every thread's windows
//   AFF=2 PER=1 ./cpuclk 4 5                macOS: affinity tags 1, 2, 1, 2 ... (xnu puts different tags on different
//                                           last-level caches = different CCDs on the 7900X3D)
//   taskset -c 6 ./cpuclk 1 5               Linux: pin to one core (check which CPUs share an L3:
//                                           /sys/devices/system/cpu/cpu*/cache/index3/shared_cpu_list)
//
// It is a clock probe, not a power virus: one ALU busy per thread (24 threads drew ~78 W, a shell loop 120 W).
// Each window is 0.5 s; a thread that migrates mid-window reports the average of both cores.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/thread_policy.h>
#endif

#define ADD8 "add $1,%0\n add $1,%0\n add $1,%0\n add $1,%0\n add $1,%0\n add $1,%0\n add $1,%0\n add $1,%0\n"
#define ADD64 ADD8 ADD8 ADD8 ADD8 ADD8 ADD8 ADD8 ADD8
#define MAXW 256

static double gSec = 5;
static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

typedef struct { int id; double ghz[MAXW]; int n; } W;

static void *run(void *p) {
    W *w = p;
#ifdef __APPLE__
    const char *aff = getenv("AFF");
    if (aff && atoi(aff) > 0) {
        thread_affinity_policy_data_t a = { (integer_t)(w->id % atoi(aff) + 1) };
        thread_policy_set(mach_thread_self(), THREAD_AFFINITY_POLICY, (thread_policy_t)&a, THREAD_AFFINITY_POLICY_COUNT);
    }
#endif
    const double end = now() + gSec;
    while (now() < end && w->n < MAXW) {
        unsigned long long x = 0, iters = 0;
        const double t0 = now();
        double t1;
        do {
            for (int i = 0; i < 4096; i++)
                __asm__ volatile(ADD64 ADD64 ADD64 ADD64 : "+r"(x));   // 256 dependent adds
            iters += 4096;
            t1 = now();
        } while (t1 - t0 < 0.5);
        w->ghz[w->n++] = (double)iters * 256 / (t1 - t0) * 1e-9;
    }
    return NULL;
}

static int cmp(const void *a, const void *b) {
    const double d = *(const double *)a - *(const double *)b;
    return (d > 0) - (d < 0);
}

int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 1;
    if (n < 1 || n > 256) { fprintf(stderr, "threads: 1-256\n"); return 2; }
    if (argc > 2) gSec = atof(argv[2]);
    pthread_t t[256];
    W *w = calloc((size_t)n, sizeof(W));
    double *all = calloc((size_t)n * MAXW, sizeof(double));
    if (!w || !all) return 1;
    for (int i = 0; i < n; i++) { w[i].id = i; pthread_create(&t[i], NULL, run, &w[i]); }
    for (int i = 0; i < n; i++) pthread_join(t[i], NULL);
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (n <= 4 || getenv("PER")) {
            printf("thread %d:", i);
            for (int k = 0; k < w[i].n; k++) printf(" %.2f", w[i].ghz[k]);
            printf("\n");
        }
        for (int k = 0; k < w[i].n; k++) all[m++] = w[i].ghz[k];
    }
    if (!m) return 1;
    qsort(all, (size_t)m, sizeof(double), cmp);
    printf("threads %d windows %d GHz min %.2f median %.2f max %.2f\n", n, m, all[0], all[m / 2], all[m - 1]);
    return 0;
}
