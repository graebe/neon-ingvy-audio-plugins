// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

#ifndef TG_RENDER_GOLDEN_H
#define TG_RENDER_GOLDEN_H

/*
 * THE TRANCE GATE'S GOLDEN RENDER, for both renderers that are held to it:
 * tests/render_plugin.c through the engine's C ABI the way a plugin calls it,
 * and tests/tg_host.cpp through the built VST3 the way a DAW hosts it. Both
 * render the same patch -- the one engines/trance-gate/tests/render_ref.c sets
 * on the Move module -- for four seconds of a 220 Hz sine quantised to int16,
 * at 44.1 kHz in blocks of 128 under a 123 BPM transport, and round the
 * gated floats back to int16.
 */
#define TG_RENDER_SR 44100.0
#define TG_RENDER_BLOCK 128
#define TG_RENDER_BPM 123.0f
#define TG_RENDER_SECONDS 4.0

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

#endif /* TG_RENDER_GOLDEN_H */
