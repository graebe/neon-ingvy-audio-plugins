// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The designs against their closed forms, and the cascades against what
//! they say they do.

use super::*;
use core::f64::consts::{FRAC_1_SQRT_2, PI};
use proptest::prelude::*;

const RATES: [f64; 4] = [44_100.0, 48_000.0, 96_000.0, 192_000.0];
const MINUS_3: f64 = -3.010_299_956_639_812;
const MINUS_6: f64 = -6.020_599_913_279_624;

fn db_at(c: &Cascade, hz: f64, fs: f64) -> f64 {
    c.db(hz, fs)
}

fn warped(hz: f64, fs: f64) -> f64 {
    (PI * hz / fs).tan()
}

/// The frequency a warped value comes from.
fn unwarped(o: f64, fs: f64) -> f64 {
    o.atan() * fs / PI
}

fn proto(f: Family, n: usize) -> Prototype {
    Prototype::new(f, n, 1.0, 60.0)
}

#[test]
fn butterworth_is_its_closed_form() {
    for &fs in &RATES {
        for n in 1..=8 {
            for &fc in &[30.0, 1000.0, 12_000.0] {
                let (lp, _) = family(&proto(Family::Butterworth, n), Band::LowPass, fc, FRAC_1_SQRT_2, fs);
                let (hp, _) = family(&proto(Family::Butterworth, n), Band::HighPass, fc, FRAC_1_SQRT_2, fs);
                for i in 0..400 {
                    let hz = 10.0 * (0.45 * fs / 10.0f64).powf(i as f64 / 399.0);
                    let x = warped(hz, fs) / warped(fc, fs);
                    let want_lp = 1.0 / (1.0 + x.powi(2 * n as i32)).sqrt();
                    let want_hp = 1.0 / (1.0 + x.powi(-2 * n as i32)).sqrt();
                    let w = 2.0 * PI * hz / fs;
                    let (got_lp, got_hp) = (lp.response(w).norm(), hp.response(w).norm());
                    assert!((got_lp - want_lp).abs() <= 1e-9 * want_lp.max(1e-12) + 1e-15, "lp n{n} {hz} {got_lp} {want_lp}");
                    assert!((got_hp - want_hp).abs() <= 1e-9 * want_hp.max(1e-12) + 1e-15, "hp n{n} {hz} {got_hp} {want_hp}");
                }
            }
        }
    }
}

#[test]
fn every_family_is_at_minus_3_db_on_its_frequency() {
    for &fs in &RATES {
        for f in Family::ALL {
            for n in 1..=8 {
                let p = Prototype::new(f, n, 0.5, 60.0);
                for &fc in &[20.0, 1000.0, 10_000.0, 0.45 * fs] {
                    for band in [Band::HighPass, Band::LowPass] {
                        let (c, _) = family(&p, band, fc, FRAC_1_SQRT_2, fs);
                        let want = if f == Family::LinkwitzRiley { MINUS_6 } else { MINUS_3 };
                        let got = db_at(&c, fc, fs);
                        assert!((got - want).abs() < 1e-6, "{f:?} n{n} {band:?} at {fc} Hz, {fs}: {got}");
                    }
                }
            }
        }
    }
}

#[test]
fn the_digital_response_is_the_prototype_on_the_warped_axis() {
    let fs = 48_000.0;
    for f in Family::ALL {
        for n in 1..=8 {
            let p = Prototype::new(f, n, 1.0, 50.0);
            let fc = 3000.0;
            let (lp, _) = family(&p, Band::LowPass, fc, FRAC_1_SQRT_2, fs);
            let (hp, _) = family(&p, Band::HighPass, fc, FRAC_1_SQRT_2, fs);
            for i in 0..200 {
                let hz = 20.0 * 1000.0f64.powf(i as f64 / 199.0);
                let x = warped(hz, fs) / warped(fc, fs);
                let w = 2.0 * PI * hz / fs;
                for (c, want) in [(&lp, p.magnitude(x)), (&hp, p.magnitude(1.0 / x))] {
                    let got = c.response(w).norm();
                    assert!((got - want).abs() <= 1e-9 * want + 1e-14, "{f:?} n{n} {hz}: {got} {want}");
                }
            }
        }
    }
}

