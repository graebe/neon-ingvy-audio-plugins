// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * THE TWO PATHS, RENDERED SIDE BY SIDE AND PINNED: the plugin's float path
 * (fd_core_process_f32) and the Move's int16 path (fd_core_process_i16) on
 * the same patch and input, within one sixteen-bit step of each other, and
 * the float render's hash recorded (render_golden.h). `--dump` prints the
 * hash and the worst difference instead of checking them.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fd_core.h"
#include "render_golden.h"

#define SR     FD_RENDER_SR
#define BLOCK  FD_RENDER_BLOCK
#define FRAMES FD_RENDER_FRAMES

static void configure (FdCore* c)
{
    for (int i = 0; i < FD_RENDER_PATCH_COUNT; i++)
        fd_core_set_num (c, (int) FD_RENDER_PATCH[i][0], FD_RENDER_PATCH[i][1]);
}

int main (int argc, char** argv)
{
    const int dump = (argc > 1 && strcmp (argv[1], "--dump") == 0);
    static float in_l[FRAMES], in_r[FRAMES];
    static float pl[FRAMES], pr[FRAMES];
    static int16_t mv[FRAMES * 2];
    fd_render_input (in_l, in_r, FRAMES);

    /* ---- the plugin's path: float, split ---- */
    FdCore* plug = fd_core_new (SR);
    configure (plug);
    memcpy (pl, in_l, sizeof pl);
    memcpy (pr, in_r, sizeof pr);
    for (int off = 0; off < FRAMES; off += BLOCK)
    {
        const int n = (FRAMES - off < BLOCK) ? FRAMES - off : BLOCK;
        fd_core_process_f32 (plug, pl + off, pr + off, (uint32_t) n);
    }
    fd_core_free (plug);

    /* ---- the Move's path: int16 interleaved ---- */
    FdCore* move = fd_core_new (SR);
    configure (move);
    for (int i = 0; i < FRAMES; i++)
    {
        mv[i * 2] = (int16_t) lrintf (in_l[i] * 32768.0f);
        mv[i * 2 + 1] = (int16_t) lrintf (in_r[i] * 32768.0f);
    }
    for (int off = 0; off < FRAMES; off += BLOCK)
    {
        const int n = (FRAMES - off < BLOCK) ? FRAMES - off : BLOCK;
        fd_core_process_i16 (move, mv + off * 2, (uint32_t) n);
    }
    fd_core_free (move);

    /* ---- the A/B, both channels, against the float clipped as int16 is.
     * The int16 input is the float input rounded (half an LSB), passed
     * through the patch's gain -- at most about +4 dB here, the bell, the
     * shelf and the tilt together -- and the output rounded again (half an
     * LSB): 0.5 + 0.5 x 1.6, so 1.5 LSB holds with room for nothing else. ---- */
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

    const uint64_t h = fd_render_fnv1a (FD_RENDER_FNV_START, pl, sizeof pl);
    if (dump)
    {
        printf ("plugin-path hash: 0x%016llXull\n", (unsigned long long) h);
        printf ("worst A/B difference: %.3f LSB at sample %d\n", worst, worst_at);
        return 0;
    }

    int fail = 0;
    if (worst > 1.5)
    {
        fprintf (stderr, "A/B FAILED: the two paths differ by %.3f LSB at sample %d\n", worst, worst_at);
        fail = 1;
    }
    else
        printf ("A/B ok: worst difference %.3f LSB (sample %d)\n", worst, worst_at);

    if (FD_RENDER_GOLDEN == 0ull)
    {
        fprintf (stderr, "GOLDEN is unset -- run with --dump and record the hash.\n");
        fail = 1;
    }
    else if (h != FD_RENDER_GOLDEN)
    {
        fprintf (stderr,
                 "GOLDEN FAILED: 0x%016llX, expected 0x%016llX\n"
                 "  the DSP changed. Re-run with --dump, decide whether that was\n"
                 "  intended, and record the new value with a reason.\n",
                 (unsigned long long) h, (unsigned long long) FD_RENDER_GOLDEN);
        fail = 1;
    }
    else
        printf ("golden ok: 0x%016llX\n", (unsigned long long) h);

    printf (fail ? "\nFAILED\n" : "\nPASS\n");
    return fail;
}
