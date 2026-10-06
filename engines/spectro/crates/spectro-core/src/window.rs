// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The analysis window.
 *
 * Hann, and only Hann. A spectrogram is read by eye, and the eye wants the
 * narrowest main lobe it can get with sidelobes low enough that a loud tone
 * does not smear a violet band across its neighbours. Hann is the usual answer
 * and iPlug2's own ISpectrumSender defaults to it; the others it offers
 * (Blackman-Harris, flattop) trade resolution for sidelobe depth, which is a
 * measurement concern rather than a picture one.
 *
 * COHERENT GAIN IS THE HALF NOBODY REMEMBERS. Multiplying by a window throws
 * away amplitude -- sum(hann)/N is 0.5 -- so a full-scale sine reads 6 dB down
 * unless the magnitude is scaled back up by it. `amplitude_scale` is that
 * factor, and it is why a sine at 1.0 shows as 0 dB and not as -6.
 *
 * WRITTEN HERE, NOT TAKEN FROM A CRATE, and the obvious crate was looked at.
 * apodize's `hanning_iter` is the SYMMETRIC window -- it divides by n-1, the
 * definition `hann` below rejects -- and 1.0.0, from April 2019, is still its
 * latest release. What is left is one cosine per sample, computed once per
 * configuration: not the kind of code a dependency saves anyone from.
 */

pub struct Window {
    pub gain: Vec<f32>,
    /// Multiply a magnitude by this to read amplitude: 2 / sum(gain).
    pub amplitude_scale: f32,
}

impl Window {
    pub fn hann(n: usize) -> Self {
        assert!(n >= 2);
        /* PERIODIC, NOT SYMMETRIC: the denominator is n, not n-1. A symmetric
         * window repeats its endpoint, which for overlap-add analysis puts a
         * small periodic ripple through the whole spectrogram. */
        let gain: Vec<f32> = (0..n)
            .map(|i| {
                let phase = 2.0 * core::f64::consts::PI * (i as f64) / (n as f64);
                (0.5 - 0.5 * phase.cos()) as f32
            })
            .collect();
        let sum: f64 = gain.iter().map(|&g| g as f64).sum();
        Self { gain, amplitude_scale: (2.0 / sum) as f32 }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_hann_window_is_a_bell_that_starts_at_zero() {
        let w = Window::hann(1024);
        assert!(w.gain[0].abs() < 1e-6, "the first sample is not zero");
        assert!((w.gain[512] - 1.0).abs() < 1e-6, "the centre is not unity");
        /* Rising to the middle, falling after it. */
        for i in 1..512 {
            assert!(w.gain[i] > w.gain[i - 1], "not monotone at {i}");
        }
    }

    #[test]
    fn coherent_gain_puts_a_full_scale_sine_at_unity() {
        let w = Window::hann(1024);
        /* sum(hann) is n/2 for the periodic window, so the scale is 4/n. */
        assert!((w.amplitude_scale - 4.0 / 1024.0).abs() < 1e-9);
    }
}
