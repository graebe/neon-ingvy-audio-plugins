// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
How much of a periodic signal is aliasing: every bin of a windowed spectrum
is either near a harmonic of `f0` (signal) or not (alias, plus whatever noise
floor the algorithm has). Not for the audio thread: it allocates and plans an
FFT on every call.

The window is the 7-term Blackman-Harris, whose sidelobes sit near -180 dB, so
the leakage of a strong harmonic does not pass for aliasing at the levels a
good oscillator reaches. Its main lobe is seven bins either side; anything
within [`GUARD`] bins of a harmonic counts as that harmonic.
*/

use realfft::RealFftPlanner;

/// Bins either side of a harmonic that belong to it.
pub const GUARD: usize = 10;

const BH7: [f64; 7] = [
    0.271_051_400_693_42,
    0.433_297_939_234_48,
    0.218_122_999_543_11,
    0.065_925_446_388_03,
    0.010_811_742_098_37,
    0.000_776_584_825_22,
    0.000_013_887_217_35,
];

/// The power spectrum of `x`, windowed, bins `0..=n/2`.
pub fn power_spectrum(x: &[f32]) -> Vec<f64> {
    let n = x.len();
    let mut buf: Vec<f64> = x
        .iter()
        .enumerate()
        .map(|(i, &v)| {
            let t = 2.0 * core::f64::consts::PI * i as f64 / n as f64;
            let w: f64 = BH7.iter().enumerate().map(|(k, &a)| if k % 2 == 0 { a } else { -a } * (k as f64 * t).cos()).sum();
            v as f64 * w
        })
        .collect();
    let fft = RealFftPlanner::<f64>::new().plan_fft_forward(n);
    let mut out = fft.make_output_vec();
    fft.process(&mut buf, &mut out).expect("sizes are the plan's");
    out.iter().map(|c| c.norm_sqr()).collect()
}

/// What [`alias`] finds.
#[derive(Clone, Copy, Debug)]
pub struct Alias {
    /// Harmonic power over everything else, in dB. Higher is cleaner.
    pub asr_db: f64,
    /// The strongest non-harmonic bin, relative to the strongest harmonic.
    pub worst_db: f64,
}

/// Signal-to-alias ratio of `x`, a tone at `f0`, counting bins up to `band` Hz.
pub fn alias(x: &[f32], f0: f64, fs: f64, band: f64) -> Alias {
    let p = power_spectrum(x);
    let df = fs / x.len() as f64;
    let (mut sig, mut ali, mut peak_sig, mut peak_ali) = (0.0, 0.0, 0.0f64, 0.0f64);
    for (k, &pk) in p.iter().enumerate().skip(GUARD + 1) {
        let f = k as f64 * df;
        if f > band {
            break;
        }
        let h = (f / f0).round();
        let harmonic = h >= 1.0 && h * f0 < fs / 2.0 && (f - h * f0).abs() <= GUARD as f64 * df;
        if harmonic {
            sig += pk;
            peak_sig = peak_sig.max(pk);
        } else {
            ali += pk;
            peak_ali = peak_ali.max(pk);
        }
    }
    let db = |r: f64| 10.0 * r.max(1e-300).log10();
    Alias { asr_db: db(sig / ali), worst_db: db(peak_ali / peak_sig) }
}

/// `n` samples of an oscillator after `skip` samples of start-up.
pub fn render(mut next: impl FnMut() -> f32, skip: usize, n: usize) -> Vec<f32> {
    (0..skip).for_each(|_| {
        next();
    });
    (0..n).map(|_| next()).collect()
}

/// The equal-tempered frequency of MIDI note `n`.
pub fn note_hz(n: i32) -> f64 {
    440.0 * 2f64.powf((n - 69) as f64 / 12.0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_pure_sine_is_all_signal() {
        let f0 = note_hz(69);
        let w = 2.0 * core::f64::consts::PI * f0 / 48_000.0;
        let x: Vec<f32> = (0..1 << 16).map(|n| (w * n as f64).sin() as f32).collect();
        let a = alias(&x, f0, 48_000.0, 24_000.0);
        /* f32 rounding is the only thing that is not the sine. */
        assert!(a.asr_db > 130.0, "{a:?}");
    }

    #[test]
    fn a_tone_off_the_harmonic_grid_is_counted_as_alias() {
        let (f0, fs) = (note_hz(69), 48_000.0);
        let x: Vec<f32> = (0..1 << 16)
            .map(|n| {
                let t = n as f64 / fs;
                ((2.0 * core::f64::consts::PI * f0 * t).sin() + 0.01 * (2.0 * core::f64::consts::PI * 1234.5 * t).sin()) as f32
            })
            .collect();
        let a = alias(&x, f0, fs, fs / 2.0);
        assert!((a.asr_db - 40.0).abs() < 0.5, "{a:?}");
        assert!((a.worst_db + 40.0).abs() < 0.5, "{a:?}");
    }

    #[test]
    fn notes_are_equal_tempered() {
        assert_eq!(note_hz(69), 440.0);
        assert!((note_hz(60) - 261.625_565).abs() < 1e-5);
        assert_eq!(render(|| 1.0, 3, 2), vec![1.0, 1.0]);
    }
}
