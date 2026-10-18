// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Keeping the rings allocates nothing: it happens on an audio callback.

use assert_no_alloc::{assert_no_alloc, AllocDisabler};
use ni_analyzer::{Lane, Rings, RING};

#[global_allocator]
static A: AllocDisabler = AllocDisabler;

#[test]
fn keeping_the_rings_allocates_nothing() {
    let mut r = Rings::new();
    let (l, rt) = ([0.5; 64], [-0.5; 64]);
    let (lf, rf) = ([0.5f32; 64], [-0.5f32; 64]);
    let (mut pre, mut post) = (vec![0.0f32; RING], vec![0.0f32; RING]);
    assert_no_alloc(|| {
        for k in 0..400 {
            r.set_watching(k % 7 != 0);
            r.set_lane([Lane::Left, Lane::Right, Lane::Mid, Lane::Side][k / 100]);
            r.input(&l, &rt);
            r.output(&l, &rt);
            r.listen(&lf, if k % 2 == 0 { Some(&rf) } else { None });
            r.copy_out(&mut pre, &mut post);
        }
    });
    assert!(pre.iter().any(|&x| x != 0.0));
    assert_eq!(assert_no_alloc::violation_count(), 0);
}
