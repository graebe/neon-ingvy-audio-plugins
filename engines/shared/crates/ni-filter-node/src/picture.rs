// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
What a picture's curve is made of: a frequency as the angle a cascade's
response is read at, and a transfer as the dB it is drawn at.
*/

use ni_filter::Complex64;

/// `hz` as radians a sample at `fs`, held a hair under Nyquist.
pub fn angle(hz: f64, fs: f64) -> f64 {
    2.0 * core::f64::consts::PI * hz.min(0.4999 * fs) / fs
}

/// A transfer's magnitude in dB, floored at -300 dB.
pub fn decibels(h: Complex64) -> f32 {
    (20.0 * h.norm().max(1e-15).log10()) as f32
}
