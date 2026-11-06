// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Sensory dissonance against what Plomp and Levelt measured and Sethares
//! drew: where two sines are roughest, where a timbre's intervals are
//! smoothest, and that the familiar triads sit in wells.

use music_core::Timbre;
use music_core::dissonance::{MAX_SIDE, Partial, pair, roughest, valleys};

const C4: f64 = 261.63;

/// The cents of a frequency ratio.
fn cents(ratio: f64) -> f64 {
    1200.0 * ratio.log2()
}

/// The valleys of `timbre` from just below the unison to `to` cents, at
/// 1-cent steps.
fn valley_cents(timbre: &Timbre, f0: f64, to: f64) -> Vec<f64> {
    let mut curve = vec![0.0; (to as usize) + 11];
    timbre.curve(f0, -10.0, 1.0, &mut curve);
    let mut out = [0; 32];
    let n = valleys(&curve, 0.02, &mut out);
    out[..n].iter().map(|&i| i as f64 - 10.0).collect()
}

fn has_valley_near(found: &[f64], at: f64, within: f64) -> bool {
    found.iter().any(|&c| (c - at).abs() <= within)
}

#[test]
fn two_sines_are_smooth_together_rough_close_and_smooth_again_far_apart() {
    assert_eq!(pair(500.0, 1.0, 500.0, 1.0), 0.0);
    let peak = roughest(500.0);
    let at = |df: f64| pair(500.0, 1.0, 500.0 + df, 1.0);
    // The analytic peak is the curve's maximum.
    for df in [peak * 0.5, peak * 0.9, peak * 1.1, peak * 2.0] {
        assert!(
            at(peak) > at(df),
            "{df} Hz rougher than the peak at {peak} Hz"
        );
    }
    // Far beyond the critical band, almost nothing.
    assert!(at(500.0) < at(peak) * 0.01);
    // The order and a common loudness: the quieter sine sets the pair's level.
    assert_eq!(pair(530.0, 0.5, 500.0, 1.0), pair(500.0, 1.0, 530.0, 0.5));
    assert!((pair(500.0, 0.5, 530.0, 1.0) - 0.5 * pair(500.0, 1.0, 530.0, 1.0)).abs() < 1e-12);
}

#[test]
fn the_roughest_distance_grows_with_the_register() {
    assert!(roughest(100.0) < roughest(500.0));
    assert!(roughest(500.0) < roughest(2000.0));
}

#[test]
fn harmonic_tones_are_smoothest_at_the_just_intervals() {
    let found = valley_cents(&Timbre::harmonic(6), C4, 1210.0);
    for (name, ratio) in [
        ("unison", 1.0),
        ("minor third", 6.0 / 5.0),
        ("major third", 5.0 / 4.0),
        ("fourth", 4.0 / 3.0),
        ("fifth", 3.0 / 2.0),
        ("major sixth", 5.0 / 3.0),
        ("octave", 2.0),
    ] {
        assert!(
            has_valley_near(&found, cents(ratio), 5.0),
            "no valley at the {name} ({:.0} cents); valleys at {found:?}",
            cents(ratio)
        );
    }
}

/// Sethares 1993, table I: seven harmonics have their minima at these nine.
/// Weighting a pair by its quieter partial (the book's rule) adds a shallow
/// 7/6 as well, which the book's own table for nine harmonics lists.
#[test]
fn seven_harmonics_add_the_septimal_valleys() {
    let found = valley_cents(&Timbre::harmonic(7), C4, 1210.0);
    for ratio in [1.0, 1.2, 1.25, 4.0 / 3.0, 1.4, 1.5, 5.0 / 3.0, 1.75, 2.0] {
        assert!(
            has_valley_near(&found, cents(ratio), 5.0),
            "no valley at {ratio}; valleys at {found:?}"
        );
    }
}

#[test]
fn a_stretched_timbre_moves_the_smooth_octave_with_it() {
    let found = valley_cents(&Timbre::stretched(6, 2.1), C4, 1350.0);
    assert!(
        has_valley_near(&found, cents(2.1), 5.0),
        "valleys at {found:?}"
    );
    assert!(
        !has_valley_near(&found, 1200.0, 15.0),
        "a valley left at the 2:1 octave: {found:?}"
    );
}

#[test]
fn a_sine_has_no_valleys_but_the_unison() {
    let found = valley_cents(&Timbre::sine(), C4, 1250.0);
    assert_eq!(found, [0.0]);
}

#[test]
fn the_fifth_is_smoother_than_the_tritone_and_the_semitone_roughest() {
    let t = Timbre::harmonic(6);
    let d = |c: f64| t.dissonance(&[C4, C4 * (c / 1200.0).exp2()]);
    assert!(d(700.0) < d(600.0));
    assert!(d(100.0) > d(300.0));
    assert!(d(100.0) > d(600.0));
}

