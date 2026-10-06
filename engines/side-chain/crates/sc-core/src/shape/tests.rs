// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The curve algebra and the stage machine's bookkeeping: properties, checkable
//! without a buffer of audio. See `crate::tests` for what is pinned elsewhere.

use crate::shape::{shape, shape_inv, Curve, Env, Stage, Stages};
use crate::tests::CURVES;

/* ------------------------------------------------------------------ shapes */

#[test]
fn shape_endpoints_and_bounds() {
    for c in CURVES {
        assert_eq!(shape(c, 0.0), 0.0, "{c:?} at 0");
        assert_eq!(shape(c, 1.0), 1.0, "{c:?} at 1");
        /* Out of range and NaN are clamped, not propagated: a NaN gain silences
         * a track permanently and cannot be recovered from. */
        assert_eq!(shape(c, -1.0), 0.0);
        assert_eq!(shape(c, 2.0), 1.0);
        assert_eq!(shape(c, f64::NAN), 0.0);
        for k in 0..=100 {
            let v = shape(c, k as f64 / 100.0);
            assert!((0.0..=1.0).contains(&v), "{c:?} out of range: {v}");
        }
    }
}

#[test]
fn shape_is_monotonic() {
    for c in CURVES {
        let mut prev = -1.0;
        for k in 0..=1000 {
            let v = shape(c, k as f64 / 1000.0);
            assert!(v >= prev, "{c:?} dipped at {k}: {v} < {prev}");
            prev = v;
        }
    }
}

#[test]
fn shape_inv_round_trips() {
    for c in CURVES {
        for k in 1..1000 {
            let t = k as f64 / 1000.0;
            let back = shape_inv(c, shape(c, t));
            assert!((back - t).abs() < 1e-9, "{c:?} t={t} -> {back}");
        }
    }
}

#[test]
fn exponential_is_the_documented_bend() {
    /* Halfway through an exponential stage the envelope is ~82% of the way --
     * the constant the header claims, checked so a change to CURVE_K cannot
     * pass quietly. */
    let v = shape(Curve::Exp, 0.5);
    assert!((v - 0.8176).abs() < 1e-3, "exp at 0.5 = {v}");
}

#[test]
fn scurve_is_flat_at_both_ends_and_antisymmetric() {
    /* The bug this pins: `0.5 * curve_exp(2t)` on both halves leaves the floor
     * vertically, which is the opposite of an S-curve. Compare the slope near
     * the ends against the slope at the middle. */
    let d = 1e-3;
    let ends = shape(Curve::SCurve, d) / d;
    let middle = (shape(Curve::SCurve, 0.5 + d) - shape(Curve::SCurve, 0.5 - d)) / (2.0 * d);
    assert!(ends < 0.5, "S-curve leaves the floor too fast: {ends}");
    assert!(middle > 1.5, "S-curve middle too slow: {middle}");
    assert!(middle > ends * 4.0, "not an S: ends={ends} middle={middle}");

    for k in 0..=500 {
        let t = k as f64 / 1000.0;
        let a = shape(Curve::SCurve, t);
        let b = shape(Curve::SCurve, 1.0 - t);
        assert!((a + b - 1.0).abs() < 1e-12, "not antisymmetric at {t}");
    }
}

/* ----------------------------------------------------------- stage machine */

fn stages(d: f64, a: f64, h: f64, r: f64) -> Stages {
    Stages {
        delay: d,
        attack: a,
        hold: h,
        release: r,
    }
}

/// Run the envelope to completion, returning every emitted value.
fn run(env: &mut Env, s: &Stages, curve: Curve, n: usize, gated: bool) -> Vec<f64> {
    (0..n).map(|_| env.next(curve, s, gated)).collect()
}

#[test]
fn envelope_lasts_exactly_the_sum_of_its_stages() {
    /* The off-by-one this pins: testing a stage's completion AFTER emitting
     * gave len+1 samples per stage, so a nominal 30 ran for 33. */
    for (d, a, h, r) in [
        (0.0, 10.0, 10.0, 10.0),
        (5.0, 20.0, 1.0, 40.0),
        (0.0, 1.0, 0.0, 1.0),
        (7.0, 0.0, 3.0, 0.0),
    ] {
        let s = stages(d, a, h, r);
        let mut env = Env::default();
        env.trigger(1.0, &s);
        let total = (d + a + h + r) as usize;
        let out = run(&mut env, &s, Curve::Linear, total + 4, false);
        assert_eq!(
            env.stage,
            Stage::Idle,
            "{d}/{a}/{h}/{r} not finished after {total}+4"
        );
        /* Idle from exactly `total` onwards, and not before. */
        assert!(
            out[total..].iter().all(|v| *v == 0.0),
            "{d}/{a}/{h}/{r} still ducking past its length: {:?}",
            &out[total..]
        );
        if total > 0 {
            assert!(
                out[..total].iter().any(|v| *v > 0.0),
                "{d}/{a}/{h}/{r} never ducked"
            );
        }
    }
}

