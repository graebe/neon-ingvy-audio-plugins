// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * THE SOAK: ten minutes of noise at 48 kHz through all sixteen nodes and the
 * isolator, every family and channel, their frequencies swept, with NaN,
 * ±Inf and denormals injected into the input. Asserted: the output stays
 * finite, and it is bit for bit what an engine fed zeros in place of the
 * non-finite samples plays -- so nothing the bad samples reached survives
 * them. Then silence rings every filter down, through normal numbers only,
 * to exactly empty.
 *
 * Ten minutes is the full tier's (fd_soak_rs, in release):
 *   cargo test --release -p fd-core --test soak -- --ignored
 */

use fd_core::{Engine, Field, Param, NODES};

const FS: f64 = 48_000.0;
const BLOCK: usize = 512;

fn patch(e: &mut Engine) {
    for slot in 0..NODES as u8 {
        let n = |f: Field, v: f64, e: &mut Engine| e.set_num(Param::Node(slot, f), v);
        n(Field::Type, f64::from(1 + slot % 9), e);
        n(Field::Family, f64::from(slot % 6), e);
        n(Field::Order, f64::from(slot % 8), e);
        n(Field::Channel, f64::from(slot % 5), e);
        n(Field::Gain, if slot % 2 == 0 { 9.0 } else { -12.0 }, e);
        n(Field::Q, 0.5 + f64::from(slot), e);
    }
    e.set_num(Param::IsoOn, 1.0);
    e.set_num(Param::IsoHigh, 6.0);
}

#[test]
#[ignore = "ten minutes of audio: the full tier runs it, in release"]
fn ten_minutes_of_noise_with_nan_inf_and_denormals() {
    let (mut dirty, mut clean) = (Engine::new(FS), Engine::new(FS));
    patch(&mut dirty);
    patch(&mut clean);
    let blocks = (600.0 * FS) as usize / BLOCK;
    let mut seed = 0x9e37_79b9_7f4a_7c15u64;
    let mut noise = || {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        (seed >> 40) as f32 / (1u64 << 24) as f32 - 0.5
    };
    let bad = [f32::NAN, f32::INFINITY, f32::NEG_INFINITY, -f32::NAN];
    for b in 0..blocks {
        if b % 200 == 0 {
            /* A sweep, as automation would move them. */
            for slot in 0..NODES as u8 {
                let hz = 20.0 * 1000f64.powf(((b / 200 + slot as usize * 7) % 64) as f64 / 63.0);
                dirty.set_num(Param::Node(slot, Field::Freq), hz);
                clean.set_num(Param::Node(slot, Field::Freq), hz);
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
        assert!(l.iter().chain(&r).all(|x| x.is_finite()), "block {b}: a non-finite sample out");
        assert!(l == cl && r == cr, "block {b}: the bad samples left a trace");
    }
    /* Silence: the filters ring down -- 60 dB in the tail, the flush's
     * 1e-30 (600 dB) in ten tails -- through normal numbers only, to exactly
     * nothing. */
    let tail = fd_core::design::tail_seconds(dirty.values(), FS);
    println!("the patch's tail: {tail:.2} s");
    let mut silence = 0.0;
    while silence < 10.0 * tail + 1.0 {
        let (mut l, mut r) = (vec![0.0f32; BLOCK], vec![0.0f32; BLOCK]);
        dirty.process_f32(&mut l, Some(&mut r));
        assert!(l.iter().chain(&r).all(|x| !x.is_subnormal()), "a denormal out at {silence:.1} s of silence");
        silence += BLOCK as f64 / FS;
    }
    assert!(dirty.is_silent(), "{silence:.1} s of silence leave every filter empty");
}
