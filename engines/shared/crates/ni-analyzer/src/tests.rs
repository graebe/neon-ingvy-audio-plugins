// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The rings keep what they are asked to, in order and in their lane, only
//! while watched; the spectrum reads a full-scale sine at 0 dBFS in its band,
//! follows one lane, and falls over its release.

use super::*;

#[test]
fn the_rings_keep_a_lane_in_order_only_while_watched() {
    let mut r = Rings::new();
    let (l, rt) = ([1.0, 2.0, 3.0], [3.0, 2.0, 1.0]);
    r.input(&l, &rt);
    r.output(&l, &rt);
    let (mut pre, mut post) = (vec![9.0; RING], vec![9.0; RING]);
    r.copy_out(&mut pre, &mut post);
    assert!(pre.iter().chain(post.iter()).all(|&x| x == 0.0), "unwatched, nothing is kept");

    r.set_watching(true);
    assert!(r.is_watching() && r.lane() == Lane::Mid);
    r.input(&l, &rt);
    r.output(&[0.5, 0.5, 0.5], &[0.5, 0.5, 0.5]);
    r.copy_out(&mut pre, &mut post);
    assert_eq!(&pre[RING - 3..], &[2.0, 2.0, 2.0], "the mid, newest last");
    assert_eq!(&post[RING - 3..], &[0.5, 0.5, 0.5]);

    r.set_lane(Lane::Side);
    r.copy_out(&mut pre, &mut post);
    assert!(pre.iter().all(|&x| x == 0.0), "a new lane starts from empty rings");
    r.listen(&[1.0, 0.0], Some(&[0.0, 1.0]));
    r.copy_out(&mut pre, &mut post);
    assert_eq!(&pre[RING - 2..], &[0.5, -0.5]);
    assert_eq!(pre, post, "a block left alone is its own output");
    r.set_lane(Lane::Left);
    r.listen(&[0.25], None);
    r.copy_out(&mut pre, &mut post);
    assert_eq!(pre[RING - 1], 0.25, "a mono track is its own right");
    assert_eq!(Lane::Right.of(1.0, 2.0), 2.0);
    assert!(Rings::default().lane() == Lane::Mid);
}

#[cfg(feature = "spectrum")]
mod spectrum {
    use crate::*;

    const N: usize = 64;
    const FS: f64 = 48_000.0;

    fn hz() -> [f64; N] {
        core::array::from_fn(|i| 20.0 * (1000.0f64).powf(i as f64 / (N - 1) as f64))
    }

    fn sine(f: f64, a: f32) -> Vec<f32> {
        (0..RING).map(|i| a * (2.0 * core::f64::consts::PI * f * i as f64 / FS).sin() as f32).collect()
    }

    #[test]
    fn a_full_scale_sine_reads_0_dbfs_in_its_band() {
        let mut a = Analyzer::new(hz());
        assert!(a.ask(Lane::Mid));
        assert!(!a.ask(Lane::Mid), "the same lane again is not new");
        let (x, quiet) = (sine(1000.0, 1.0), vec![0.0; RING]);
        assert!(a.offer(1, true, Lane::Mid, &x, &quiet));
        a.analyse(1, FS);
        let band = hz().iter().enumerate().min_by(|p, q| (p.1 - 1000.0).abs().total_cmp(&(q.1 - 1000.0).abs())).unwrap().0;
        assert!(a.pre()[band].abs() < 1.5, "{} dBFS", a.pre()[band]);
        assert!(a.post().iter().all(|&d| d <= -150.0 + 1e-3), "silence sits on the floor");
        assert!(hz().iter().zip(a.pre()).filter(|(f, _)| **f >= 0.5 * FS).all(|(_, &d)| d == FLOOR_DB));
    }

    #[test]
    fn only_new_frames_of_the_lane_asked_for_are_read() {
        let mut a = Analyzer::new(hz());
        let x = sine(1000.0, 1.0);
        assert!(!a.offer(1, true, Lane::Mid, &x, &x), "nothing asked yet");
        a.ask(Lane::Side);
        assert!(!a.offer(1, true, Lane::Mid, &x, &x), "another lane's frame");
        assert!(!a.offer(1, false, Lane::Side, &x, &x), "a frame nobody watched");
        assert!(a.offer(1, true, Lane::Side, &x, &x));
        a.analyse(1, FS);
        assert!(!a.offer(1, true, Lane::Side, &x, &x), "the same frame twice");
        assert!(a.ask(Lane::Left));
        assert!(a.pre().iter().all(|&d| d == FLOOR_DB), "a new lane starts from the floor");
    }

    #[test]
    fn a_band_rises_at_once_and_falls_over_its_release() {
        let mut a = Analyzer::new(hz());
        a.ask(Lane::Mid);
        let (loud, quiet) = (sine(1000.0, 1.0), vec![0.0; RING]);
        a.offer(1, true, Lane::Mid, &loud, &loud);
        a.analyse(1, FS);
        let top = *a.pre().iter().max_by(|p, q| p.total_cmp(q)).unwrap();
        /* A release later, the band has fallen most of the way to the floor. */
        let later = 1 + (RELEASE_S * FS) as u32;
        a.offer(later, true, Lane::Mid, &quiet, &quiet);
        a.analyse(later, FS);
        let fallen = *a.pre().iter().max_by(|p, q| p.total_cmp(q)).unwrap();
        let expect = top + (FLOOR_DB - top) * (1.0 - (-1.0f32).exp());
        assert!((fallen - expect).abs() < 0.5, "{fallen} after one release from {top}, expected {expect}");
    }
}
