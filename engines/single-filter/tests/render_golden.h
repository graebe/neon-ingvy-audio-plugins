// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

#ifndef SF_RENDER_GOLDEN_H
#define SF_RENDER_GOLDEN_H

/*
 * THE SINGLE FILTERS' GOLDEN RENDERS, for render_ab.c: each product's
 * plugin float path and Move int16 path through the engine's C ABI, the same
 * patch, two seconds of the same input at 44.1 kHz in blocks of 128, and the
 * left channel's floats hashed -- one hash a product.
 *
 * Each product's patch moves what it has off its default: Freq, Q, the
 * slope, the family (Chebyshev I, with Ripple), Gain, Channel (Mid) and
 * Output -- so a change to its design, its glides or its lanes moves its
 * hash. The input is a seeded generator's, the same every time.
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define SF_RENDER_SR 44100.0
#define SF_RENDER_BLOCK 128
#define SF_RENDER_FRAMES (44100 * 2)

/* THE PATCH, as (field, plain value): a field the product lacks is skipped. */
#define SF_RENDER_PATCH_COUNT 8
static const double SF_RENDER_PATCH[SF_RENDER_PATCH_COUNT][2] = {
    { 0, 1500.0 },   /* Freq */
    { 1, 4.5 },      /* Gain +4.5 dB */
    { 2, 1.8 },      /* Q */
    { 4, 3 },        /* Slope, 24 dB/oct (an All Pass: 4th order) */
    { 5, 2 },        /* Family: Chebyshev I */
    { 6, 0.5 },      /* Ripple */
    { 9, 3 },        /* Channel: Mid */
    { 10, -1.5 },    /* Output */
};

/* A chirp and a breath of noise, different on the two channels. */
static void sf_render_input (float* l, float* r, int frames)
{
    uint32_t s = 0x9E3779B9u;
    double phase = 0.0;
    for (int i = 0; i < frames; ++i)
    {
        const double t = (double) i / SF_RENDER_SR;
        phase += 2.0 * 3.14159265358979323846 * (40.0 * pow (400.0, t / 2.0)) / SF_RENDER_SR;
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        const float noise = (float) ((double) s / 4294967296.0 - 0.5) * 0.1f;
        l[i] = 0.4f * (float) sin (phase) + noise;
        r[i] = 0.3f * (float) sin (1.5 * phase) - noise;
    }
}

#define SF_RENDER_FNV_START 0xCBF29CE484222325ull

static uint64_t sf_render_fnv1a (uint64_t h, const void* data, size_t n)
{
    const unsigned char* p = (const unsigned char*) data;
    for (size_t i = 0; i < n; ++i)
    {
        h ^= p[i];
        h *= 0x100000001B3ull;
    }
    return h;
}

/* FNV-1a over the left channel's float bytes, by SF_SHAPE_* (index 0 is
 * Empty, no product), recorded 2026-10-18 at the first render. A change is
 * re-recorded with --dump, with its date and its reason, never accepted
 * silently. */
static const uint64_t SF_RENDER_GOLDEN[10] = {
    0,
    0x7FFDF2B80C90D97Cull, /* Bell */
    0xD6F18651D0A96564ull, /* Low Shelf */
    0x7A73DCE3700139E3ull, /* High Shelf */
    0x8D9730115D1EE0BCull, /* High Pass */
    0x0408CA30DA77434Dull, /* Low Pass */
    0xA0E6C0C8FB7051FAull, /* Band Pass */
    0x53A6B4C9D516DF99ull, /* Notch */
    0x1CC9FB0A5BA09DB3ull, /* All Pass */
    0xFBCB7F1A37369C89ull, /* Tilt */
};

#endif