#[test]
fn the_same_interval_is_rougher_low_down() {
    let t = Timbre::harmonic(6);
    let minor_third =
        |f: f64| t.dissonance(&[f, f * 1.2]) - t.dissonance(&[f]) - t.dissonance(&[f * 1.2]);
    assert!(
        minor_third(65.41) > minor_third(523.25),
        "C2 not rougher than C5"
    );
}

#[test]
fn the_order_of_the_notes_does_not_matter() {
    let t = Timbre::harmonic(6);
    let a = t.dissonance(&[C4, C4 * 1.25, C4 * 1.5]);
    let b = t.dissonance(&[C4 * 1.5, C4, C4 * 1.25]);
    assert!((a - b).abs() < 1e-9);
}

#[test]
fn a_curve_and_a_landscape_agree_with_the_notes_they_stand_for() {
    let t = Timbre::harmonic(6);
    let mut curve = [0.0; 13];
    t.curve(C4, 0.0, 100.0, &mut curve);
    for (i, &d) in curve.iter().enumerate() {
        let g = C4 * (i as f64 / 12.0).exp2();
        assert!((d - t.dissonance(&[C4, g])).abs() < 1e-9);
    }
    let n = 13;
    let mut land = vec![0.0; n * n];
    assert!(t.landscape(C4, 100.0, n, &mut land));
    for (x, y) in [(4, 3), (3, 4), (0, 0), (12, 12), (5, 2)] {
        let mid = C4 * (x as f64 / 12.0).exp2();
        let top = mid * (y as f64 / 12.0).exp2();
        assert!((land[y * n + x] - t.dissonance(&[C4, mid, top])).abs() < 1e-9);
    }
}

#[test]
fn a_landscape_refuses_what_it_cannot_hold() {
    let t = Timbre::sine();
    let mut small = [0.0; 3];
    assert!(!t.landscape(C4, 10.0, 2, &mut small));
    assert!(!t.landscape(C4, 10.0, 0, &mut small));
    let mut big = vec![0.0; (MAX_SIDE + 1) * (MAX_SIDE + 1)];
    assert!(!t.landscape(C4, 10.0, MAX_SIDE + 1, &mut big));
}

/// Whether the landscape at (x, y) is lower than all eight around it.
fn is_well(land: &[f64], n: usize, x: usize, y: usize) -> bool {
    let at = |x: usize, y: usize| land[y * n + x];
    (x - 1..=x + 1).all(|i| (y - 1..=y + 1).all(|j| (i, j) == (x, y) || at(i, j) > at(x, y)))
}

#[test]
fn the_major_and_minor_triads_lie_in_wells() {
    // 10-cent steps; the just triads' intervals rounded to them.
    let t = Timbre::harmonic(6);
    let n = 121;
    let mut land = vec![0.0; n * n];
    assert!(t.landscape(C4, 10.0, n, &mut land));
    let step = |ratio: f64| (cents(ratio) / 10.0).round() as usize;
    let major = (step(5.0 / 4.0), step(6.0 / 5.0));
    let minor = (step(6.0 / 5.0), step(5.0 / 4.0));
    // A cell either side, for the rounding to 10 cents.
    let near_well = |(x, y): (usize, usize)| {
        (x - 1..=x + 1).any(|i| (y - 1..=y + 1).any(|j| is_well(&land, n, i, j)))
    };
    assert!(near_well(major), "no well at the major triad");
    assert!(near_well(minor), "no well at the minor triad");
}

#[test]
fn a_timbre_takes_only_what_it_can_hold() {
    assert!(Timbre::new(&[]).is_none());
    assert!(
        Timbre::new(&[Partial {
            ratio: 0.0,
            amp: 1.0
        }])
        .is_none()
    );
    assert!(
        Timbre::new(&[Partial {
            ratio: 1.0,
            amp: -1.0
        }])
        .is_none()
    );
    assert!(
        Timbre::new(
            &[Partial {
                ratio: 1.0,
                amp: 1.0
            }; 17]
        )
        .is_none()
    );
    let t = Timbre::new(&[
        Partial {
            ratio: 1.0,
            amp: 1.0,
        },
        Partial {
            ratio: 2.7,
            amp: 0.4,
        },
    ])
    .unwrap();
    assert_eq!(t.partials().len(), 2);
    assert_eq!(Timbre::harmonic(40).partials().len(), 16);
    assert_eq!(Timbre::harmonic(0).partials().len(), 1);
    assert_eq!(Timbre::bar(12).partials().len(), 6);
    assert_eq!(Timbre::membrane(12).partials().len(), 10);
    assert_eq!(Timbre::bell(12).partials()[0].ratio, 0.5);
    assert_eq!(Timbre::bell(12).partials().len(), 9);
    assert_eq!(Timbre::saron(12).partials().len(), 4);
    assert_eq!(
        Timbre::odd(3)
            .partials()
            .iter()
            .map(|p| p.ratio)
            .collect::<Vec<_>>(),
        [1.0, 3.0, 5.0]
    );
    assert_eq!(Timbre::sine().partials(), Timbre::harmonic(1).partials());
}
