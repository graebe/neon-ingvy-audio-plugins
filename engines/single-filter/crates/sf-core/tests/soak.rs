// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * THE SOAK: every product, a minute of noise at 48 kHz each -- nine minutes
 * in all -- its frequency swept, its family, slope and channel changed as
 * automation would, with NaN, ±Inf and denormals injected into the input.
 * Asserted: the output stays finite, and it is bit for bit what an engine fed
 * zeros in place of the non-finite samples plays -- so nothing the bad
 * samples reached survives them. Then silence rings the filter down to
 * exactly empty.
 *
 * WHAT A HOST IS HANDED IN THE SILENCE. The filter works in f64, whose state
 * the node flushes below 1e-30 at each block's end, so nothing inside it is
 * ever subnormal. A fast ring -- a first-order cut near 9 kHz falls by 0.3 a
 * sample -- does pass, within one block, through the range below f32's
 * smallest normal on its way to the flush, and a float written there is
 * subnormal. Every shell runs the engine under flush-to-zero (ni::Processor's
 * juce::ScopedNoDenormals; the Move's no_denormals), which writes zero
 * instead; this test runs without it, so it asserts finite output and the
 * empty end.
 *
 * The full tier's (sf_soak_rs, in release):
 *   cargo test --release -p sf-core --test soak -- --ignored
 */

use sf_core::{tail_seconds, Engine, Field, SHAPES};

const FS: f64 = 48_000.0;
const BLOCK: usize = 512;

#[test]
#[ignore = "nine minutes of audio: the full tier runs it, in release"]
fn a_minute_of_noise_with_nan_inf_and_denormals_through_each_product() {
    for shape in SHAPES {
        let (mut dirty, mut clean) = (Engine::new(shape, FS).unwrap(), Engine::new(shape, FS).unwrap());
        let blocks = (60.0 * FS) as usize / BLOCK;
        let mut seed = 0x9e37_79b9_7f4a_7c15u64;
        let mut noise = || {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            (seed >> 40) as f32 / (1u64 << 24) as f32 - 0.5
        };
        let bad = [f32::NAN, f32::INFINITY, f32::NEG_INFINITY, -f32::NAN];
        for b in 0..blocks {
            if b % 100 == 0 {
                let k = b / 100;
                for e in [&mut dirty, &mut clean] {
                    e.set(Field::Freq, 20.0 * 1000f64.powf((k % 64) as f64 / 63.0));
                    e.set(Field::Family, (k % 6) as f64);
                    e.set(Field::Order, (k % 8) as f64);
                    e.set(Field::Channel, (k / 4 % 5) as f64);
                    e.set(Field::Gain, if k % 2 == 0 { 12.0 } else { -18.0 });
                    e.set(Field::Q, 0.3 + (k % 20) as f64);
                    e.set(Field::Matched, (k / 7 % 2) as f64);
                }
            }
            let mut l: Vec<f32> = (0..BLOCK).map(|_| noise()).collect();
            let mut r: Vec<f32> = (0..BLOCK).map(|_| noise()).collect();
            if b % 97 == 3 {
                for k in 0..8 {
                    l[(k * 61 + b) % BLOCK] = bad[k % 4];
                    r[(k * 37 + b) % BLOCK] = bad[(k + 1) % 4];
                    l[(k * 13 + 5) % BLOCK] = 1e-41; // a denormal
                }
            }
            let zero = |x: &f32| if x.is_finite() { *x } else { 0.0 };
            let (mut cl, mut cr): (Vec<f32>, Vec<f32>) = (l.iter().map(zero).collect(), r.iter().map(zero).collect());
            dirty.process_f32(&mut l, Some(&mut r));
            clean.process_f32(&mut cl, Some(&mut cr));
            assert!(l.iter().chain(&r).all(|x| x.is_finite()), "{shape:?} block {b}: a non-finite sample out");
            assert!(l == cl && r == cr, "{shape:?} block {b}: the bad samples left a trace");
        }
        let tail = tail_seconds(shape, dirty.values(), FS);
        let mut silence = 0.0;
        while silence < 10.0 * tail + 1.0 {
            let (mut l, mut r) = (vec![0.0f32; BLOCK], vec![0.0f32; BLOCK]);
            dirty.process_f32(&mut l, Some(&mut r));
            assert!(l.iter().chain(&r).all(|x| x.is_finite()), "{shape:?}: a non-finite sample at {silence:.1} s of silence");
            silence += BLOCK as f64 / FS;
        }
        assert!(dirty.is_silent(), "{shape:?}: {silence:.1} s of silence leave the filter empty");
    }
}