#[test]
fn linear_attack_is_a_straight_ramp() {
    let s = stages(0.0, 10.0, 5.0, 5.0);
    let mut env = Env::default();
    env.trigger(1.0, &s);
    let out = run(&mut env, &s, Curve::Linear, 10, false);
    for (i, v) in out.iter().enumerate() {
        let want = i as f64 / 10.0;
        assert!((v - want).abs() < 1e-12, "sample {i}: {v} != {want}");
    }
}

#[test]
fn delay_holds_the_signal_open_before_the_duck() {
    let s = stages(8.0, 4.0, 4.0, 4.0);
    let mut env = Env::default();
    env.trigger(1.0, &s);
    let out = run(&mut env, &s, Curve::Linear, 8, false);
    assert!(
        out.iter().all(|v| *v == 0.0),
        "the delay ducked something: {out:?}"
    );
    assert_eq!(env.stage, Stage::Delay);
    /* And the attack starts on the very next sample. */
    let v = env.next(Curve::Linear, &s, false);
    assert_eq!(v, 0.0, "attack's first sample is t=0");
    assert_eq!(env.stage, Stage::Attack);
}

#[test]
fn a_retrigger_never_steps_the_gain_upwards() {
    /* THE CLICK THIS PREVENTS. Retriggering mid-release with `from` reset to
     * zero would send the gain back to unity for one sample. */
    let s = stages(0.0, 20.0, 5.0, 100.0);
    let mut env = Env::default();
    env.trigger(1.0, &s);
    run(&mut env, &s, Curve::Linear, 40, false); /* into the release */
    assert_eq!(env.stage, Stage::Release);
    let before = env.duck;
    assert!(before > 0.1 && before < 1.0, "not mid-release: {before}");

    env.trigger(1.0, &s);
    let after = env.next(Curve::Linear, &s, false);
    assert!(
        after >= before - 1e-12,
        "gain jumped up on retrigger: {before} -> {after}"
    );
}

#[test]
fn a_release_falls_from_where_the_machine_got_to() {
    /* Release during the attack, so the envelope never reached `scale`.
     * Releasing from the floor would step the gain DOWN when asked to come up. */
    let s = stages(0.0, 100.0, 10.0, 50.0);
    let mut env = Env::default();
    env.trigger(1.0, &s);
    run(&mut env, &s, Curve::Linear, 20, false);
    let at = env.duck;
    assert!(at > 0.0 && at < 0.5, "not early in the attack: {at}");
    env.release(&s);
    let first = env.next(Curve::Linear, &s, false);
    assert!(
        (first - at).abs() < 1e-12,
        "release started from {first}, not {at}"
    );
}

#[test]
fn a_shallow_trigger_ducks_shallowly() {
    let s = stages(0.0, 4.0, 10.0, 4.0);
    let mut env = Env::default();
    env.trigger(0.25, &s);
    let out = run(&mut env, &s, Curve::Linear, 12, false);
    let peak = out.iter().cloned().fold(0.0f64, f64::max);
    assert!((peak - 0.25).abs() < 1e-12, "peak {peak} != scale 0.25");
}

#[test]
fn gate_mode_holds_until_released() {
    let s = stages(0.0, 2.0, 3.0, 4.0);
    let mut env = Env::default();
    env.trigger(1.0, &s);
    /* Far longer than Hold, and it must still be at the floor. */
    let out = run(&mut env, &s, Curve::Linear, 500, true);
    assert_eq!(env.stage, Stage::Hold);
    assert_eq!(*out.last().unwrap(), 1.0);
    env.release(&s);
    /* A release of four samples EMITS four and reaches Idle on the fifth call:
     * `pos` counts samples already emitted, so the stage is finished when the
     * fifth call finds `pos == len`. */
    run(&mut env, &s, Curve::Linear, 4, false);
    assert_eq!(env.stage, Stage::Release, "still releasing on its last sample");
    assert_eq!(env.next(Curve::Linear, &s, false), 0.0);
    assert_eq!(env.stage, Stage::Idle);
}

#[test]
fn every_stage_at_zero_is_a_no_op_not_a_hang() {
    let s = stages(0.0, 0.0, 0.0, 0.0);
    let mut env = Env::default();
    env.trigger(1.0, &s);
    /* The loop's bound is what makes this terminate rather than spin. */
    assert_eq!(env.next(Curve::Linear, &s, false), 0.0);
    assert_eq!(env.stage, Stage::Idle);
}