#[test]
fn ripple_and_stopband_are_what_they_were_asked_to_be() {
    let fs = 96_000.0;
    let fc = 1000.0;
    for n in 2..=8 {
        let cheb = Prototype::new(Family::ChebyshevI, n, 0.5, 60.0);
        let (c, _) = family(&cheb, Band::LowPass, fc, FRAC_1_SQRT_2, fs);
        let (mut lo, mut hi) = (0.0f64, -100.0f64);
        for i in 0..2000 {
            let hz = 1.0 + 600.0 * i as f64 / 1999.0;
            let d = db_at(&c, hz, fs);
            lo = lo.min(d);
            hi = hi.max(d);
        }
        assert!(hi <= 1e-9 && hi > -1e-3, "cheb I n{n} peak {hi}");
        assert!((lo + 0.5).abs() < 0.01, "cheb I n{n} trough {lo}");

        let inv = Prototype::new(Family::ChebyshevII, n, 1.0, 45.0);
        let (c, _) = family(&inv, Band::LowPass, fc, FRAC_1_SQRT_2, fs);
        let edge = (1..20_000).map(|i| 1000.0 + 2.0 * i as f64).find(|&hz| db_at(&c, hz, fs) <= -45.0 + 1e-6).unwrap();
        let worst = (0..4000).map(|i| db_at(&c, edge + 10.0 * i as f64, fs)).fold(-400.0, f64::max);
        assert!(worst <= -45.0 + 1e-6, "cheb II n{n} stopband {worst}");

        let e = Prototype::new(Family::Elliptic, n, 0.3, 70.0);
        let (c, _) = family(&e, Band::LowPass, fc, FRAC_1_SQRT_2, fs);
        let pass = (0..2000).map(|i| db_at(&c, 1.0 + 500.0 * i as f64 / 1999.0, fs)).fold(0.0, f64::min);
        assert!(pass >= -0.3 - 1e-6, "elliptic n{n} passband {pass}");
        /* A low order reaches 70 dB only far up -- past Nyquist at order 2. */
        if let Some(edge) = (1..8000).map(|i| 1000.0 + 5.0 * i as f64).find(|&hz| db_at(&c, hz, fs) <= -70.0 + 1e-6) {
            let worst = (0..4000).map(|i| db_at(&c, edge + 10.0 * i as f64, fs)).filter(|d| d.is_finite()).fold(-400.0, f64::max);
            assert!(worst <= -70.0 + 1e-6, "elliptic n{n} stopband {worst}");
        }
    }
}

/// The constant table is the reverse Bessel polynomial's n distinct roots.
#[test]
fn bessel_poles_are_the_reverse_bessel_polynomials_roots() {
    for n in 1..=8 {
        let p = proto(Family::Bessel, n);
        let fact = |m: usize| (1..=m).fold(1.0f64, |f, i| f * i as f64);
        let coef: Vec<f64> = (0..=n).map(|k| fact(2 * n - k) / (2f64.powi((n - k) as i32) * fact(k) * fact(n - k))).collect();
        let poly = |s: Complex64| coef.iter().rev().fold(Complex64::new(0.0, 0.0), |y, a| y * s + a);
        let mut roots: Vec<Complex64> = p.pairs().iter().flat_map(|q| [q.pole(), q.pole().conj()]).collect();
        roots.extend(p.real().map(|s| Complex64::new(-s, 0.0)));
        assert_eq!(roots.len(), n);
        for (i, r) in roots.iter().enumerate() {
            /* Relative to the polynomial's largest term there. */
            let size = coef.iter().enumerate().map(|(k, a)| a * r.norm().powi(k as i32)).fold(0.0, f64::max);
            assert!(poly(*r).norm() / size < 1e-12, "n{n} root {r:?}: residual {}", poly(*r).norm() / size);
            assert!(roots[..i].iter().all(|q| (q - r).norm() > 1e-3), "n{n}: {r:?} twice");
        }
    }
}

/// Every prototype, normalised, is at -3 dB at 1 rad/s (Linkwitz-Riley at
/// -6 dB): each family's `w3` in closed form is where it crosses.
#[test]
fn every_prototype_crosses_minus_3_db_at_one() {
    for f in Family::ALL {
        for n in 1..=8 {
            for &ripple in &[0.01, 0.5, 1.0, 3.0] {
                for &stop in &[20.0, 60.0, 120.0] {
                    let p = Prototype::new(f, n, ripple, stop);
                    let want = if f == Family::LinkwitzRiley { MINUS_6 } else { MINUS_3 };
                    let got = 20.0 * p.magnitude(1.0).log10();
                    assert!((got - want).abs() < 1e-9, "{f:?} n{n} ripple {ripple} stop {stop}: {got} dB");
                }
            }
        }
    }
}

#[test]
fn a_band_is_at_minus_3_db_on_both_edges_and_a_notch_is_deep() {
    let fs = 48_000.0;
    for f in [Family::Butterworth, Family::Bessel, Family::ChebyshevI, Family::ChebyshevII, Family::Elliptic] {
        for n in 1..=8 {
            let p = Prototype::new(f, n, 1.0, 60.0);
            for &q in &[0.7f64, 2.0, 8.0] {
                let hz = 2000.0;
                let h = 0.5 / q;
                let r = (1.0 + h * h).sqrt();
                let (f1, f2) = (hz * (r - h), hz * (r + h));
                let (bp, clamped) = family(&p, Band::BandPass, hz, q, fs);
                assert!(!clamped);
                let (bs, _) = family(&p, Band::Notch, hz, q, fs);
                for edge in [f1, f2] {
                    assert!((db_at(&bp, edge, fs) - MINUS_3).abs() < 1e-6, "bp {f:?} n{n} q{q} at {edge}: {}", db_at(&bp, edge, fs));
                    assert!((db_at(&bs, edge, fs) - MINUS_3).abs() < 1e-6, "bs {f:?} n{n} q{q} at {edge}: {}", db_at(&bs, edge, fs));
                }
                let centre = unwarped((warped(f1, fs) * warped(f2, fs)).sqrt(), fs);
                /* A family with zeros of its own and no real pole ends at its
                 * stopband's level at the centre; every other notch is a zero. */
                let floor = if matches!(f, Family::ChebyshevII | Family::Elliptic) && n % 2 == 0 && n >= 2 { -60.0 + 1e-6 } else { -200.0 };
                assert!(db_at(&bs, centre, fs) < floor, "notch {f:?} n{n} q{q}: {}", db_at(&bs, centre, fs));
                let peak = db_at(&bp, centre, fs);
                let want = if matches!(f, Family::ChebyshevI | Family::Elliptic) && n % 2 == 0 { -1.0 } else { 0.0 };
                assert!((peak - want).abs() < 1e-6, "bp {f:?} n{n} centre {peak}");
            }
        }
    }
}

