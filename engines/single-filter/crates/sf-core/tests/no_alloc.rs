// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The audio paths allocate nothing: every product's block, its listening,
//! its Move block, a parameter by key, the panic.

use assert_no_alloc::{assert_no_alloc, AllocDisabler};
use sf_core::{Engine, Field, Lane, SHAPES};

#[global_allocator]
static A: AllocDisabler = AllocDisabler;

#[test]
fn every_audio_path_allocates_nothing() {
    let mut engines: Vec<Engine> = SHAPES.iter().map(|&s| Engine::new(s, 48_000.0).unwrap()).collect();
    let (mut l, mut r) = (vec![0.25f32; 512], vec![-0.25f32; 512]);
    let mut i16s = vec![1000i16; 1024];
    let mut text = [0u8; 32];
    assert_no_alloc(|| {
        for e in &mut engines {
            e.set_watching(true);
            for k in 0..40 {
                e.set(Field::Freq, 50.0 * (k + 1) as f64);
                e.set(Field::Order, (k % 8) as f64);
                e.set(Field::Family, (k % 6) as f64);
                e.set(Field::Channel, (k % 5) as f64);
                e.set(Field::Matched, (k % 2) as f64);
                e.set_lane([Lane::Left, Lane::Right, Lane::Mid, Lane::Side][k % 4]);
                e.process_f32(&mut l, Some(&mut r));
                e.process_f32(&mut l, None);
                e.listen_f32(&l, Some(&r));
                e.process_i16(&mut i16s, 512);
                e.set_param("freq", "1.5k");
                e.get_param("freq", &mut text);
            }
            e.on_midi(&[0xB0, 120, 0]);
            e.set_param("panic", "");
        }
    });
    assert_eq!(assert_no_alloc::violation_count(), 0);
}
