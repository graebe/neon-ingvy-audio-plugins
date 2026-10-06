// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Render the gate through the PLUGIN's audio path and write s16le stereo.
 *
 * The point is an A/B against the Move module's reference render. Same patch,
 * same input, same fake transport -- but float, split channels, and the
 * transport arriving as a struct the way a DAW supplies it, rather than
 * int16 interleaved from two host callbacks.
 *
 * If this diverges from the module's render, the port changed the sound, and
 * the difference is what says where.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "trance_gate_core.h"

#define SR    44100.0
#define BLOCK 128

/*
 * FNV-1a over the rendered bytes, so the A/B can be a REGISTERED TEST rather
 * than a pipe into md5 that somebody has to remember to type. The expected
 * value is the Move module's own reference render -- the same four seconds of
 * the same patch that engines/trance-gate/tests/render_ref.c produces, whose
 * md5 is d8389d25abb3c44b34461f3029f6ab48. The two paths are different (int16
 * interleaved there, float split and rounded here) and the BYTES are the same,
 * so this constant is also the FNV-1a of that file.
 *
 * If this fires, the port changed the sound. Pipe both renderers to files and
 * `cmp` them: the first differing byte says which step.
 */
/* Re-recorded 2026-09-23 with the per-step level latched at gate-open; the
 * Move reference moved with it, for the same reason and by the same bytes.
 * Previous: 0x37792113E4834F69 (md5 8e4892aa8e3947594e91cf966f7ddc98). */
/* Re-recorded with the Width-relative stage units; the Move reference moved
 * with it, by the same 4-LSB conversion rounding and no more.
 * Previous: 0xF8D52B9F5E7FE171 */
/*
 * Re-recorded 2026-09-24 with the Rust engine, and THIS ONE IS NOT A SOUND
 * CHANGE -- it is the compiler flag leaving.
 *
 * The previous value was recorded from a clang build with FMA contraction on,
 * clang's default: `a - b*c` fused into one instruction that does not round in
 * the middle. Rust does not contract, so the same algorithm lands one LSB
 * apart in a few hundred samples of the four seconds.
 *
 * Verified rather than assumed, because "the flag did it" is exactly what a
 * real regression would like you to believe. Compiling the OLD C engine with
 * -ffp-contract=off produces this hash exactly, and with contraction left on
 * it produces the previous one:
 *
 *     C, clang default            0xA55438688E6363E5   <- was pinned here
 *     C, -ffp-contract=off        0x13190E03DAB19715
 *     Rust                        0x13190E03DAB19715
 *
 * The same thing happened to the engine repo's own golden render, which moved
 * from 4264807b9e7da87844309fa48d0cc8a3 to 3992810c52d7962b4d25b3a30494ee2e
 * for this reason and was re-measured on the Move itself. This reference's
 * whole claim is "the plugin renders what the Move module renders", and the
 * Move module is now the uncontracted build -- so the number HAD to move to
 * go on being true.
 * Previous: 0xA55438688E6363E5 */
/*
 * Re-recorded 2026-09-30 for three INTENDED sound changes, together with the
 * Move reference in engines/trance-gate/tests/run.sh, which is the same bytes
 * (d8389d25abb3c44b34461f3029f6ab48 is that render's md5; this is its FNV):
 *
 *   1. The transport start no longer drops the gate. Stopped is an open gate,
 *      and the first block used to start the envelope at zero -- 1.0 to 0.1 in
 *      one sample here, then the attack back up. The envelope is now seeded at
 *      the open gate's level. This is every difference above 1 LSB, and all of
 *      them are in frames 1..153, the first step's attack.
 *   2. The Move's int16 path ROUNDS instead of truncating (tg-core's
 *      process_i16), as sc-core's already did. Everywhere else the two
 *      renders differ by exactly 1 LSB, and only where truncation had lost
 *      it. This file's own float -> int16 conversion rounds the same way
 *      (roundf) so that it still produces the Move's bytes -- verified: the
 *      two hashes are equal again.
 *   3. The phase-locked loop is a time constant rather than a per-block
 *      fraction, and never runs the playhead backwards. This render's host
 *      clock is exact, so this one moved nothing -- stated so nobody has to
 *      wonder.
 *
 * The parameter glides (Amount, Sustain) moved nothing either: this patch
 * does not move a parameter mid-render, and the glides are inert at rest.
 * Previous: 0x13190E03DAB19715 */