#[test]
fn a_band_near_nyquist_keeps_its_lower_edge_and_says_so() {
    let fs = 44_100.0;
    let (c, clamped) = family(&proto(Family::Butterworth, 2), Band::BandPass, 20_000.0, 0.5, fs);
    assert!(clamped);
    assert!(c.sections().iter().all(|s| s.g() > 0.0 && s.k() > 0.0));
}

#[test]
fn resonance_raises_a_second_order_cut_by_its_q() {
    let fs = 48_000.0;
    for &q in &[0.5, 1.0, 4.0, 12.0] {
        let (c, _) = family(&proto(Family::Butterworth, 2), Band::LowPass, 1000.0, q, fs);
        assert!((db_at(&c, 1000.0, fs) - 20.0 * q.log10()).abs() < 1e-9, "q {q}");
    }
    /* Order 1 has no resonance. */
    let (a, _) = family(&proto(Family::Butterworth, 1), Band::LowPass, 1000.0, 8.0, fs);
    assert!((db_at(&a, 1000.0, fs) - MINUS_3).abs() < 1e-9);
}

#[test]
fn linkwitz_riley_cuts_sum_to_an_all_pass() {
    let fs = 48_000.0;
    for n in [4, 8] {
        let p = proto(Family::LinkwitzRiley, n);
        let (lp, _) = family(&p, Band::LowPass, 800.0, FRAC_1_SQRT_2, fs);
        let (hp, _) = family(&p, Band::HighPass, 800.0, FRAC_1_SQRT_2, fs);
        let ap = all_pass(800.0, FRAC_1_SQRT_2, n / 2, fs);
        for i in 0..300 {
            let w = PI * 0.98 * (i as f64 + 0.5) / 300.0;
            let sum = lp.response(w) + hp.response(w);
            assert!((sum.norm() - 1.0).abs() < 1e-9, "LR{n} sum {}", sum.norm());
            /* The sum IS the Butterworth all-pass of half the order. */
            assert!((sum - ap.response(w)).norm() < 1e-9, "LR{n} vs all-pass");
        }
    }
}

#[test]
fn linkwitz_riley_of_twice_an_odd_order_sums_flat_with_one_cut_inverted() {
    let fs = 48_000.0;
    for n in [2, 6] {
        let p = proto(Family::LinkwitzRiley, n);
        let (lp, _) = family(&p, Band::LowPass, 800.0, FRAC_1_SQRT_2, fs);
        let (hp, _) = family(&p, Band::HighPass, 800.0, FRAC_1_SQRT_2, fs);
        for i in 0..300 {
            let w = PI * 0.98 * (i as f64 + 0.5) / 300.0;
            assert!(((lp.response(w) - hp.response(w)).norm() - 1.0).abs() < 1e-9, "LR{n}");
        }
    }
}

#[test]
fn an_all_pass_is_flat() {
    for n in 1..=8 {
        let c = all_pass(500.0, 3.0, n, 48_000.0);
        for i in 0..200 {
            let w = PI * 0.99 * i as f64 / 199.0;
            assert!((c.response(w).norm() - 1.0).abs() < 1e-12);
        }
    }
}

/// The analog bell, |H|.
fn analog_bell(hz: f64, f0: f64, g_db: f64, q: f64) -> f64 {
    let a = 10f64.powf(g_db / 40.0);
    let w = hz / f0;
    let re = (1.0 - w * w).powi(2);
    10.0 * ((re + (w * a / q).powi(2)) / (re + (w / (a * q)).powi(2))).log10()
}

#[test]
fn a_bell_meets_its_analog_shape_at_dc_its_frequency_and_nyquist() {
    for &fs in &RATES {
        for &(f0, g, q) in &[(100.0, 6.0, 1.0), (1000.0, -12.0, 4.0), (8000.0, 9.0, 0.5), (15_000.0, -6.0, 2.0), (19_000.0, 12.0, 1.0)] {
            let c = bell(f0, g, q, fs);
            assert!(db_at(&c, 1e-6, fs).abs() < 1e-9, "dc");
            assert!((db_at(&c, f0, fs) - g).abs() < 1e-9, "{f0} {g} at its frequency: {}", db_at(&c, f0, fs));
            let nyq = c.response(PI).norm();
            assert!((20.0 * nyq.log10() - analog_bell(0.5 * fs, f0, g, q)).abs() < 1e-6, "nyquist");
            /* Below a quarter of the rate it follows the analog closely. */
            for i in 0..200 {
                let hz = 20.0 * (0.25 * fs / 20.0f64).powf(i as f64 / 199.0);
                let err = (db_at(&c, hz, fs) - analog_bell(hz, f0, g, q)).abs();
                /* Three points matched: within 0.35 dB of the analog shape
                 * for a bell below a quarter of the rate. A broad bell above
                 * it keeps its gain at DC, Freq and Nyquist exactly and its
                 * skirt within 2.5 dB (measured: 2.3 at 19 kHz, Q 1, 44.1
                 * kHz), where the bilinear transform squeezes it whole. */
                let tol = if f0 < 0.25 * fs { 0.35 } else { 2.5 };
                assert!(err < tol, "{f0} {g} {q} {fs} at {hz}: {err}");
            }
        }
    }
    assert!(bell(1000.0, 0.0, 1.0, 48_000.0).is_identity());
}

