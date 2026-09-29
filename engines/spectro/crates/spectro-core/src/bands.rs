/*
 * The frequency axis: FFT bins folded into log-spaced bands.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * AN FFT IS LINEAR IN FREQUENCY AND MUSIC IS NOT. At 48 kHz with N=1024 the
 * bins are 46.9 Hz apart, so the octave from 40 to 80 Hz -- the whole bottom of
 * a kick drum -- is ONE bin, while the octave from 10 to 20 kHz is 213 of them.
 * Drawn linearly, a spectrogram is nine tenths cymbal.
 *
 * So the picture's vertical axis is log frequency and the bands are computed
 * once, here: `bands` geometric steps from f_min to f_max, each owning the bins
 * whose centres fall inside it.
 *
 * PEAK, NOT MEAN, within a band -- the same call the Trance Gate's scope makes
 * about its columns. A band at 15 kHz holds dozens of bins and a mean over them
 * reports the noise floor between the partials rather than the partials; a peak
 * reports the loudest thing actually present, which is what a reader is looking
 * for.
 *
 * THE BOTTOM BANDS OWN NO BIN AT ALL and that is not an error to guard against
 * later: a band is 3% wide, a bin is 5.86 Hz at 48 kHz with N=8192, so every
 * band below ~194 Hz is narrower than the grid it is measured on. That is 100
 * of the 256 rows -- the bottom two fifths of the picture.
 *
 * THOSE BANDS ARE INTERPOLATED, NOT SNAPPED, AND THAT IS WHAT MAKES THE LOW END
 * READ AS A GRADIENT RATHER THAN AS BLOCKS.
 *
 * Snapping each of them to its nearest bin gave four or five consecutive bands
 * the SAME bin and therefore the same byte -- a flat block four pixels tall,
 * then a step to the next bin's value. Stacked up the bottom of the picture
 * that is a staircase, and it read as patchiness in the analysis rather than as
 * what it was: the display asking for more resolution than the transform has.
 *
 * So a band with no bin of its own carries its centre in BIN UNITS and the
 * magnitude is interpolated between the two bins either side of it.
 * GEOMETRICALLY -- equivalently, linearly in dB -- because the picture's
 * intensity axis is logarithmic, so that is the space in which a straight line
 * looks straight. Interpolating linearly in amplitude leaves the loud neighbour
 * dominating and the staircase half visible.
 *
 * It invents no detail the transform does not have: it is the same information,
 * resampled onto the axis the picture actually draws, instead of being held
 * constant across it.
 */

/// A band's half-open bin range, `lo..hi`, never empty.
#[derive(Clone, Copy, Debug)]
pub struct Band {
    pub lo: usize,
    pub hi: usize,
    /// `None` when the band owns bins and is reduced by PEAK.
    ///
    /// `Some(x)` when it is narrower than a bin and owns none: `x` is the
    /// band's centre in bin units, `lo` is `floor(x)` and `hi` is `lo + 1`, and
    /// the magnitude is interpolated between those two rather than taken from
    /// the nearer of them. See the header.
    pub frac: Option<f32>,
}

pub struct Bands {
    pub ranges: Vec<Band>,
    /// Geometric centre of each band, in Hz -- the UI's axis labels come from
    /// here so the mapping is never written down twice.
    pub centres: Vec<f32>,
}

impl Bands {
    /// `n_bins` is the usable bin count, `fft_size / 2 + 1` (DC through
    /// Nyquist). `f_max` is clamped to Nyquist: a band above it could only ever
    /// be empty, and an empty band at the TOP would silently repeat the Nyquist
    /// bin up the rest of the picture.
    pub fn new(count: usize, n_bins: usize, sample_rate: f32, f_min: f32, f_max: f32) -> Self {
        let mut bands = Self {
            ranges: Vec::with_capacity(count),
            centres: Vec::with_capacity(count),
        };
        bands.rebuild(count, n_bins, sample_rate, f_min, f_max);
        bands
    }

