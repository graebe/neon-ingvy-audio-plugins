// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

#ifndef FD_RENDER_GOLDEN_H
#define FD_RENDER_GOLDEN_H

/*
 * NI FILTER DESIGNER'S GOLDEN RENDER, for render_ab.c: the plugin's float
 * path and the Move's int16 path through the engine's C ABI, the same patch,
 * four seconds of the same input at 44.1 kHz in blocks of 128, and the left
 * channel's floats hashed.
 *
 * Every kind of thing the engine does is in the patch: the isolator at 48 dB
 * with a band down, an elliptic low cut, a bell on the mid only, a Chebyshev
 * II notch, a third-order high shelf, a Bessel high cut, an all-pass on the
 * side, a tilt, and Output -- so a change to any of them moves the hash. The
 * input is a seeded generator's, the same every time.
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define FD_RENDER_SR 44100.0
#define FD_RENDER_BLOCK 128
#define FD_RENDER_FRAMES (44100 * 4)

/* THE PATCH, as (index, plain value) pairs in the table's order: the rest of
 * the 174 at their defaults. Node n's field f is 14 + 10 n + f; the fields
 * are type, on, family, order, freq, gain, q, ripple, stopband, channel. */
#define FD_RENDER_PATCH_COUNT 36
static const double FD_RENDER_PATCH[FD_RENDER_PATCH_COUNT][2] = {
    { 0, -1.0 },                                  /* Output -1 dB */
    { 4, 1 }, { 5, -3.0 }, { 13, 1 },             /* isolator on, Low -3 dB, 48 dB */
    { 14, 4 }, { 16, 4 }, { 17, 5 }, { 18, 60 }, { 21, 0.5 }, { 22, 50 },  /* low cut, elliptic, 36 dB, 60 Hz */
    { 24, 1 }, { 28, 800 }, { 29, 4 }, { 30, 2 }, { 33, 3 },               /* bell, 800 Hz, +4, Q 2, mid */
    { 34, 7 }, { 36, 3 }, { 37, 1 }, { 38, 3000 }, { 40, 3 }, { 42, 40 },  /* notch, Chebyshev II, 3 kHz */
    { 44, 3 }, { 47, 2 }, { 48, 8000 }, { 49, -3 },                         /* high shelf, order 3, 8 kHz, -3 */
    { 54, 5 }, { 56, 1 }, { 57, 3 }, { 58, 15000 },                         /* high cut, Bessel, 24 dB, 15 kHz */
    { 64, 8 }, { 67, 2 }, { 68, 1000 }, { 73, 4 },                          /* all-pass, order 3, 1 kHz, side */
    { 74, 9 }, { 78, 1000 }, { 79, 2 },                                     /* tilt, +2 dB at 1 kHz */
};

/* A chirp and a breath of noise, different on the two channels. */
static void fd_render_input (float* l, float* r, int frames)
{
    uint32_t s = 0x9E3779B9u;
    double phase = 0.0;
    for (int i = 0; i < frames; ++i)
    {
        const double t = (double) i / FD_RENDER_SR;
        phase += 2.0 * 3.14159265358979323846 * (40.0 * pow (400.0, t / 4.0)) / FD_RENDER_SR;
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        const float noise = (float) ((double) s / 4294967296.0 - 0.5) * 0.1f;
        l[i] = 0.4f * (float) sin (phase) + noise;
        r[i] = 0.3f * (float) sin (1.5 * phase) - noise;
    }
}

#define FD_RENDER_FNV_START 0xCBF29CE484222325ull

static uint64_t fd_render_fnv1a (uint64_t h, const void* data, size_t n)
{
    const unsigned char* p = (const unsigned char*) data;
    for (size_t i = 0; i < n; ++i)
    {
        h ^= p[i];
        h *= 0x100000001B3ull;
    }
    return h;
}

/* FNV-1a over the left channel's float bytes, recorded 2026-10-12 at the
 * first render. A change is re-recorded with --dump, with its date and its
 * reason, never accepted silently. */
#define FD_RENDER_GOLDEN 0x2789938177B46ACAull

#endif