#[test]
fn shelves_reach_their_gain_and_cross_half_of_it_at_their_frequency() {
    let fs = 48_000.0;
    for order in 1..=4 {
        for &(f0, g) in &[(200.0, 6.0), (2000.0, -9.0), (12_000.0, 4.0)] {
            let lo = low_shelf(f0, g, FRAC_1_SQRT_2, order, fs);
            let hi = high_shelf(f0, g, FRAC_1_SQRT_2, order, fs);
            assert!((db_at(&lo, 1e-6, fs) - g).abs() < 1e-9, "low o{order} dc");
            assert!(db_at(&hi, 1e-6, fs).abs() < 1e-9, "high o{order} dc");
            if order % 2 == 0 {
                assert!((db_at(&lo, f0, fs) - g / 2.0).abs() < 1e-6, "low o{order} {f0}: {}", db_at(&lo, f0, fs));
                assert!((db_at(&hi, f0, fs) - g / 2.0).abs() < 1e-6, "high o{order} {f0}: {}", db_at(&hi, f0, fs));
            }
            if f0 < 5000.0 {
                /* A first-order shelf is still turning there. */
                let tol = if order == 1 { 0.25 } else { 0.05 };
                assert!((db_at(&hi, 0.45 * fs, fs) - g).abs() < tol, "high o{order} {f0} top: {}", db_at(&hi, 0.45 * fs, fs));
            }
        }
    }
}

#[test]
fn a_tilt_turns_about_its_frequency() {
    let fs = 48_000.0;
    let c = tilt(1000.0, 6.0, fs);
    assert!((db_at(&c, 1e-6, fs) + 3.0).abs() < 1e-9);
    assert!(db_at(&c, 1000.0, fs).abs() < 0.05);
    assert!((db_at(&c, 20_000.0, fs) - 3.0).abs() < 0.1);
}

#[test]
fn what_a_cascade_renders_is_what_it_reports() {
    use realfft::RealFftPlanner;
    let fs = 48_000.0;
    let n = 1 << 16;
    let designs = [
        family(&Prototype::new(Family::Elliptic, 6, 0.5, 60.0), Band::LowPass, 3000.0, FRAC_1_SQRT_2, fs).0,
        family(&Prototype::new(Family::ChebyshevII, 5, 1.0, 40.0), Band::HighPass, 400.0, 2.0, fs).0,
        family(&Prototype::new(Family::Bessel, 4, 1.0, 60.0), Band::BandPass, 2000.0, 3.0, fs).0,
        family(&Prototype::new(Family::Butterworth, 3, 1.0, 60.0), Band::Notch, 5000.0, 2.0, fs).0,
        bell(800.0, 7.0, 2.0, fs),
        high_shelf(6000.0, -5.0, 1.0, 3, fs),
        all_pass(1500.0, 2.0, 4, fs),
    ];
    let mut planner = RealFftPlanner::<f64>::new();
    let fft = planner.plan_fft_forward(n);
    for c in designs {
        let mut st = State::default();
        let mut x: Vec<f64> = (0..n).map(|i| c.run(&mut st, if i == 0 { 1.0 } else { 0.0 })).collect();
        let mut spec = fft.make_output_vec();
        fft.process(&mut x, &mut spec).unwrap();
        for (bin, h) in spec.iter().enumerate().step_by(97) {
            let w = 2.0 * PI * bin as f64 / n as f64;
            let want = c.response(w);
            if want.norm() > 1e-5 {
                assert!((h.norm() / want.norm()).ln().abs() < 1e-6, "bin {bin}: {} vs {}", h.norm(), want.norm());
            }
        }
    }
}

#[test]
fn identity_is_bit_exact() {
    let c = Cascade::identity();
    let mut st = State::default();
    for x in [0.1, -0.73, 1e-30, 0.999_999_9] {
        assert_eq!(c.run(&mut st, x), x);
    }
    assert!(c.is_identity());
    assert_eq!(c.response(1.0), Complex64::new(1.0, 0.0));
}

#[test]
fn a_state_flushes_what_has_decayed() {
    let (c, _) = family(&proto(Family::Butterworth, 4), Band::LowPass, 1000.0, FRAC_1_SQRT_2, 48_000.0);
    let mut st = State::default();
    c.run(&mut st, 1e-20);
    assert!(!st.is_empty());
    st.flush(1e-15);
    assert!(st.is_empty());
    c.run(&mut st, 1.0);
    st.reset();
    assert!(st.is_empty());
}

#[test]
fn a_cutoff_swept_every_sample_at_high_q_stays_bounded() {
    let fs = 48_000.0;
    let p = Prototype::new(Family::Elliptic, 8, 1.0, 80.0);
    let mut st = State::default();
    let mut rng = 0x1234_5678u32;
    let mut peak = 0.0f64;
    let len = 480; // 10 ms
    for i in 0..len {
        let hz = 20.0 * 1000.0f64.powf(i as f64 / (len - 1) as f64);
        let (c, _) = family(&p, Band::LowPass, hz, 40.0, fs);
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        let x = (rng as f64 / u32::MAX as f64) * 2.0 - 1.0;
        let y = c.run(&mut st, x);
        assert!(y.is_finite());
        peak = peak.max(y.abs());
    }
    assert!(peak < 1e4, "peak {peak}");
}

