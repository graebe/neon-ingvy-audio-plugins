// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * THE TWO PATHS, RENDERED SIDE BY SIDE, AND THE RESULTS PINNED.
 *
 * AGENTS.md asks that every DSP exist on both the Live plugin and the Move
 * module. This makes that checkable, with two claims per patch
 * (render_golden.h):
 *
 *   A/B      the plugin's path (float, split channels) and the Move's (int16
 *            interleaved) render the same, sample for sample, within the one
 *            LSB the int16 output costs -- given the same input, which is why
 *            the input is on the sixteen-bit grid
 *
 *   GOLDEN   the float render hashes to a recorded value, so a change to the
 *            shared DSP -- which moves both paths together and passes the
 *            A/B -- still has to be deliberate
 *
 * The Time patch is the Move's engine; the Spectral one runs here through the
 * plugin's archive on both paths, which holds its int16 door to the float one
 * too.
 *
 * `--dump` prints the hashes to re-record with, and only when the sound is
 * MEANT to change.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "render_golden.h"
#include "vz_core.h"

#define SR     VZ_RENDER_SR
#define BLOCK  VZ_RENDER_BLOCK
#define FRAMES VZ_RENDER_FRAMES

static float in_l[FRAMES], in_r[FRAMES];
static float pl[FRAMES], pr[FRAMES];
static int16_t mv[FRAMES * 2];

static void configure(VzCore *c, const double *patch)
{
    for (int i = 0; i < VZ_RENDER_PARAMS_COUNT; i++)
        vz_core_set_num(c, i, patch[i]);
}

/* One patch through both paths: the A/B and the golden. 0 when both hold. */
static int run(const char *name, const double *patch, uint64_t golden, int dump)
{
    VzCore *plug = vz_core_new(SR);
    configure(plug, patch);
    memcpy(pl, in_l, sizeof pl);
    memcpy(pr, in_r, sizeof pr);
    for (int off = 0; off < FRAMES; off += BLOCK) {
        const int n = (FRAMES - off < BLOCK) ? FRAMES - off : BLOCK;
        vz_core_process_f32(plug, pl + off, pr + off, (uint32_t)n);
    }
    vz_core_free(plug);

    VzCore *move = vz_core_new(SR);
    configure(move, patch);
    for (int i = 0; i < FRAMES; i++) {
        mv[i * 2] = (int16_t)lrintf(in_l[i] * 32768.0f);
        mv[i * 2 + 1] = (int16_t)lrintf(in_r[i] * 32768.0f);
    }
    for (int off = 0; off < FRAMES; off += BLOCK) {
        const int n = (FRAMES - off < BLOCK) ? FRAMES - off : BLOCK;
        vz_core_process_i16(move, mv + off * 2, (uint32_t)n);
    }
    vz_core_free(move);

    int worst_at = -1;
    double worst = 0.0;
    for (int i = 0; i < FRAMES * 2; i++) {
        const float f = (i % 2 == 0) ? pl[i / 2] : pr[i / 2];
        double want = (double)f * 32768.0;
        if (want > 32767.0) want = 32767.0;
        if (want < -32768.0) want = -32768.0;
        const double d = fabs((double)mv[i] - want);
        if (d > worst) {
            worst = d;
            worst_at = i / 2;
        }
    }

    const uint64_t h = vz_render_fnv1a(VZ_RENDER_FNV_START, pl, sizeof pl);
    if (dump) {
        printf("%s: plugin-path hash 0x%016llXull, worst A/B %.3f LSB at sample %d\n", name,
               (unsigned long long)h, worst, worst_at);
        return 0;
    }

    int fail = 0;
    /* Half an LSB is the int16 rounding; the float's own rounding adds a
     * hair. One LSB leaves room for that and nothing else. */
    if (worst > 1.0) {
        fprintf(stderr, "%s: A/B FAILED: the two paths differ by %.3f LSB at sample %d\n", name, worst, worst_at);
        fail = 1;
    } else {
        printf("%s: A/B ok, worst difference %.3f LSB (sample %d)\n", name, worst, worst_at);
    }
    if (golden == 0ull) {
        fprintf(stderr, "%s: GOLDEN is unset -- run with --dump and record the hash.\n", name);
        fail = 1;
    } else if (h != golden) {
        fprintf(stderr,
                "%s: GOLDEN FAILED: 0x%016llX, expected 0x%016llX\n"
                "  the DSP changed. Re-run with --dump, decide whether that was\n"
                "  intended, and record the new value with a reason.\n",
                name, (unsigned long long)h, (unsigned long long)golden);
        fail = 1;
    } else {
        printf("%s: golden ok, 0x%016llX\n", name, (unsigned long long)h);
    }
    return fail;
}

int main(int argc, char **argv)
{
    const int dump = (argc > 1 && strcmp(argv[1], "--dump") == 0);
    if (vz_param_count() != VZ_RENDER_PARAMS_COUNT) {
        fprintf(stderr, "the engine has %d parameters; the patches name %d\n", vz_param_count(),
                VZ_RENDER_PARAMS_COUNT);
        return 1;
    }
    vz_render_input(in_l, in_r, FRAMES);
    int fail = run("time", VZ_RENDER_TIME, VZ_RENDER_GOLDEN_TIME, dump);
    fail |= run("spectral", VZ_RENDER_SPECTRAL, VZ_RENDER_GOLDEN_SPECTRAL, dump);
    if (!dump)
        printf(fail ? "\nFAILED\n" : "\nPASS\n");
    return fail;
}
