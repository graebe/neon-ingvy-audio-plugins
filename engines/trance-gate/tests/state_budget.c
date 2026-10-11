// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * THE STATE BLOB AGAINST THE AUDIO CALLBACK'S CLOCK.
 *
 * `set_param("state", ...)` and `get_param("state")` are the module's two
 * most expensive entry points: the whole patch -- eight slots, each pattern,
 * its per-step depths and arrival order, its sound -- parsed or written as
 * text. The host loads a saved state off the callback (plugin_api_v1.h), but
 * a live `state` write -- a scene, a preset recalled while playing -- and
 * every save's read land ON it, inside one 128-frame block: 2902 us at
 * 44.1 kHz, shared with everything else the Move renders.
 *
 * So this times both through the vtable the device calls, on the largest
 * blob a real patch produces -- all eight slots at 128 steps, every depth off
 * its default, every arrival order shuffled, ties and a non-default sound in
 * each -- and holds them to a fraction of that block.
 *
 * THE BUDGET IS CONSERVATIVE BECAUSE THIS IS NOT THE MOVE. run.sh runs it in
 * the module image on aarch64 (modules/_shared/test.sh), the nearest thing to
 * the device a test machine has -- but an Apple core under Docker is several
 * times a Cortex-A72's speed. A median within 5% of the block here leaves
 * room for that factor and still a small share of the device's block, and
 * the 90th percentile gets 25%, which catches a cost that is only paid every
 * few calls. The WORST call is printed and not held to anything: on a shared
 * test machine it is the scheduler's number (a preempted call reads as
 * milliseconds), and on the device the callback is SCHED_FIFO. The numbers
 * are printed every run, so a trend shows before a failure.
 *
 * Built against the release staticlib, as shipped: a debug build's timing
 * says nothing about the device.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "audio_fx_api_v2.h"

audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);

#define BLOCK_US (1e6 * MOVE_FRAMES_PER_BLOCK / MOVE_SAMPLE_RATE) /* 2902 us */
#define MEDIAN_SHARE 0.05
#define P90_SHARE 0.25
#define RUNS 400
/* What a chain keeps of an audio FX's state (MAX_FX_STATE_LEN): a blob
 * longer than this is not saved at all, so it is also the largest loaded. */
#define STATE_MAX 8192

static audio_fx_api_v2_t *api;

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void set(void *inst, const char *k, const char *v) { api->set_param(inst, k, v); }

/* Every slot at its largest: 128 steps, a rolled pattern with a shuffled
 * arrival order (randomize; a pattern written afterwards would put the order
 * back in position order, which the emitter then leaves out), then the ties
 * and every depth off their defaults, and a sound unlike the default. `seed`
 * varies the values, so two blobs built with different seeds differ
 * throughout. */
static int build_worst(void *inst, int seed, char *out) {
    char v[32];
    for (int s = 0; s < 8; s++) {
        snprintf(v, sizeof(v), "%d", s);
        set(inst, "slot", v);
        set(inst, "length", "127");
        snprintf(v, sizeof(v), "%d", seed * 8 + s + 1);
        set(inst, "randomize", v); /* a new pattern AND a shuffled order */
        set(inst, "ties", "92492492492492492492492492492492");
        for (int i = 0; i < 128; i++) {
            snprintf(v, sizeof(v), "%d", i);
            set(inst, "cursor", v);
            snprintf(v, sizeof(v), "%.3f", 0.101 + 0.007 * ((i * 37 + s * 11 + seed * 5) % 127));
            set(inst, "step_amount", v);
        }
        snprintf(v, sizeof(v), "%.4f", 3.1234 + s + seed);
        set(inst, "attack", v);
        snprintf(v, sizeof(v), "%.4f", 41.7333 - s);
        set(inst, "decay", v);
        set(inst, "sustain", seed ? "0.437" : "0.613");
        snprintf(v, sizeof(v), "%.4f", 27.3333 + s);
        set(inst, "release", v);
        set(inst, "hold", "0.73");
        set(inst, "amount", "0.91");
        set(inst, "curve", s % 2 ? "2" : "1");
        set(inst, "time_mode", s % 2 ? "1" : "0");
        set(inst, "legato", "1");
        set(inst, "fade", "0.47");
        set(inst, "fade_dir", "1");
        set(inst, "fade_soft", "1");
        set(inst, "rate", s % 2 ? "1/32" : "1/8");
    }
    set(inst, "slot", "5");
    return api->get_param(inst, "state", out, STATE_MAX);
}