fn any_family() -> impl Strategy<Value = Family> {
    (0usize..6).prop_map(Family::from_index)
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(400))]

    #[test]
    fn every_design_is_a_stable_cascade(
        f in any_family(),
        band in 0usize..4,
        n in 1usize..=8,
        hz in 1.0f64..30_000.0,
        q in 0.05f64..60.0,
        ripple in 0.0f64..4.0,
        stop in 10.0f64..130.0,
        fs_i in 0usize..4,
    ) {
        let fs = RATES[fs_i];
        let p = Prototype::new(f, n, ripple, stop);
        let band = [Band::HighPass, Band::LowPass, Band::BandPass, Band::Notch][band];
        let (c, _) = family(&p, band, hz, q, fs);
        let others = [bell(hz, stop - 70.0, q, fs), low_shelf(hz, stop / 4.0 - 15.0, q, n.min(4), fs),
                      high_shelf(hz, 15.0 - stop / 4.0, q, n.min(4), fs), tilt(hz, ripple * 5.0 - 10.0, fs), all_pass(hz, q, n, fs)];
        for c in std::iter::once(&c).chain(others.iter()) {
            for s in c.sections() {
                prop_assert!(s.g().is_finite() && s.g() > 0.0, "g {}", s.g());
                prop_assert!(s.is_first_order() || (s.k().is_finite() && s.k() > 0.0), "k {}", s.k());
                let (a, b, d) = s.mix();
                prop_assert!(a.is_finite() && b.is_finite() && d.is_finite());
            }
            prop_assert!(c.gain().is_finite());
            let mut st = State::default();
            let mut y = 0.0;
            for i in 0..4096 {
                y = c.run(&mut st, if i % 64 < 32 { 1.0 } else { -1.0 });
                prop_assert!(y.is_finite());
            }
            prop_assert!(y.abs() < 1e6);
        }
    }
}



/// A section's decay is its free response's: an impulse through a Q 40
/// resonance at 20 Hz, and through a first-order low-pass at 5 Hz, falls
/// 60 dB below its peak where decay_samples says.
#[test]
fn a_sections_decay_is_its_free_responses() {
    let fs = 48_000.0;
    for sec in [Section::second(warped(20.0, fs), 1.0 / 40.0, 0.0, 1.0, 0.0), Section::first(warped(5.0, fs), 0.0, 1.0)] {
        let n = sec.decay_samples(60.0);
        let mut s = [0.0; 2];
        let y: Vec<f64> = (0..(1.5 * n) as usize).map(|i| sec.run(&mut s, if i == 0 { 1.0 } else { 0.0 })).collect();
        let peak = y.iter().fold(0.0f64, |m, v| m.max(v.abs()));
        let last = y.iter().rposition(|v| v.abs() > peak * 1e-3).unwrap() as f64;
        assert!((last - n).abs() < 0.02 * n, "{sec:?}: {last} samples, said {n}");
    }
    /* A wire does not ring. */
    assert_eq!(Cascade::identity().decay_samples(60.0), 0.0);
}

/// Moving between two designs starts exactly on the one and lands exactly on
/// the other, and only between cascades of one structure.
#[test]
fn a_cascade_moves_between_designs_of_one_structure() {
    let fs = 48_000.0;
    let a = bell(800.0, 9.0, 4.0, fs);
    let b = bell(1200.0, -3.0, 2.0, fs);
    for (x, y) in a.sections().iter().zip(b.sections()) {
        assert_eq!(x.toward(y, 0.0), *x);
        assert_eq!(x.toward(y, 1.0), *y);
    }
    assert!(a.same_structure(&b));
    let cut = family(&proto(Family::Butterworth, 3), Band::LowPass, 1000.0, FRAC_1_SQRT_2, fs).0;
    assert!(!a.same_structure(&cut) && !cut.same_structure(&Cascade::identity()));
    let (mut s1, mut s2) = (State::default(), State::default());
    for i in 0..64 {
        let x = (i as f64 * 0.3).sin();
        assert_eq!(a.run_toward(&b, 1.0, &mut s1, x), b.run(&mut s2, x));
    }
}

/// The analog shelf a design matches: order 2 or 4, as design.rs composes it.
fn analog_shelf_db(low: bool, hz: f64, gain_db: f64, q: f64, order: usize, f: f64) -> f64 {
    let mut m2 = 1.0;
    for k in 0..order / 2 {
        let mut sq = 1.0 / (2.0 * (PI * (2 * k + 1) as f64 / (2 * order) as f64).sin());
        if k == 0 {
            sq = (sq * q / FRAC_1_SQRT_2).min(40.0);
        }
        let a = 10f64.powf(gain_db * 2.0 / order as f64 / 40.0);
        let ra = a.sqrt();
        let w = f / hz;
        let (num, den) = if low { (a - w * w, 1.0 - a * w * w) } else { (1.0 - a * w * w, a - w * w) };
        let mid = (ra * w / sq).powi(2);
        m2 *= a * a * (num * num + mid) / (den * den + mid);
    }
    10.0 * m2.log10()
}

