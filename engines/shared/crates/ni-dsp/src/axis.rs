// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
A picture's frequency axis: `N` points from `lo` to `hi` in equal steps of
pitch, so an octave is as wide at 40 Hz as at 10 kHz -- what every response
curve and spectrum the editors draw is sampled on.
*/

/// `N` frequencies from `lo` to `hi`, both included, each the same ratio above
/// the last. `lo` and `hi` are positive, `hi` above `lo`, and `N` at least 2.
pub fn log_points<const N: usize>(lo: f64, hi: f64) -> [f64; N] {
    let span = hi / lo;
    core::array::from_fn(|i| lo * span.powf(i as f64 / (N - 1) as f64))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_points_span_the_axis_in_equal_steps_of_pitch() {
        let p: [f64; 11] = log_points(20.0, 20_480.0);
        assert_eq!(p[0], 20.0);
        assert!((p[10] - 20_480.0).abs() < 1e-9);
        for w in p.windows(2) {
            assert!((w[1] / w[0] - 2.0).abs() < 1e-12, "one octave a step");
        }
    }
}