/* `n` timings in us, sorted and summarised; held to the budget. */
static int report(const char *what, double *t, int n) {
    qsort(t, (size_t)n, sizeof(double), cmp_double);
    double median = t[n / 2], p90 = t[n * 9 / 10], worst = t[n - 1];
    int ok = median <= MEDIAN_SHARE * BLOCK_US && p90 <= P90_SHARE * BLOCK_US;
    printf("  %-20s median %6.1f us (%4.1f%% of a block), p90 %6.1f us (%4.1f%%), worst %7.1f us  %s\n",
           what, median, 100.0 * median / BLOCK_US, p90, 100.0 * p90 / BLOCK_US, worst, ok ? "ok" : "FAIL");
    return ok;
}

int main(void) {
    static host_api_v1_t host;
    memset(&host, 0, sizeof(host));
    host.api_version = MOVE_PLUGIN_API_VERSION;
    host.sample_rate = MOVE_SAMPLE_RATE;
    host.frames_per_block = MOVE_FRAMES_PER_BLOCK;
    api = move_audio_fx_init_v2(&host);
    if (!api) { printf("init refused the host table\n"); return 1; }

    static char a[STATE_MAX], b[STATE_MAX], got[STATE_MAX];
    void *maker = api->create_instance(NULL, NULL);
    int na = build_worst(maker, 0, a);
    api->destroy_instance(maker);
    maker = api->create_instance(NULL, NULL);
    int nb = build_worst(maker, 1, b);
    api->destroy_instance(maker);
    printf("state blob vs a %.0f us block (budget: median %.0f%%, p90 %.0f%%):\n",
           BLOCK_US, 100 * MEDIAN_SHARE, 100 * P90_SHARE);
    printf("  the largest realistic blobs: %d and %d bytes\n", na, nb);
    if (na <= 0 || nb <= 0 || na >= STATE_MAX || nb >= STATE_MAX || strcmp(a, b) == 0) {
        printf("  FAIL: two distinct blobs under %d bytes were not built\n", STATE_MAX);
        return 1;
    }

    /* A load must be a real one, or this times a refusal: each blob reads
     * back as itself from the instance it was loaded into. */
    void *inst = api->create_instance(NULL, NULL);
    const char *blob[2] = { a, b };
    for (int k = 0; k < 2; k++) {
        set(inst, "state", blob[k]);
        if (api->get_param(inst, "state", got, STATE_MAX) <= 0 || strcmp(got, blob[k]) != 0) {
            printf("  FAIL: blob %c does not read back as itself after a load\n", 'A' + k);
            return 1;
        }
    }

    /* Alternating, so every load changes every slot. One unmeasured pass
     * first: the first touch of each page is not the steady state. */
    static double load[RUNS], save[RUNS];
    for (int i = 0; i < 4; i++) set(inst, "state", blob[i % 2]);
    for (int i = 0; i < RUNS; i++) {
        double t0 = now_us();
        set(inst, "state", blob[i % 2]);
        load[i] = now_us() - t0;
    }
    for (int i = 0; i < RUNS; i++) {
        double t0 = now_us();
        int n = api->get_param(inst, "state", got, STATE_MAX);
        save[i] = now_us() - t0;
        if (n <= 0) { printf("  FAIL: get_param(\"state\") refused\n"); return 1; }
    }
    api->destroy_instance(inst);

    int ok = report("set_param(\"state\")", load, RUNS);
    ok &= report("get_param(\"state\")", save, RUNS);
    return ok ? 0 : 1;
}