/// A shelf near Nyquist stays near its analog shape: each section is the
/// matched or the bilinear design, whichever strays less (a matched high
/// shelf at 18 kHz, +18 dB, order 4 strayed 13.6 dB at 8.5 kHz at 44.1 kHz).
/// Measured, 20 Hz to 0.49·fs, orders 2 and 4, ±6 to ±18 dB: within 3.2 dB
/// for a shelf up to a quarter of the rate and 1.3 dB below half its
/// frequency; within 7.9 dB for one up to 0.45·fs, where the analog shelf
/// is still turning at Nyquist.
#[test]
fn a_shelf_near_nyquist_stays_near_its_analog_shape() {
    for &fs in &[44_100.0, 48_000.0, 96_000.0] {
        for &hz in &[2000.0, 8000.0, 12_000.0, 14_000.0, 16_000.0, 18_000.0, 20_000.0] {
            for &g in &[-18.0, -12.0, -6.0, 6.0, 12.0, 18.0] {
                for order in [2usize, 4] {
                    for low in [false, true] {
                        let c = if low { low_shelf(hz, g, FRAC_1_SQRT_2, order, fs) } else { high_shelf(hz, g, FRAC_1_SQRT_2, order, fs) };
                        let (mut below, mut all) = (0.0f64, 0.0f64);
                        for i in 0..400 {
                            let f = 20.0 * (0.49 * fs / 20.0f64).powf(i as f64 / 399.0);
                            let e = (c.db(f, fs) - analog_shelf_db(low, hz, g, FRAC_1_SQRT_2, order, f)).abs();
                            all = all.max(e);
                            if f < 0.5 * hz {
                                below = below.max(e);
                            }
                        }
                        let what = format!("{fs} Hz, {} shelf {hz} Hz {g} dB order {order}", if low { "low" } else { "high" });
                        if hz <= 0.25 * fs {
                            assert!(all < 3.2 && below < 1.3, "{what}: {all:.2} dB off, {below:.2} below Freq/2");
                        }
                        assert!(all < 7.9, "{what}: {all:.2} dB off");
                    }
                }
            }
        }
    }
}

/// The analog Butterworth low-pass of `order` 2, 4 or 8 at `cutoff`, its
/// sharpest section's Q raised by `peak` dB: its gain at `f`, in dB.
fn analog_butterworth_db(order: usize, cutoff: f64, peak: f64, f: f64) -> f64 {
    let qs: Vec<f64> = (0..order / 2).map(|k| 1.0 / (2.0 * (PI * (2 * k + 1) as f64 / (2 * order) as f64).sin())).collect();
    let sharpest = qs.iter().cloned().fold(0.0, f64::max);
    let r = f / cutoff;
    qs.iter()
        .map(|&q| {
            let q = if q == sharpest { q * 10f64.powf(peak / 20.0) } else { q };
            -10.0 * ((1.0 - r * r).powi(2) + (r / q).powi(2)).log10()
        })
        .sum()
}

/// THE MATCHED CUT AGAINST ITS ANALOG ONE, as NI Bit-Crusher's tone filter
/// holds it (bc-core's the_filter_keeps_its_slope_and_peak_up_to_twenty_
/// kilohertz and the_filter_is_flat_then_its_slope_and_peak_rises_by_exactly
/// _peak): a Butterworth low-pass of order 2, 4 or 8, its sharpest section
/// raised by Peak, from 20 Hz to 20 kHz -- within 3 dB above -6 dB, 4.5 dB to
/// -20 dB and a quarter of the depth to -60 dB, at 44.1, 48 and 96 kHz, the
/// cutoff up to 20 kHz; at the cutoff exactly -3 dB + Peak.
#[test]
fn a_matched_cut_keeps_its_analog_slope_and_peak_to_twenty_kilohertz() {
    let mut worst: f64 = 0.0;
    for fs in [44_100.0, 48_000.0, 96_000.0] {
        for order in [2usize, 4, 8] {
            let p = proto(Family::Butterworth, order);
            for peak in [0.0, 6.0, 12.0, 24.0] {
                let q = FRAC_1_SQRT_2 * 10f64.powf(peak / 20.0);
                for cutoff in [100.0, 1_000.0, 8_000.0, 14_000.0, 18_000.0, 20_000.0] {
                    let (c, _) = family_matched(&p, Band::LowPass, cutoff, q, fs);
                    for i in 0..400 {
                        let f = 20.0 * 1000f64.powf(f64::from(i) / 399.0);
                        let want = analog_butterworth_db(order, cutoff, peak, f);
                        if want < -60.0 {
                            continue;
                        }
                        let got = c.db(f, fs);
                        let tol = if want > -6.0 { 3.0 } else if want > -20.0 { 4.5 } else { 0.25 * -want };
                        assert!((got - want).abs() < tol, "{fs} Hz, order {order}, peak {peak}, {cutoff} Hz: {got} dB at {f} Hz, analog {want}");
                        if want > -6.0 {
                            worst = worst.max((got - want).abs());
                        }
                    }
                    let at = c.db(cutoff, fs);
                    assert!((at - (peak + MINUS_3)).abs() < 0.01, "{fs} {order} {peak} {cutoff}: {at} dB at the cutoff");
                    if cutoff <= 1_000.0 {
                        assert!(c.db(cutoff / 10.0, fs).abs() < 0.2, "flat below");
                    }
                }
            }
        }
    }
    println!("matched cut: within {worst:.2} dB of the analog above -6 dB");
}