#define GOLDEN_FNV1A 0xA364399720461935ULL

static uint64_t fnv = 0xcbf29ce484222325ULL;
static void fnv_add(const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) { fnv ^= b[i]; fnv *= 0x100000001b3ULL; }
}

int main(int argc, char **argv) {
    /* --verify hashes instead of writing, so the test needs no golden file
     * and no shell. Without it this still streams raw s16le for a listen. */
    int verify = (argc > 1 && strcmp(argv[1], "--verify") == 0);
    double seconds = (argc > 1 && !verify) ? atof(argv[1]) : 4.0;

    tg_core_t *c = tg_core_create(SR);

    /* The identical patch tests/render_ref.c sets on the module. */
    tg_core_set_param(c, "rate",    "1/16");
    tg_core_set_param(c, "length",  "15");
    tg_core_set_param(c, "pattern", "BEEF");
    tg_core_set_param(c, "ties",    "0022");
    tg_core_set_param(c, "attack",  "3.8267");
    tg_core_set_param(c, "decay",   "43.7333");
    tg_core_set_param(c, "sustain", "0.6");
    tg_core_set_param(c, "release", "27.3333");
    tg_core_set_param(c, "hold",    "0.75");
    tg_core_set_param(c, "amount",  "0.9");
    for (int s = 0; s < 16; s++) {
        char v[16];
        snprintf(v, sizeof(v), "%d", s);
        tg_core_set_param(c, "cursor", v);
        snprintf(v, sizeof(v), "%.3f", 0.35 + 0.04 * s);
        tg_core_set_param(c, "step_amount", v);
    }
    tg_core_set_param(c, "cursor", "0");

    const int total = (int)(seconds * SR);
    float L[BLOCK], R[BLOCK];
    int16_t out[BLOCK * 2];
    double phase = 0.0;
    const double w = 2.0 * M_PI * 220.0 / SR;

    tg_transport_t t;
    t.running = 1;
    t.beats = 0.0;
    t.bpm = 123.0f;

    for (int done = 0; done < total; done += BLOCK) {
        int n = (total - done) < BLOCK ? (total - done) : BLOCK;
        for (int i = 0; i < n; i++) {
            /* The module's renderer quantises to int16 BEFORE gating, because
             * that is the buffer Move hands it. Matching that exactly here is
             * what makes the two comparable at all -- otherwise this would be
             * measuring int16 rounding, not the port. */
            short q = (short)lrint(22000.0 * sin(phase));
            phase += w;
            L[i] = R[i] = (float)q;
        }
        tg_core_process_f32_split(c, L, R, n, &t);
        for (int i = 0; i < n; i++) {
            /* ROUNDED, half away from zero -- roundf, which is Rust's
             * f32::round, not lrintf, which rounds half to even. The Move path
             * rounds this way now, and this conversion has to be the same one
             * or the two renders differ at every exact half. */
            float l = roundf(L[i]), r = roundf(R[i]);
            out[i * 2]     = (int16_t)(l > 32767.0f ? 32767.0f : (l < -32768.0f ? -32768.0f : l));
            out[i * 2 + 1] = (int16_t)(r > 32767.0f ? 32767.0f : (r < -32768.0f ? -32768.0f : r));
        }
        if (verify) fnv_add(out, (size_t)n * 2 * sizeof(int16_t));
        else        fwrite(out, sizeof(int16_t), (size_t)n * 2, stdout);
        t.beats += (n / SR) * (t.bpm / 60.0);
    }
    tg_core_destroy(c);

    if (verify) {
        if (fnv == GOLDEN_FNV1A) {
            printf("  4s through the plugin path matches the Move render    ok\n");
            return 0;
        }
        printf("  RENDER CHANGED: got 0x%016llX want 0x%016llX\n",
               (unsigned long long)fnv, (unsigned long long)GOLDEN_FNV1A);
        return 1;
    }
    return 0;
}
