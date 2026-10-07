// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

#ifndef SC_RENDER_GOLDEN_H
#define SC_RENDER_GOLDEN_H

/*
 * THE SIDE-CHAIN'S GOLDEN RENDER, for both renderers held to it: render_ab.c
 * through the engine's C ABI, where the plugin's path and the Move's are
 * compared, and tests/side-chain/sc_host.cpp through the built VST3 the way a
 * DAW hosts it. Both render the same patch for four seconds of the same
 * input, at 44.1 kHz in blocks of 128 under a 120 BPM transport, and hash the
 * left channel's floats.
 *
 * Cycle, so the render depends on the transport and exercises the
 * phase-locked loop rather than a queue of notes.
 */
#include <math.h>
#include <stdint.h>

#define SC_RENDER_SR 44100.0
#define SC_RENDER_BLOCK 128
#define SC_RENDER_BPM 120.0
/* Spelled as an integer rather than (int)(SR * SECONDS): a cast of a double
 * is not a constant expression, so arrays sized by it would be variable-length
 * arrays, which compile only as a GNU extension. */
#define SC_RENDER_FRAMES (44100 * 4)

/*
 * THE PATCH, as the host's fifteen plain values in the parameters' order
 * (plugins/side-chain/Params.h): Cycle at 1/4, Exponential, Delay 3 %,
 * Attack 6 %, Hold 11 %, Release 42 %, Depth 85 %; the rest at their
 * defaults, which a Cycle render does not read. render_ab.c sets the same
 * through the engine's string door.
 */
static const double SC_RENDER_PARAMS[15] = { 0, 4, 0, 3, 6, 11, 42, 85, 1, 1, 36, 0, 0, -24, 20 };

/*
 * FNV-1a over the left channel's float bytes.
 *
 * Recorded 2026-09-29, the first render. It did NOT move when the int16 path
 * was changed to round rather than truncate, which is the evidence that that
 * change touched only the Move's path and not the shared gain law.
 *
 * Nor on 2026-09-30, when the phase-locked loop became a time constant, a
 * stopped transport began RELEASING a Cycle duck instead of cutting it, Depth
 * began to glide and a stage length became safe to move mid-stage: this render
 * never stops, never moves a parameter, and its host clock is exact.
 *
 * If this fires, the DSP changed: re-run render_ab with --dump, satisfy
 * yourself that the change was intended, and record the new value WITH A
 * REASON.
 */
static const uint64_t SC_RENDER_GOLDEN = 0xF166F7CC7678B4BEull;

static inline uint64_t sc_render_fnv1a(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
    return h;
}
#define SC_RENDER_FNV_START 1469598103934665603ull

/*
 * A REPRODUCIBLE INPUT WITH TRANSIENTS IN IT.
 *
 * A pure sine would hide every timing error that lands between zero crossings.
 * This is a sine plus a cheap LCG click train, so a duck that starts a sample
 * early or late changes the bytes.
 */
static inline void sc_render_input(float *l, float *r, int frames)
{
    uint32_t seed = 0x5E1F1E1Du;
    for (int i = 0; i < frames; i++) {
        const double t = (double)i / SC_RENDER_SR;
        const float tone = (float)(0.35 * sin(2.0 * 3.14159265358979323846 * 220.0 * t));
        seed = seed * 1664525u + 1013904223u;
        /* A transient every 1024 samples, so the picture has edges. */
        const float click = (i % 1024 < 24)
            ? (float)((seed >> 9) & 0xFFFF) / 65535.0f * 0.5f - 0.25f
            : 0.0f;
        l[i] = tone + click;
        r[i] = tone - click;
    }
}

#endif /* SC_RENDER_GOLDEN_H */