/// Every family at every order, a high-pass or a low-pass, matched: at its
/// cutoff exactly the bilinear design's level there -- the family's -3 dB
/// (-6 dB for Linkwitz-Riley) raised by the resonance -- the same at DC and
/// far into the passband, and stable, high up in the band too.
#[test]
fn every_matched_cut_meets_its_cutoff_and_passband() {
    for fs in [44_100.0, 48_000.0, 96_000.0] {
        for f in Family::ALL {
            for n in 1..=8 {
                let p = Prototype::new(f, n, 1.0, 60.0);
                for band in [Band::LowPass, Band::HighPass] {
                    for &(fc, q) in &[(60.0, FRAC_1_SQRT_2), (1000.0, 3.0), (12_000.0, 1.0), (0.45 * fs, 2.0)] {
                        let (m, _) = family_matched(&p, band, fc, q, fs);
                        let (b, _) = family(&p, band, fc, q, fs);
                        let what = format!("{fs} {f:?} n{n} {band:?} {fc} Hz Q {q}");
                        assert!((m.db(fc, fs) - b.db(fc, fs)).abs() < 1e-6, "{what}: {} at the cutoff, bilinear {}", m.db(fc, fs), b.db(fc, fs));
                        let pass = if band == Band::LowPass { 1e-6 } else { 0.4999 * fs };
                        let far = if band == Band::LowPass { fc / 50.0 } else { (fc * 50.0).min(0.4999 * fs) };
                        assert!((m.db(pass, fs) - b.db(pass, fs)).abs() < 0.3 || band == Band::HighPass, "{what}: DC");
                        if band == Band::LowPass || fc * 50.0 < 0.3 * fs {
                            assert!((m.db(far, fs) - b.db(far, fs)).abs() < 0.3, "{what}: the passband, {} vs {}", m.db(far, fs), b.db(far, fs));
                        }
                        assert!(m.decay_samples(60.0).is_finite(), "{what}: unstable");
                    }
                }
            }
        }
    }
}

/// A matched Butterworth high-pass follows its analog one: within 4 dB above
/// -6 dB and 4.5 dB to -20 dB, at 44.1, 48 and 96 kHz, cutoffs up to 20 kHz.
/// Measured: within 2 dB but for a resonant cutoff at 20 kHz (3.5 dB above
/// -6 dB and 4.45 dB at -20 dB at 44.1 kHz, order 2, Q for +12 dB) -- a
/// second-order high-pass whose corner is 0.45·fs cannot hold its peak's
/// skirt; the bilinear one there is 25 dB off.
#[test]
fn a_matched_high_pass_keeps_its_analog_shape() {
    for fs in [44_100.0, 48_000.0, 96_000.0] {
        for order in [2usize, 4, 8] {
            let p = proto(Family::Butterworth, order);
            for peak in [0.0, 12.0] {
                let q = FRAC_1_SQRT_2 * 10f64.powf(peak / 20.0);
                for cutoff in [100.0, 1_000.0, 8_000.0, 14_000.0, 20_000.0] {
                    let (c, _) = family_matched(&p, Band::HighPass, cutoff, q, fs);
                    for i in 0..400 {
                        let f = 20.0 * 1000f64.powf(f64::from(i) / 399.0);
                        /* A high-pass is its low-pass at cutoff²/f. */
                        let want = analog_butterworth_db(order, cutoff, peak, cutoff * cutoff / f);
                        if want < -20.0 {
                            continue;
                        }
                        let got = c.db(f, fs);
                        let tol = if want > -6.0 { 4.0 } else { 4.5 };
                        assert!((got - want).abs() < tol, "{fs} Hz, order {order}, peak {peak}, {cutoff} Hz: {got} dB at {f} Hz, analog {want}");
                    }
                }
            }
        }
    }
}

/// A matched cut retuned while it runs does not click: an order-8 low-pass
/// with 24 dB of peak, its cutoff swept 200 Hz to 18 kHz and back, the
/// sections redesigned every 16 samples under a 220 Hz tone at 0.5.
#[test]
fn a_matched_cut_retunes_without_a_click() {
    let fs = 48_000.0;
    let p = proto(Family::Butterworth, 8);
    let q = FRAC_1_SQRT_2 * 10f64.powf(24.0 / 20.0);
    let mut st = State::default();
    let (mut last, mut worst) = (0.0f64, 0.0f64);
    let mut c = family_matched(&p, Band::LowPass, 200.0, q, fs).0;
    let n = 96_000;
    for i in 0..n {
        if i % 16 == 0 {
            let t = i as f64 / n as f64;
            let hz = 200.0 * 90f64.powf(1.0 - (2.0 * t - 1.0).abs());
            c = family_matched(&p, Band::LowPass, hz, q, fs).0;
        }
        let x = 0.5 * (2.0 * PI * 220.0 * i as f64 / fs).sin();
        let y = c.run(&mut st, x);
        if i > 4_800 {
            worst = worst.max((y - last).abs() / 10f64.powf(24.0 / 20.0));
        }
        last = y;
    }
    /* The tone moves at most 0.0145 a sample, 16 times that at the peak. */
    println!("a matched cut swept: its largest step, over the peak's gain, {worst:.4}");
    assert!(worst < 0.03, "{worst}");
}

/* --------------------------------------------- continuity along a sweep -- */

