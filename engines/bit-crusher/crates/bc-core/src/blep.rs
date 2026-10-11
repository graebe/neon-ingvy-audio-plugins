// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The band-limited kernel Anti-Alias renders the hold with: one windowed sinc,
read two ways.

AS AN INTERPOLATOR it reads the input between samples, so a level is taken at
the exact instant the hold's clock says -- 3.675 samples after the last, not
3 or 4 -- rather than at the nearest host sample.

AS A BAND-LIMITED STEP (BLEP; Stilson & Smith, ICMC 1996; Brandt, ICMC 2001)
it is the running integral of the same sinc: a step that rises over the
kernel's width instead of jumping. The hold's output is a staircase in
continuous time; each step is drawn as this band-limited step at its exact
instant, so nothing of the staircase lies above the project's Nyquist to fold
back down. The images of a hold -- the tone mirrored about every multiple of
the Rate -- are the effect, and stay; what goes is everything the host's
sample grid would have added to them. The result is the same at 44.1, 48 or
96 kHz.

THE KERNEL. A sinc with its -6 dB point at 0.4325 of the sample rate, under a
Kaiser window (β = 6.755, about 70 dB of stopband), 2·HALF_WIDTH samples long:
flat to about 0.37 of the sample rate (16 kHz at 44.1 kHz) and at least 70 dB
down from half the sample rate on, so whatever would fold back is that far
below. HALF_WIDTH samples of the input ahead of the instant are needed to
interpolate, and HALF_WIDTH of the output after it to draw the step, so the
crusher runs LATENCY = 2·HALF_WIDTH samples late -- always, Anti-Alias on or
off, so switching it moves nothing in time.

THE TABLES are the kernel and its integral at PHASES points a sample,
computed once for the process (a LazyLock: the first Engine::new pays, never
an audio thread) and read with linear interpolation between points.
*/

use std::sync::LazyLock;

/// The kernel's half-width in samples.
pub const HALF_WIDTH: usize = 16;
/// What it costs: input ahead to interpolate, output after to draw a step.
pub const LATENCY: usize = 2 * HALF_WIDTH;
/// Table points per sample.
const PHASES: usize = 256;
/// The sinc's cutoff, in cycles per sample (the -6 dB point).
const CUTOFF: f64 = 0.4325;
/// The Kaiser window's β: about 70 dB of stopband.
const BETA: f64 = 6.755;

const POINTS: usize = 2 * HALF_WIDTH * PHASES + 1;

/// The kernel and its running integral (the band-limited step), at
/// t = -HALF_WIDTH + i/PHASES for i in 0..POINTS. The step is kept rather
/// than its residual: the residual jumps at t = 0, and a table read by
/// linear interpolation would smear the jump.
struct Tables {
    kernel: Vec<f64>,
    step: Vec<f64>,
}

/// The zeroth-order modified Bessel function of the first kind, by its
/// series: what the Kaiser window is made of.
fn bessel_i0(x: f64) -> f64 {
    let (mut sum, mut term, q) = (1.0, 1.0, 0.25 * x * x);
    for k in 1..64 {
        term *= q / (k * k) as f64;
        sum += term;
        if term < 1e-17 * sum {
            break;
        }
    }
    sum
}

static TABLES: LazyLock<Tables> = LazyLock::new(|| {
    let w = HALF_WIDTH as f64;
    let norm = bessel_i0(BETA);
    let kernel: Vec<f64> = (0..POINTS)
        .map(|i| {
            let t = -w + i as f64 / PHASES as f64;
            let x = 2.0 * CUTOFF * t;
            let sinc = if x.abs() < 1e-12 { 1.0 } else { (core::f64::consts::PI * x).sin() / (core::f64::consts::PI * x) };
            let r = t / w;
            let window = if r.abs() >= 1.0 { 0.0 } else { bessel_i0(BETA * (1.0 - r * r).sqrt()) / norm };
            2.0 * CUTOFF * sinc * window
        })
        .collect();
    /* The running integral, by the trapezoid rule at the table's own
     * resolution, scaled so the whole is exactly 1: a step of height 1. */
    let mut integral = vec![0.0; POINTS];
    for i in 1..POINTS {
        integral[i] = integral[i - 1] + 0.5 * (kernel[i - 1] + kernel[i]) / PHASES as f64;
    }
    let total = integral[POINTS - 1];
    let step = integral.iter().map(|v| v / total).collect();
    Tables { kernel, step }
});

