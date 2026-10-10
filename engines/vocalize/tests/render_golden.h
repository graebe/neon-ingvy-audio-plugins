// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

#ifndef VZ_RENDER_GOLDEN_H
#define VZ_RENDER_GOLDEN_H

/*
 * NI VOCALIZE'S GOLDEN RENDERS, for every renderer held to them: render_ab.c
 * through the engine's C ABI, where the plugin's path and the Move's are
 * compared, and tests/vocalize/vz_host.cpp through the built VST3 the way a
 * DAW hosts it. Each renders a patch for four seconds of the same input, at
 * 44.1 kHz in blocks of 128, and hashes the left channel's floats.
 *
 * TWO PATCHES, one per engine. Every stage is in each -- Excite, the vowel
 * between two of the five, Size, Resonance, Vocal short of the whole way, Mix
 * and Output -- so a change to any of them moves a hash. The Time patch is the
 * one the Move can play; the Spectral one is the plugin's alone.
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define VZ_RENDER_SR 44100.0
#define VZ_RENDER_BLOCK 128
/* An integer, not (int)(SR * SECONDS), so arrays sized by it are not VLAs. */
#define VZ_RENDER_FRAMES (44100 * 4)

/* THE PATCHES, as the host's nine plain values in the parameters' order
 * (vz_param_*): Mode, Vocal 85 %, Vowel E>I 70 %, Size +30 %, Resonance
 * 65 %, Excite 40 %, Mix 90 %, Output -3 dB, Voice 100 % -- of the textbook
 * voice, as no voice is captured: the renders were recorded before Voice
 * existed, and did not move when it came. */
#define VZ_RENDER_PARAMS_COUNT 9
static const double VZ_RENDER_TIME[VZ_RENDER_PARAMS_COUNT] = { 0, 85, 1.7, 30, 65, 40, 90, -3, 100 };
static const double VZ_RENDER_SPECTRAL[VZ_RENDER_PARAMS_COUNT] = { 1, 85, 1.7, 30, 65, 40, 90, -3, 100 };

/*
 * FNV-1a over the left channel's float bytes, recorded 2026-10-10 at the
 * first render.
 *
 * If one fires, the DSP changed: re-run vz_render_ab with --dump, satisfy
 * yourself that the change was intended, and record the new value WITH A
 * REASON.
 */
static const uint64_t VZ_RENDER_GOLDEN_TIME = 0xBD9CEE92AE266486ull;
static const uint64_t VZ_RENDER_GOLDEN_SPECTRAL = 0x68AE64588DDF0663ull;

static inline uint64_t vz_render_fnv1a(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
    return h;
}
#define VZ_RENDER_FNV_START 1469598103934665603ull

/*
 * THE INPUT: a sawtooth and a burst of noise, ON THE SIXTEEN-BIT GRID.
 *
 * A saw for the harmonics a vowel is put on, noise for the breath. Every
 * sample is a whole number of 1/32768ths, so the Move's int16 path and the
 * plugin's float path see the very same numbers.
 */
static inline void vz_render_input(float *l, float *r, int frames)
{
    uint32_t seed = 0x5EEDu;
    for (int i = 0; i < frames; i++) {
        const double t = (double)i / VZ_RENDER_SR;
        const double phase = fmod(110.0 * t, 1.0);
        const double saw = 0.35 * (2.0 * phase - 1.0);
        seed = seed * 1664525u + 1013904223u;
        const double noise = (i % 22050 < 4410) ? (double)((seed >> 9) & 0xFFFF) / 65535.0 * 0.2 - 0.1 : 0.0;
        l[i] = (float)((double)lrint((saw + noise) * 32768.0) / 32768.0);
        r[i] = (float)((double)lrint((saw - noise) * 32768.0) / 32768.0);
    }
}

#endif /* VZ_RENDER_GOLDEN_H */
