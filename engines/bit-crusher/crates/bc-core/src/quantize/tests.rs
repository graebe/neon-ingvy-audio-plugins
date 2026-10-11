// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

use super::*;
use proptest::prelude::*;

/// Every level a grid can output, found by sweeping full scale finely.
fn levels(g: &Grid) -> Vec<f64> {
    let mut out: Vec<f64> = (-20_000..=20_000).map(|i| g.quantize(f64::from(i) / 20_000.0)).collect();
    out.dedup();
    out
}

#[test]
fn whole_bits_give_two_to_the_bits_steps() {
    for bits in 1..=8 {
        let tread = levels(&Grid::new(f64::from(bits), false));
        let rise = levels(&Grid::new(f64::from(bits), true));
        /* Mid-rise: 2^b levels exactly. Mid-tread has its level at zero and
         * one at each end of the range, so one more. */
        assert_eq!(rise.len(), 1 << bits, "mid-rise, {bits} bits");
        assert_eq!(tread.len(), (1 << bits) + 1, "mid-tread, {bits} bits");
    }
}

#[test]
fn mid_tread_keeps_silence_silent_and_mid_rise_makes_it_half_a_step() {
    let tread = Grid::new(4.0, false);
    let rise = Grid::new(4.0, true);
    assert_eq!(tread.quantize(0.0), 0.0);
    assert_eq!(tread.quantize(1e-6), 0.0);
    assert_eq!(rise.quantize(1e-6), 0.5 * rise.delta());
    assert_eq!(rise.quantize(-1e-6), -0.5 * rise.delta());
    assert_eq!(rise.quantize(0.0), 0.5 * rise.delta());
}

#[test]
fn one_bit_is_a_sign() {
    let rise = Grid::new(1.0, true);
    assert_eq!(levels(&rise), vec![-0.5, 0.5]);
    assert_eq!(rise.quantize(1.0), 0.5);
    let tread = Grid::new(1.0, false);
    assert_eq!(levels(&tread), vec![-1.0, 0.0, 1.0]);
}

#[test]
fn nothing_leaves_full_scale_however_hard_it_is_driven() {
    for bits in [1.0, 1.5, 2.5, 3.0, 3.01, 7.3, 16.0] {
        for mid_rise in [false, true] {
            let g = Grid::new(bits, mid_rise);
            for x in [-100.0, -1.0, 1.0, 100.0] {
                assert!(g.quantize(x).abs() <= 1.0, "{bits} {mid_rise} {x}");
            }
        }
    }
}

#[test]
fn a_fractional_grid_clips_its_top_step_at_full_scale() {
    /* 2.5 bits: Δ = 0.354, so 3Δ = 1.06 is drawn back to 1. */
    let g = Grid::new(2.5, false);
    assert!((g.quantize(0.95) - 1.0).abs() < 1e-12);
    assert!((g.quantize(0.6) - 2.0 * g.delta()).abs() < 1e-12);
}

proptest! {
    #[test]
    fn the_error_is_at_most_half_a_step_inside_full_scale(
        bits in 1.0f64..16.0, x in -0.999f64..0.999, mid_rise in any::<bool>()
    ) {
        let g = Grid::new(bits, mid_rise);
        let y = g.quantize(x);
        /* Below the top level the error is half a step; the top step,
         * clipped or short, can only bring the output closer. */
        prop_assert!((y - x).abs() <= 0.5 * g.delta() + 1e-12, "{} -> {}", x, y);
    }

    #[test]
    fn the_staircase_never_goes_down(bits in 1.0f64..16.0, a in -1.5f64..1.5, b in -1.5f64..1.5, mid_rise in any::<bool>()) {
        let g = Grid::new(bits, mid_rise);
        let (lo, hi) = if a < b { (a, b) } else { (b, a) };
        prop_assert!(g.quantize(lo) <= g.quantize(hi));
    }

    #[test]
    fn more_bits_never_make_a_worse_copy_on_average(bits in 1.0f64..15.0) {
        /* Not sample by sample -- a step edge can move past one input -- but
         * the mean error over a ramp falls as the bits rise. */
        let err = |b: f64| {
            let g = Grid::new(b, false);
            (0..2000).map(|i| {
                let x = f64::from(i) / 1000.0 - 1.0;
                (g.quantize(x) - x).abs()
            }).sum::<f64>()
        };
        prop_assert!(err(bits + 1.0) <= err(bits) + 1e-9);
    }
}

#[test]
fn a_nan_bit_depth_is_one_bit() {
    assert_eq!(Grid::new(f64::NAN, false), Grid::new(1.0, false));
    assert_eq!(Grid::new(0.2, false), Grid::new(1.0, false));
}

proptest! {
    #[test]
    fn the_shape_is_odd_and_expand_undoes_compress(amount in 0.0f64..=1.0, x in -1.0f64..=1.0) {
        let s = Shape::new(amount);
        let c = s.compress(x);
        prop_assert!((s.compress(-x) + c).abs() < 1e-12);
        prop_assert!((s.expand(c) - x).abs() < 1e-9);
        prop_assert!(c.abs() <= 1.0 + 1e-12);
    }
}

#[test]
fn the_shape_is_a_line_at_zero_and_mu_law_at_full() {
    let flat = Shape::new(0.0);
    assert_eq!(flat.compress(0.3), 0.3);
    assert_eq!(flat.expand(0.3), 0.3);
    let mu = Shape::new(1.0);
    let want = (255.0f64 * 0.1).ln_1p() / 256.0f64.ln();
    assert!((mu.compress(0.1) - want).abs() < 1e-12);
    assert!((mu.compress(1.0) - 1.0).abs() < 1e-12);
    assert_eq!(Shape::new(f64::NAN), flat);
}

#[test]
fn the_shape_puts_the_fine_steps_near_silence() {
    let g = Grid::new(4.0, false);
    let s = Shape::new(1.0);
    let crush = |x: f64| s.expand(g.quantize(s.compress(x)));
    /* A quiet signal keeps far more of itself than on the even grid. */
    let quiet = 0.02;
    assert_eq!(g.quantize(quiet), 0.0);
    assert!((crush(quiet) - quiet).abs() < 0.01);
}
