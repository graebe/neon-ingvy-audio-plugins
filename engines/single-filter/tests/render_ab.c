// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * THE TWO PATHS, RENDERED SIDE BY SIDE AND PINNED, for every product: the
 * plugin's float path (sf_core_process_f32) and the Move's int16 path
 * (sf_core_process_i16) on the same patch and input, within one and a half
 * sixteen-bit steps of each other, and the float render's hash recorded
 * (render_golden.h). `--dump` prints the hashes and the worst differences
 * instead of checking them.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "render_golden.h"
#include "sf_core.h"

#define SR     SF_RENDER_SR
#define BLOCK  SF_RENDER_BLOCK
#define FRAMES SF_RENDER_FRAMES

static const char* const names[10] = { "", "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Band Pass", "Notch", "All Pass", "Tilt" };

static void configure (SfCore* c, uint32_t shape)
{
    for (int i = 0; i < SF_RENDER_PATCH_COUNT; i++)
    {
        const int index = sf_field_index (shape, (uint32_t) SF_RENDER_PATCH[i][0]);
        if (index >= 0)
            sf_core_set_num (c, index, SF_RENDER_PATCH[i][1]);
    }
    /* The patch from the first sample, not faded in from the defaults. */
    sf_core_reset (c);
}

int main (int argc, char** argv)
{
    const int dump = (argc > 1 && strcmp (argv[1], "--dump") == 0);
    static float in_l[FRAMES], in_r[FRAMES];
    static float pl[FRAMES], pr[FRAMES];
    static int16_t mv[FRAMES * 2];
    sf_render_input (in_l, in_r, FRAMES);
    int fail = 0;

    for (uint32_t shape = 1; shape <= 9; ++shape)
    {
        /* ---- the plugin's path: float, split ---- */
        SfCore* plug = sf_core_new (shape, SR);
        configure (plug, shape);
        memcpy (pl, in_l, sizeof pl);
        memcpy (pr, in_r, sizeof pr);
        for (int off = 0; off < FRAMES; off += BLOCK)
        {
            const int n = (FRAMES - off < BLOCK) ? FRAMES - off : BLOCK;
            sf_core_process_f32 (plug, pl + off, pr + off, (uint32_t) n);
        }
        sf_core_free (plug);

        /* ---- the Move's path: int16 interleaved ---- */
        SfCore* move = sf_core_new (shape, SR);
        configure (move, shape);
        for (int i = 0; i < FRAMES; i++)
        {
            mv[i * 2] = (int16_t) lrintf (in_l[i] * 32768.0f);
            mv[i * 2 + 1] = (int16_t) lrintf (in_r[i] * 32768.0f);
        }
        for (int off = 0; off < FRAMES; off += BLOCK)
        {
            const int n = (FRAMES - off < BLOCK) ? FRAMES - off : BLOCK;
            sf_core_process_i16 (move, mv + off * 2, (uint32_t) n);
        }
        sf_core_free (move);

        /* ---- the A/B, both channels, against the float clipped as int16
         * is: half an LSB of input rounding through the filter's gain (at
         * most +4.5 dB, less Output), and half of output rounding. ---- */
        int worst_at = -1;
        double worst = 0.0;
        for (int i = 0; i < FRAMES * 2; i++)
        {
            const float f = (i % 2 == 0) ? pl[i / 2] : pr[i / 2];
            double want = (double) f * 32768.0;
            if (want > 32767.0) want = 32767.0;
            if (want < -32768.0) want = -32768.0;
            const double d = fabs ((double) mv[i] - want);
            if (d > worst)
            {
                worst = d;
                worst_at = i / 2;
            }
        }

        const uint64_t h = sf_render_fnv1a (SF_RENDER_FNV_START, pl, sizeof pl);
        if (dump)
        {
            printf ("%-10s  hash 0x%016llXull  worst A/B %.3f LSB at sample %d\n", names[shape], (unsigned long long) h, worst, worst_at);
            continue;
        }
        if (worst > 1.5)
        {
            fprintf (stderr, "%s: A/B FAILED: the two paths differ by %.3f LSB at sample %d\n", names[shape], worst, worst_at);
            fail = 1;
        }
        if (SF_RENDER_GOLDEN[shape] == 0ull)
        {
            fprintf (stderr, "%s: GOLDEN is unset -- run with --dump and record the hash.\n", names[shape]);
            fail = 1;
        }
        else if (h != SF_RENDER_GOLDEN[shape])
        {
            fprintf (stderr,
                     "%s: GOLDEN FAILED: 0x%016llX, expected 0x%016llX\n"
                     "  the DSP changed. Re-run with --dump, decide whether that was\n"
                     "  intended, and record the new value with a reason.\n",
                     names[shape], (unsigned long long) h, (unsigned long long) SF_RENDER_GOLDEN[shape]);
            fail = 1;
        }
        else
            printf ("%-10s  A/B ok (%.3f LSB), golden ok\n", names[shape], worst);
    }
    if (dump)
        return 0;
    printf (fail ? "\nFAILED\n" : "\nPASS\n");
    return fail;
}