/// Build the tables now, off any audio thread.
pub fn prepare() {
    LazyLock::force(&TABLES);
}

#[inline]
fn lookup(table: &[f64], t: f64) -> f64 {
    let pos = (t + HALF_WIDTH as f64) * PHASES as f64;
    if !(pos > 0.0 && pos < (POINTS - 1) as f64) {
        return 0.0;
    }
    let i = pos as usize;
    let f = pos - i as f64;
    table[i] + (table[i + 1] - table[i]) * f
}

/// The kernel at `t` samples from its centre; 0 outside its width.
#[inline]
pub fn kernel(t: f64) -> f64 {
    lookup(&TABLES.kernel, t)
}

/// The band-limited unit step at `t` minus the plain one: what turns a
/// sample-and-hold's jump into a band-limited step. 0 outside its width.
#[inline]
pub fn residual(t: f64) -> f64 {
    let pos = (t + HALF_WIDTH as f64) * PHASES as f64;
    if !(pos > 0.0 && pos < (POINTS - 1) as f64) {
        return 0.0;
    }
    lookup(&TABLES.step, t) - if t >= 0.0 { 1.0 } else { 0.0 }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_step_rises_from_nothing_to_one_across_the_kernel() {
        prepare();
        assert!(residual(-(HALF_WIDTH as f64) + 0.01).abs() < 1e-4);
        assert!(residual(HALF_WIDTH as f64 - 0.01).abs() < 1e-4);
        /* Half way up at the instant itself: just before it, the step is
         * half done; just after, half still to come. */
        assert!((residual(-1e-9) - 0.5).abs() < 1e-3);
        assert!((residual(1e-9) + 0.5).abs() < 1e-3);
        assert_eq!(residual(f64::NAN), 0.0);
    }

    #[test]
    fn the_kernel_passes_dc_and_is_symmetric() {
        prepare();
        for frac in [0.0, 0.25, 0.5, 0.9] {
            let sum: f64 = (-(HALF_WIDTH as i64)..HALF_WIDTH as i64).map(|m| kernel(m as f64 + frac)).sum();
            assert!((sum - 1.0).abs() < 1e-3, "{frac}: {sum}");
        }
        assert!((kernel(3.3) - kernel(-3.3)).abs() < 1e-12);
        assert_eq!(kernel(HALF_WIDTH as f64 + 1.0), 0.0);
    }

    #[test]
    fn the_kernel_is_flat_in_the_band_and_deep_beyond_half_the_rate() {
        prepare();
        /* The continuous kernel's spectrum, f in cycles a sample: what a
         * band-limited step leaves of a staircase at f. It is symmetric, so
         * the transform is real. */
        let gain = |f: f64| {
            let n = 2 * HALF_WIDTH * 64;
            (0..=n)
                .map(|i| {
                    let t = -(HALF_WIDTH as f64) + i as f64 / 64.0;
                    kernel(t) * (2.0 * core::f64::consts::PI * f * t).cos() / 64.0
                })
                .sum::<f64>()
                .abs()
        };
        assert!((gain(0.0) - 1.0).abs() < 1e-3);
        assert!((gain(0.30) - 1.0).abs() < 0.01, "flat at 0.30: {}", gain(0.30));
        for f in [0.5, 0.55, 0.7, 1.0, 1.5] {
            assert!(gain(f) < 10f64.powf(-66.0 / 20.0), "deep at {f}: {}", gain(f));
        }
    }
}
