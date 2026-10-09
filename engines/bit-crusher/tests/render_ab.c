// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * THE TWO PATHS, RENDERED SIDE BY SIDE, AND THE RESULT PINNED.
 *
 * AGENTS.md asks that every DSP exist on both the Live plugin and the Move
 * module. This makes that checkable, with two claims:
 *
 *   A/B      the plugin's path (float, split channels) and the Move's (int16
 *            interleaved) render the same, sample for sample, within the one
 *            LSB the int16 output costs -- given the same input, which is why
 *            the input is on the sixteen-bit grid (render_golden.h)
 *
 *   GOLDEN   the float render hashes to a recorded value, so a change to the
 *            shared DSP -- which moves both paths together and passes the
 *            A/B -- still has to be deliberate
 *
 * `--dump` prints the hash to re-record with, and only when the sound is
 * MEANT to change.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bc_core.h"
#include "render_golden.h"

#define SR     BC_RENDER_SR
#define BLOCK  BC_RENDER_BLOCK
#define FRAMES BC_RENDER_FRAMES

static void configure(BcCore *c)
{
    for (int i = 0; i < BC_RENDER_PARAMS_COUNT; i++)
        bc_core_set_num(c, i, BC_RENDER_PARAMS[i]);
}

int main(int argc, char **argv)
{
    const int dump = (argc > 1 && strcmp(argv[1], "--dump") == 0);
    if (bc_param_count() != BC_RENDER_PARAMS_COUNT) {
        fprintf(stderr, "the engine has %d parameters; the patch names %d\n", bc_param_count(),
                BC_RENDER_PARAMS_COUNT);
        return 1;
    }

    static float in_l[FRAMES], in_r[FRAMES];
    static float pl[FRAMES], pr[FRAMES];
    static int16_t mv[FRAMES * 2];
    bc_render_input(in_l, in_r, FRAMES);

    /* ---- the plugin's path: float, split ---- */
    BcCore *plug = bc_core_new(SR);
    configure(plug);
    memcpy(pl, in_l, sizeof pl);
    memcpy(pr, in_r, sizeof pr);
    for (int off = 0; off < FRAMES; off += BLOCK) {
        const int n = (FRAMES - off < BLOCK) ? FRAMES - off : BLOCK;
        bc_core_process_f32(plug, pl + off, pr + off, (uint32_t)n);
    }
    bc_core_free(plug);

    /* ---- the Move's path: int16 interleaved ---- */
    BcCore *move = bc_core_new(SR);
    configure(move);
    for (int i = 0; i < FRAMES; i++) {
        mv[i * 2] = (int16_t)lrintf(in_l[i] * 32768.0f);
        mv[i * 2 + 1] = (int16_t)lrintf(in_r[i] * 32768.0f);
    }
    for (int off = 0; off < FRAMES; off += BLOCK) {
        const int n = (FRAMES - off < BLOCK) ? FRAMES - off : BLOCK;
        bc_core_process_i16(move, mv + off * 2, (uint32_t)n);
    }
    bc_core_free(move);

    /* ---- the A/B, both channels, against the float clipped as int16 is ---- */
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

    const uint64_t h = bc_render_fnv1a(BC_RENDER_FNV_START, pl, sizeof pl);
    if (dump) {
        printf("plugin-path hash: 0x%016llXull\n", (unsigned long long)h);
        printf("worst A/B difference: %.3f LSB at sample %d\n", worst, worst_at);
        return 0;
    }

    int fail = 0;
    /* Half an LSB is the int16 rounding; the float's own rounding adds a
     * hair. One LSB leaves room for that and nothing else. */
    if (worst > 1.0) {
        fprintf(stderr, "A/B FAILED: the two paths differ by %.3f LSB at sample %d\n", worst, worst_at);
        fail = 1;
    } else {
        printf("A/B ok: worst difference %.3f LSB (sample %d)\n", worst, worst_at);
    }

    if (BC_RENDER_GOLDEN == 0ull) {
        fprintf(stderr, "GOLDEN is unset -- run with --dump and record the hash.\n");
        fail = 1;
    } else if (h != BC_RENDER_GOLDEN) {
        fprintf(stderr,
                "GOLDEN FAILED: 0x%016llX, expected 0x%016llX\n"
                "  the DSP changed. Re-run with --dump, decide whether that was\n"
                "  intended, and record the new value with a reason.\n",
                (unsigned long long)h, (unsigned long long)BC_RENDER_GOLDEN);
        fail = 1;
    } else {
        printf("golden ok: 0x%016llX\n", (unsigned long long)h);
    }

    printf(fail ? "\nFAILED\n" : "\nPASS\n");
    return fail;
}
