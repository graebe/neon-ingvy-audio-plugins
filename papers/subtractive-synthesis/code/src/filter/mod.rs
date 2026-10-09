// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
Filters in the topology-preserving transform (TPT): each analog integrator is
replaced by a trapezoidal one, and the delay-free loop that creates is solved
in closed form instead of being broken by an extra unit delay. Cutoff and
resonance can then move every sample without the filter blowing up, and the
cutoff lands exactly where it is asked for, by pre-warping.
*/

pub mod ladder;
pub mod svf;

/// The pre-warped integrator gain `tan(pi fc / fs)`, with the cutoff kept
/// just under Nyquist where the tangent is still finite.
#[inline]
pub fn prewarp(fc: f32, fs: f32) -> f32 {
    let w = core::f32::consts::PI * (fc / fs).clamp(0.0, 0.499);
    w.tan()
}

#[cfg(test)]
pub(crate) mod testing {
    /// The steady-state amplitude of `f` driven by a unit sine at `freq`.
    pub fn gain(mut f: impl FnMut(f32) -> f32, freq: f32, fs: f32) -> f32 {
        let w = 2.0 * core::f32::consts::PI * freq / fs;
        let settle = (fs * 0.5) as usize;
        let mut peak = 0.0f32;
        for n in 0..settle + (fs * 0.1) as usize {
            let y = f((w * n as f32).sin());
            if n >= settle {
                peak = peak.max(y.abs());
            }
        }
        peak
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn prewarping_is_finite_up_to_nyquist() {
        assert_eq!(prewarp(0.0, 48_000.0), 0.0);
        assert!((prewarp(12_000.0, 48_000.0) - 1.0).abs() < 1e-6);
        assert!(prewarp(1e9, 48_000.0).is_finite());
    }
}
