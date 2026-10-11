// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

#ifndef BC_RENDER_GOLDEN_H
#define BC_RENDER_GOLDEN_H

/*
 * THE BIT-CRUSHER'S GOLDEN RENDER, for every renderer held to it: render_ab.c
 * through the engine's C ABI, where the plugin's path and the Move's are
 * compared, and tests/bit-crusher/bc_host.cpp through the built VST3 the way
 * a DAW hosts it. Each renders the same patch for four seconds of the same
 * input, at 44.1 kHz in blocks of 128, and hashes the left channel's floats.
 *
 * Every stage of the crusher is in the patch -- Drive, fractional Bits, Shape,
 * Mid-Rise, Dither, Anti-Alias, a Rate that does not divide 44.1 kHz, Jitter,
 * both filters and Octave, Mix and Output -- so a change to any of them moves
 * the hash. The generators are seeded, so the noise in it is the same noise
 * every time.
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define BC_RENDER_SR 44100.0
#define BC_RENDER_BLOCK 128
/* An integer, not (int)(SR * SECONDS), so arrays sized by it are not VLAs. */
#define BC_RENDER_FRAMES (44100 * 4)

/* THE PATCH, as the host's thirteen plain values in the parameters' order
 * (bc_param_*): Drive +6 dB, 5.5 bits, Shape 30 %, Mid-Rise on, Dither 20 %,
 * Anti-Alias on, Rate 7 kHz, Jitter 25 %, both filters on, Octave +0.5,
 * Mix 80 %, Output -2 dB. */
#define BC_RENDER_PARAMS_COUNT 13
static const double BC_RENDER_PARAMS[BC_RENDER_PARAMS_COUNT] = { 6, 5.5, 30, 1, 20, 1, 7000, 25, 1, 1, 0.5, 80, -2 };

/*
 * FNV-1a over the left channel's float bytes, recorded 2026-10-10 at the
 * first render (0x766762EA46AB4609), and again the same day, after the DSP
 * review: the crusher now runs 32 samples late (bc_latency), Anti-Alias --
 * on in this patch -- is the band-limited hold instead of ADAA, and the
 * pre-filter is a 10th-order inverse Chebyshev instead of a Butterworth. All
 * three are meant to change the sound.
 *
 * If this fires, the DSP changed: re-run bc_render_ab with --dump, satisfy
 * yourself that the change was intended, and record the new value WITH A
 * REASON.
 */
static const uint64_t BC_RENDER_GOLDEN = 0x59C0F292549A3A50ull;

static inline uint64_t bc_render_fnv1a(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
    return h;
}
#define BC_RENDER_FNV_START 1469598103934665603ull

/*
 * THE INPUT: a sine and a click train, ON THE SIXTEEN-BIT GRID.
 *
 * Every sample is a whole number of 1/32768ths, so the Move's int16 path and
 * the plugin's float path see the very same numbers. Off the grid they would
 * differ by less than a sixteen-bit step -- and a crusher is a staircase,
 * where that can be the difference between one step and the next.
 */
static inline void bc_render_input(float *l, float *r, int frames)
{
    uint32_t seed = 0xB17C2u;
    for (int i = 0; i < frames; i++) {
        const double t = (double)i / BC_RENDER_SR;
        const double tone = 0.45 * sin(2.0 * 3.14159265358979323846 * 220.0 * t);
        seed = seed * 1664525u + 1013904223u;
        const double click = (i % 1024 < 24) ? (double)((seed >> 9) & 0xFFFF) / 65535.0 * 0.4 - 0.2 : 0.0;
        l[i] = (float)((double)lrint((tone + click) * 32768.0) / 32768.0);
        r[i] = (float)((double)lrint((tone - click) * 32768.0) / 32768.0);
    }
}

#endif /* BC_RENDER_GOLDEN_H */