    /// Recompute the table in place, into the vectors it already owns.
    ///
    /// **ALLOCATION-FREE ONCE BUILT, WHICH IS WHY IT EXISTS.** The range
    /// dropdown changes this table, and the table is read on the audio thread --
    /// so the audio thread rebuilds it itself rather than taking a new one from
    /// somewhere else. `Vec::clear` keeps capacity, so as long as `count` does
    /// not change (it cannot: it is fixed at `configure`) nothing is allocated
    /// and nothing is freed. `tests/no_alloc.rs` covers exactly this path.
    pub fn rebuild(
        &mut self,
        count: usize,
        n_bins: usize,
        sample_rate: f32,
        f_min: f32,
        f_max: f32,
    ) {
        assert!(count >= 1 && n_bins >= 2 && sample_rate > 0.0);

        let nyquist = sample_rate * 0.5;
        let bin_hz = sample_rate / ((n_bins - 1) as f32 * 2.0);
        /* Never below the first bin above DC: a band edge at 0 Hz has no
         * logarithm, and DC is not a pitch. */
        let lo_hz = f_min.max(bin_hz).max(1.0);
        let hi_hz = f_max.min(nyquist).max(lo_hz * 2.0);

        let (log_lo, log_hi) = (lo_hz.ln(), hi_hz.ln());
        let edge = |i: usize| (log_lo + (log_hi - log_lo) * (i as f32) / (count as f32)).exp();

        self.ranges.clear();
        self.centres.clear();

        for b in 0..count {
            let (e_lo, e_hi) = (edge(b), edge(b + 1));
            let centre = (e_lo * e_hi).sqrt();
            self.centres.push(centre);

            /* Bin k is centred at k * bin_hz, so the bins inside [e_lo, e_hi)
             * are ceil(e_lo / bin_hz) .. ceil(e_hi / bin_hz). Bin 0 is DC and
             * belongs to no band. */
            let lo = ((e_lo / bin_hz).ceil() as usize).max(1).min(n_bins - 1);
            let hi = ((e_hi / bin_hz).ceil() as usize).max(lo).min(n_bins);

            self.ranges.push(if hi > lo {
                Band { lo, hi, frac: None }
            } else {
                /*
                 * No bin of its own, so it sits BETWEEN two. Clamped so that
                 * `lo + 1` is still a bin: the pair is what gets interpolated,
                 * and a centre past the last bin would otherwise index off the
                 * end of the spectrum.
                 */
                let x = centre / bin_hz;
                let k = (x.floor() as usize).clamp(1, n_bins.saturating_sub(2).max(1));
                Band { lo: k, hi: k + 1, frac: Some(x) }
            });
        }
    }

    pub fn len(&self) -> usize {
        self.ranges.len()
    }
}

/// The band centre frequencies for a range, written into `out`, ascending.
/// Returns how many were written.
///
/// **A PURE FUNCTION, AND THAT IS THE POINT.** The editor's frequency scale is
/// drawn from these, and it is the message thread that asks. Reading the audio
/// thread's own table would be a data race the moment the range changes; deriving
/// the same numbers from the same inputs is not. The two agree because they are
/// the same arithmetic, and `the_axis_matches_the_table` says so.
pub fn centres_for(
    out: &mut [f32],
    count: usize,
    n_bins: usize,
    sample_rate: f32,
    f_min: f32,
    f_max: f32,
) -> usize {
    if count == 0 || n_bins < 2 || !(sample_rate > 0.0) {
        return 0;
    }
    let nyquist = sample_rate * 0.5;
    let bin_hz = sample_rate / ((n_bins - 1) as f32 * 2.0);
    let lo_hz = f_min.max(bin_hz).max(1.0);
    let hi_hz = f_max.min(nyquist).max(lo_hz * 2.0);
    let (log_lo, log_hi) = (lo_hz.ln(), hi_hz.ln());
    let edge = |i: usize| (log_lo + (log_hi - log_lo) * (i as f32) / (count as f32)).exp();

    let n = count.min(out.len());
    for (b, slot) in out.iter_mut().take(n).enumerate() {
        *slot = (edge(b) * edge(b + 1)).sqrt();
    }
    n
}