/// |H| on a log grid from 20 Hz to the top, in dB, held up to -40 dB:
/// beside a stopband zero any move of the zero is a large change in dB, and
/// not what a jump sounds like.
fn curve_db(c: &Cascade, fs: f64) -> Vec<f64> {
    (0..48).map(|i| c.db(20.0 * ((0.49 * fs) / 20.0).powf(i as f64 / 47.0), fs).max(-40.0)).collect()
}

fn largest_change(a: &[f64], b: &[f64]) -> f64 {
    a.iter().zip(b).map(|(x, y)| (x - y).abs()).fold(0.0, f64::max)
}

/// A step of the curve from `f0` to `f1`, halved sixteen times toward the
/// half that changes most: what is left of it at 1/15 000 of an octave, and
/// whether the design switched realisation there. A curve that only moves
/// fast -- the bilinear transform's near Nyquist -- leaves nothing; a jump
/// is all still there.
fn jump_within(make: &dyn Fn(f64) -> Cascade, mut f0: f64, mut f1: f64, fs: f64) -> (f64, bool) {
    let (mut c0, mut c1) = (make(f0), make(f1));
    for _ in 0..16 {
        let mid = (f0 * f1).sqrt();
        let cm = make(mid);
        let (lo, hi) = (curve_db(&c0, fs), curve_db(&c1, fs));
        let m = curve_db(&cm, fs);
        if largest_change(&lo, &m) >= largest_change(&m, &hi) {
            (f1, c1) = (mid, cm);
        } else {
            (f0, c0) = (mid, cm);
        }
    }
    (largest_change(&curve_db(&c0, fs), &curve_db(&c1, fs)), !c0.same_realisation(&c1))
}

/// Along a sweep of `make` from `from` to `to` in steps of 1/240 octave --
/// what a glide hears tick to tick -- the largest jump of the curve (a step
/// that halving does not shrink, jump_within) where every section stays as
/// it was realised, and the largest where one switches.
fn largest_jumps(make: &dyn Fn(f64) -> Cascade, from: f64, to: f64, fs: f64) -> (f64, f64) {
    let (mut unreported, mut switching) = (0.0f64, 0.0f64);
    let mut f = from;
    let mut last = curve_db(&make(f), fs);
    while f < to {
        let next = f * 2f64.powf(1.0 / 240.0);
        let now = curve_db(&make(next), fs);
        if largest_change(&last, &now) > 0.25 {
            let (jump, switched) = jump_within(make, f, next, fs);
            if switched {
                switching = switching.max(jump);
            } else {
                unreported = unreported.max(jump);
            }
        }
        (f, last) = (next, now);
    }
    (unreported, switching)
}

/*
 * A CURVE JUMPS ONLY WHERE ITS REALISATION SWITCHES. The matched designs
 * choose, from an eighth of the rate up, between candidates for each analog
 * section (matched with either sign at Nyquist, or bilinear): the best at
 * each setting, so where two tie the curve steps from one to the other --
 * 16 dB in a matched elliptic low-pass's transition at 44.1 kHz. Every
 * such step is a switch the design reports (Section::form), which the
 * engine then moves over a fade rather than a sub-block. Anywhere else the
 * curve is continuous: a step that is large (the bilinear transform moves
 * fast near Nyquist) shrinks to nothing when it is halved.
 */
#[test]
fn a_matched_design_moves_smoothly_except_where_it_switches_and_says_so() {
    let fs = 44_100.0;
    let top = 0.49 * fs;
    let ell = Prototype::new(Family::Elliptic, 8, 0.5, 60.0);
    let cheb2 = Prototype::new(Family::ChebyshevII, 6, 1.0, 50.0);
    let bw = proto(Family::Butterworth, 4);
    type Design = Box<dyn Fn(f64) -> Cascade>;
    let cases: [(&str, Design); 7] = [
        ("high shelf +12 dB order 4", Box::new(move |hz| high_shelf(hz, 12.0, FRAC_1_SQRT_2, 4, fs))),
        ("low shelf -12 dB order 3", Box::new(move |hz| low_shelf(hz, -12.0, 2.0, 3, fs))),
        ("bell +12 dB Q 4", Box::new(move |hz| bell(hz, 12.0, 4.0, fs))),
        ("matched low-pass Butterworth 4, Q 2", Box::new(move |hz| family_matched(&bw, Band::LowPass, hz, 2.0, fs).0)),
        ("matched high-pass Butterworth 4, Q 2", Box::new(move |hz| family_matched(&bw, Band::HighPass, hz, 2.0, fs).0)),
        ("matched low-pass elliptic 8", Box::new(move |hz| family_matched(&ell, Band::LowPass, hz, FRAC_1_SQRT_2, fs).0)),
        ("matched high-pass Chebyshev II 6", Box::new(move |hz| family_matched(&cheb2, Band::HighPass, hz, FRAC_1_SQRT_2, fs).0)),
    ];
    let mut fails = Vec::new();
    for (name, make) in &cases {
        let (unreported, switching) = largest_jumps(make.as_ref(), 4000.0, top, fs);
        println!("{name}: largest jump {unreported:.4} dB with no switch, {switching:.3} dB where it switches");
        if unreported > 0.05 {
            fails.push(format!("{name}: a {unreported:.2} dB jump with no switch"));
        }
    }
    assert!(fails.is_empty(), "{fails:?}");
}

