/*!
The one-pole smoother's coefficient.
*/

/// One-pole coefficient for a time constant in ms at a sample rate:
/// `1 - exp(-1 / (ms * sr / 1000))`. A zero or absurd sample rate would give
/// a coefficient outside 0..1, and a one-pole above 1 oscillates -- hence
/// the guard and the clamp.
#[inline]
pub fn coeff(ms: f64, sample_rate: f64) -> f64 {
    let n = ms * sample_rate / 1000.0;
    if !(n > 0.0) {
        return 1.0;
    }
    (1.0 - (-1.0 / n).exp()).clamp(0.0, 1.0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stays_in_range_whatever_the_sample_rate() {
        for sr in [0.0, -1.0, f64::NAN, 1.0, 44100.0, 1e12] {
            let c = coeff(5.0, sr);
            assert!((0.0..=1.0).contains(&c), "{sr}: {c}");
        }
        assert_eq!(coeff(5.0, 0.0), 1.0);
        /* One time constant leaves 1/e of a step behind. */
        let c = coeff(1.0, 48000.0);
        let left = (1.0 - c).powi(48);
        assert!((left - (-1.0f64).exp()).abs() < 1e-12);
    }
}