/// Amplitude (1.0 = full scale) to one byte, through dB.
///
/// `0` means "at or below the floor", which is what makes silence draw as
/// exactly the ground colour rather than as the first step of the ramp.
#[inline]
pub fn amplitude_to_byte(amplitude: f32, db_floor: f32, db_ceil: f32) -> u8 {
    /* A NaN reaching here would become an arbitrary byte, i.e. an arbitrary
     * colour in the middle of the picture. Not finite -> the floor. */
    if !amplitude.is_finite() || amplitude <= 0.0 {
        return 0;
    }
    /* 1e-20 rather than a branch on zero: log10(0) is -inf, and -inf below the
     * floor still clamps correctly, but the multiply first is cheaper. */
    let db = 20.0 * (amplitude + 1e-20).log10();
    let span = (db_ceil - db_floor).max(1.0);
    let t = ((db - db_floor) / span).clamp(0.0, 1.0);
    (t * 255.0 + 0.5) as u8
}

#[cfg(test)]
mod tests {
    use super::*;

    const SR: f32 = 48_000.0;
    const BINS: usize = 513; /* fft_size 1024 */

    #[test]
    fn bands_are_ordered_and_inside_the_requested_range() {
        let b = Bands::new(128, BINS, SR, 20.0, 20_000.0);
        assert_eq!(b.len(), 128);
        for i in 1..b.len() {
            assert!(b.centres[i] > b.centres[i - 1], "centres not ascending at {i}");
            assert!(b.ranges[i].lo >= b.ranges[i - 1].lo, "bin ranges not ascending at {i}");
        }
        for r in &b.ranges {
            assert!(r.hi > r.lo, "an empty band survived");
            assert!(r.hi <= BINS, "a band reaches past Nyquist");
            assert!(r.lo >= 1, "a band claims DC");
        }
        assert!(*b.centres.last().unwrap() <= SR * 0.5, "a centre above Nyquist");
    }

    #[test]
    fn the_axis_is_logarithmic() {
        let b = Bands::new(120, BINS, SR, 20.0, 20_000.0);
        /* Equal ratios, not equal differences: every step multiplies the
         * frequency by the same factor to within float error. */
        let ratio = b.centres[1] / b.centres[0];
        for i in 2..b.len() {
            let r = b.centres[i] / b.centres[i - 1];
            assert!((r - ratio).abs() < 1e-3, "step {i} was {r}, not {ratio}");
        }
    }

    #[test]
    fn f_max_is_clamped_to_nyquist_at_low_sample_rates() {
        /* 20 kHz asked for, 22.05 kHz available -- fine. At 44.1 kHz Nyquist is
         * 22050, but at 32 kHz it is 16000 and the top bands would be empty. */
        let b = Bands::new(64, BINS, 32_000.0, 20.0, 20_000.0);
        assert!(*b.centres.last().unwrap() <= 16_000.0);
        for r in &b.ranges {
            assert!(r.hi <= BINS);
        }
    }

    #[test]
    fn the_byte_scale_runs_floor_to_ceiling() {
        assert_eq!(amplitude_to_byte(0.0, -96.0, 0.0), 0, "silence is not the floor");
        assert_eq!(amplitude_to_byte(f32::NAN, -96.0, 0.0), 0, "a NaN became a colour");
        assert_eq!(amplitude_to_byte(1.0, -96.0, 0.0), 255, "full scale is not the ceiling");
        assert_eq!(amplitude_to_byte(4.0, -96.0, 0.0), 255, "over full scale did not clamp");
        /* -48 dB is halfway up a -96..0 ramp. */
        let mid = amplitude_to_byte(10f32.powf(-48.0 / 20.0), -96.0, 0.0);
        assert!((i32::from(mid) - 128).abs() <= 1, "-48 dB landed at {mid}");
    }
}
