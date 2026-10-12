// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The elliptic prototype in closed form, by Landen's transformation (Sophocles
J. Orfanidis, Lecture Notes on Elliptic Filter Design, 2006): the Jacobi
functions `cd` and `sn` and the inverse of `sn` as a descending sequence of
moduli, seven of which reach double precision for any modulus a filter uses.

Normalised as Orfanidis normalises it: the passband edge at 1 rad/s, the
stopband edge at 1/k. proto.rs moves the -3 dB point to 1 afterwards.
*/

use num_complex::Complex64;

/// Landen's descending moduli of `k`.
const STEPS: usize = 7;

fn landen(mut k: f64) -> [f64; STEPS] {
    let mut v = [0.0; STEPS];
    for vi in v.iter_mut() {
        k = (k / (1.0 + (1.0 - k * k).sqrt())).powi(2);
        *vi = k;
    }
    v
}

/// Ascending from the circular function `w0` of `u·π/2` to the Jacobi one.
fn ascend(mut w: Complex64, v: &[f64; STEPS]) -> Complex64 {
    for &vi in v.iter().rev() {
        w = w * (1.0 + vi) / (1.0 + vi * w * w);
    }
    w
}

/// cd(u·K, k).
pub fn cd(u: Complex64, k: f64) -> Complex64 {
    ascend((u * core::f64::consts::FRAC_PI_2).cos(), &landen(k))
}

/// sn(u·K, k).
pub fn sn(u: Complex64, k: f64) -> Complex64 {
    ascend((u * core::f64::consts::FRAC_PI_2).sin(), &landen(k))
}

/// u such that sn(u·K, k) = w.
pub fn asn(mut w: Complex64, k: f64) -> Complex64 {
    let v = landen(k);
    for i in 0..STEPS {
        let before = if i == 0 { k } else { v[i - 1] };
        w = w / (1.0 + (1.0 - w * w * before * before).sqrt()) * (2.0 / (1.0 + v[i]));
    }
    w.asin() * (2.0 / core::f64::consts::PI)
}

/// The selectivity modulus k an order `n` needs for the discrimination
/// modulus `k1` (the degree equation, solved exactly).
pub fn degree(n: usize, k1: f64) -> f64 {
    let k1p = (1.0 - k1 * k1).sqrt();
    let mut prod = 1.0;
    for i in 1..=n / 2 {
        let u = (2 * i - 1) as f64 / n as f64;
        prod *= sn(Complex64::new(u, 0.0), k1p).re.powi(4);
    }
    let kp = k1p.powi(n as i32) * prod;
    (1.0 - kp * kp).sqrt()
}
