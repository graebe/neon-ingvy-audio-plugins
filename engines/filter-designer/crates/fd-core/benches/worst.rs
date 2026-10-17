// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! What the audio thread pays at its worst: a prototype of each family at
//! order 8, a 64-sample block right after a preset turns all sixteen nodes
//! into order-8 elliptic low-pass filters at once, and the glides that
//! design every tick (Ripple, high shelves, matched elliptic cuts).
//!
//! The budget is a tenth of the block's real time: 133 µs for 64 samples at
//! 48 kHz.

use std::hint::black_box;

use fd_core::{Engine, Field, Param, NODES};
use ni_filter::{Family, Prototype};

fn main() {
    divan::main();
}

#[divan::bench(args = Family::ALL)]
fn prototype_at_order_8(family: Family) -> Prototype {
    Prototype::new(black_box(family), 8, black_box(0.5), black_box(60.0))
}

/// The first 64-sample block after the preset: its control ticks design the
/// prototypes, the fades start.
#[divan::bench]
fn block_after_a_16_node_preset(bencher: divan::Bencher) {
    bencher
        .with_inputs(|| {
            let mut e = Engine::new(48_000.0);
            for slot in 0..NODES as u8 {
                e.set_num(Param::Node(slot, Field::Type), 5.0);
                e.set_num(Param::Node(slot, Field::Family), 4.0);
                e.set_num(Param::Node(slot, Field::Order), 7.0);
                e.set_num(Param::Node(slot, Field::Freq), 500.0 + 500.0 * slot as f64);
            }
            (e, vec![0.1f32; 64], vec![-0.1f32; 64])
        })
        .bench_local_values(|(mut e, mut l, mut r)| {
            e.process_f32(&mut l, Some(&mut r));
            (e, l, r)
        });
}

/// A block while Ripple glides on all sixteen: the steady cost of automating
/// it.
#[divan::bench]
fn block_while_ripple_glides_on_16_nodes(bencher: divan::Bencher) {
    let mut e = Engine::new(48_000.0);
    for slot in 0..NODES as u8 {
        e.set_num(Param::Node(slot, Field::Type), 5.0);
        e.set_num(Param::Node(slot, Field::Family), 4.0);
        e.set_num(Param::Node(slot, Field::Order), 7.0);
        e.set_num(Param::Node(slot, Field::Freq), 500.0 + 500.0 * slot as f64);
    }
    let (mut l, mut r) = (vec![0.1f32; 64], vec![-0.1f32; 64]);
    for _ in 0..100 {
        e.process_f32(&mut l, Some(&mut r));
    }
    let mut up = false;
    bencher.bench_local(|| {
        up = !up;
        for slot in 0..NODES as u8 {
            e.set_num(Param::Node(slot, Field::Ripple), if up { 2.5 } else { 0.1 });
        }
        e.process_f32(&mut l, Some(&mut r));
    });
}

/// A block while Freq glides on sixteen order-4 high shelves high in the
/// band, where every design weighs the matched against the bilinear one.
#[divan::bench]
fn block_while_16_high_shelves_glide(bencher: divan::Bencher) {
    let mut e = Engine::new(48_000.0);
    for slot in 0..NODES as u8 {
        e.set_num(Param::Node(slot, Field::Type), 3.0);
        e.set_num(Param::Node(slot, Field::Order), 3.0);
        e.set_num(Param::Node(slot, Field::Gain), 12.0);
    }
    let (mut l, mut r) = (vec![0.1f32; 64], vec![-0.1f32; 64]);
    let mut up = false;
    bencher.bench_local(|| {
        up = !up;
        for slot in 0..NODES as u8 {
            e.set_num(Param::Node(slot, Field::Freq), if up { 16_000.0 } else { 9_000.0 });
        }
        e.process_f32(&mut l, Some(&mut r));
    });
}

/// A block while Freq glides on sixteen matched order-8 elliptic low-pass
/// cuts high in the band, where every section weighs its realisations (the
/// three-point matches and the bilinear one) and a switch between them
/// morphs: the costliest glide there is.
#[divan::bench]
fn block_while_16_matched_elliptic_cuts_glide(bencher: divan::Bencher) {
    let mut e = Engine::new(48_000.0);
    e.set_num(Param::MatchedCuts, 1.0);
    for slot in 0..NODES as u8 {
        e.set_num(Param::Node(slot, Field::Type), 5.0);
        e.set_num(Param::Node(slot, Field::Family), 4.0);
        e.set_num(Param::Node(slot, Field::Order), 7.0);
        e.set_num(Param::Node(slot, Field::Freq), 9_000.0);
    }
    let (mut l, mut r) = (vec![0.1f32; 64], vec![-0.1f32; 64]);
    for _ in 0..100 {
        e.process_f32(&mut l, Some(&mut r));
    }
    let mut up = false;
    bencher.bench_local(|| {
        up = !up;
        for slot in 0..NODES as u8 {
            e.set_num(Param::Node(slot, Field::Freq), if up { 18_000.0 } else { 9_000.0 });
        }
        e.process_f32(&mut l, Some(&mut r));
    });
}
