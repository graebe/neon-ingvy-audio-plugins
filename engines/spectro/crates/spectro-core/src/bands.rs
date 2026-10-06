// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The frequency axis: FFT bins folded into log-spaced bands.
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

    pub fn is_empty(&self) -> bool {
        self.ranges.is_empty()
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
#[allow(clippy::neg_cmp_op_on_partial_ord, reason = "a NaN sample rate must take the guard, and gives no centres")]
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

/// Power (amplitude squared, 1.0 = full scale) to one byte -- the same scale
/// as [`amplitude_to_byte`], without the square root.
#[inline]
pub fn power_to_byte(power: f32, db_floor: f32, db_ceil: f32) -> u8 {
    if !power.is_finite() || power <= 0.0 {
        return 0;
    }
    db_to_byte(10.0 * power.log10(), db_floor, db_ceil)
}

/* ------------------------------------------------------------------ clash --
 *
 * WHERE TWO SOURCES ARE FIGHTING, which is not the same question as where they
 * overlap.
 *
 * The obvious metric is the product of the two spectra, and it is the wrong
 * one. A product in amplitude is a SUM in dB, so 0 dB against -60 dB scores
 * exactly what -30 dB against -30 dB scores -- and only the second is a clash.
 * The first is one source winning outright, which is what a mix is supposed to
 * sound like.
 *
 * So two conditions, and both are needed:
 *
 *   BOTH PRESENT      min(a, b) above a floor. `min` is high only where
 *                     neither source is quiet, which is the actual question.
 *   COMPARABLE        |a - b| within a window. Past about 9-12 dB the louder
 *                     source simply masks the other; that is a source being
 *                     buried, not two sources competing, and painting it would
 *                     bury the real clashes in orange.
 *
 * IT IS ALL BYTE ARITHMETIC, and that is not a shortcut. A column byte is
 * already linear in dB -- see `amplitude_to_byte` above -- so `min` of two
 * bytes IS `min` of two decibels, and a byte difference IS a dB difference
 * scaled by 255/96. Nothing needs converting back.
 *
 * The result is a STRENGTH, not a flag: 0 where there is no clash, rising with
 * how far above the floor the quieter source is and how evenly the two are
 * matched. The picture paints it as an intensity, so a cell where two sources
 * sit at -6 dB together has to read louder than one where they meet at -50.
 */

/// Clash strength for one cell, 0 where there is none.
///
/// `floor` and `balance` are in the same byte units as the levels; see
/// [`db_to_byte`] and [`db_span_to_byte`].
#[inline]
pub fn clash_cell(a: u8, b: u8, floor: u8, balance: u8) -> u8 {
    let lo = a.min(b);
    if lo < floor {
        return 0;
    }
    let diff = a.abs_diff(b);
    if diff > balance {
        return 0;
    }

    /*
     * Two factors, multiplied, both 0..=255:
     *
     *   depth    how far the QUIETER source is above the floor -- the headroom
     *            it has to be heard in
     *   even     how matched they are, falling to nothing at the edge of the
     *            balance window so the region fades out rather than ending on
     *            a hard line nobody chose
     *
     * `balance == 0` means "only exact ties", and then `even` is 255 rather
     * than a divide by zero.
     */
    let span = 255 - floor as u16;
    let depth = ((lo - floor) as u16 * 255).checked_div(span).unwrap_or(255);
    let even = if balance == 0 {
        255u16
    } else {
        255 - (diff as u16 * 255) / balance as u16
    };
    ((depth * even) / 255) as u8
}

/// A whole column. `out` is filled for `min(a.len(), b.len(), out.len())` bands
/// and zeroed past it, so a caller never draws a stale tail.
pub fn clash_column(a: &[u8], b: &[u8], out: &mut [u8], floor: u8, balance: u8) {
    let n = a.len().min(b.len()).min(out.len());
    for i in 0..n {
        out[i] = clash_cell(a[i], b[i], floor, balance);
    }
    for slot in out.iter_mut().skip(n) {
        *slot = 0;
    }
}

/// A dBFS level as the byte the engine would have encoded it as.
///
/// The inverse of `amplitude_to_byte`'s scaling, and the only correct way to
/// turn "flag anything above -60 dB" into a threshold these bytes can be
/// compared against.
pub fn db_to_byte(db: f32, db_floor: f32, db_ceil: f32) -> u8 {
    let span = (db_ceil - db_floor).max(1.0);
    let t = ((db - db_floor) / span).clamp(0.0, 1.0);
    (t * 255.0 + 0.5) as u8
}

/// A byte back to the dBFS it stands for -- the inverse of [`db_to_byte`].
///
/// BYTE 0 IS NOT `db_floor`, IT IS "AT OR BELOW IT". `amplitude_to_byte`
/// returns 0 for silence and for anything under the floor alike, so this
/// returns [`f32::NEG_INFINITY`] rather than pretending to a level it was never
/// told. A caller summing power needs that distinction: four silent channels
/// must add to silence, and they will not if each contributes 10^(floor/10).
pub fn byte_to_db(byte: u8, db_floor: f32, db_ceil: f32) -> f32 {
    if byte == 0 {
        return f32::NEG_INFINITY;
    }
    let span = (db_ceil - db_floor).max(1.0);
    db_floor + (byte as f32 / 255.0) * span
}

/* -------------------------------------------------------------------- sum --
 *
 * SEVERAL SOURCES INTO ONE PICTURE, and it cannot be done in byte space.
 *
 * A byte here is linear in dB (see `amplitude_to_byte`), which is exactly what
 * makes `clash_cell` above cheap -- and exactly what makes summing impossible
 * the same way. Adding two bytes adds two DECIBELS, which is multiplying two
 * amplitudes: -20 dB and -20 dB would come out at -40, quieter than either.
 *
 * So a sum has to leave byte space, add POWER, and come back:
 *
 *     db  = floor + (byte / 255) * (ceil - floor)
 *     pow = 10^(db / 10)
 *     out = db_to_byte(10 * log10(sum of pow))
 *
 * POWER RATHER THAN AMPLITUDE because two tracks are not phase locked. Adding
 * amplitudes assumes they are, and would put two equal sources 6 dB up; adding
 * power puts them at +3, which is what two uncorrelated sources of equal level
 * actually measure. A mix of a bass and a pad is the incoherent case.
 */

/*
 * AS TABLES, because a column is 256 cells and a sum was a `powf` per cell per
 * source and a `log10` per cell. A byte has 256 values, so its power is a
 * lookup; and a power maps back to the byte whose edges it falls between, so
 * the way back is a binary search over the 255 edges. The edges are found by
 * bisecting the encoder above -- `db_to_byte(10 * log10(p))` -- over f32, so
 * the table reproduces that arithmetic exactly rather than approximating it.
 */

/// The byte scale between one `db_floor` and `db_ceil`, as power tables.
pub struct PowerTable {
    /// Byte to power; byte 0 is silence and contributes nothing.
    power: [f32; 256],
    /// `edge[b - 1]` is the least power that encodes as byte `b` or above.
    edge: [f32; 255],
}

impl PowerTable {
    pub fn new(db_floor: f32, db_ceil: f32) -> Self {
        let encode = |p: f32| {
            if p > 0.0 {
                db_to_byte(10.0 * p.log10(), db_floor, db_ceil)
            } else {
                0
            }
        };
        let power = core::array::from_fn(|b| {
            let db = byte_to_db(b as u8, db_floor, db_ceil);
            if db.is_finite() {
                10.0f32.powf(db * 0.1)
            } else {
                0.0
            }
        });
        /* Positive finite floats order like their bit patterns, so the least
         * power reaching byte b is a bisection over those. */
        let edge = core::array::from_fn(|i| {
            let b = i as u8 + 1;
            let (mut lo, mut hi) = (0u32, f32::MAX.to_bits());
            while hi - lo > 1 {
                let mid = lo + (hi - lo) / 2;
                if encode(f32::from_bits(mid)) >= b {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            f32::from_bits(hi)
        });
        Self { power, edge }
    }

    /// The byte a power encodes as.
    #[inline]
    pub fn to_byte(&self, power: f32) -> u8 {
        self.edge.partition_point(|&e| e <= power) as u8
    }

    /// Add several channels' columns into one, in power.
    ///
    /// `out` is filled for as many bands as the shortest input has and zeroed
    /// past it, the same contract `clash_column` keeps -- a caller never draws
    /// a stale tail.
    pub fn sum_column(&self, srcs: &[&[u8]], out: &mut [u8]) {
        let n = srcs
            .iter()
            .map(|s| s.len())
            .min()
            .unwrap_or(0)
            .min(out.len());

        for (i, o) in out[..n].iter_mut().enumerate() {
            /* Silence is power 0 and adds nothing -- see byte_to_db. */
            let power: f32 = srcs.iter().map(|s| self.power[s[i] as usize]).sum();
            *o = self.to_byte(power);
        }
        out[n..].fill(0);
    }
}

/// A dB DIFFERENCE in byte units -- for the balance window, which is a span
/// rather than a level and so has no floor to subtract.
pub fn db_span_to_byte(db: f32, db_floor: f32, db_ceil: f32) -> u8 {
    let span = (db_ceil - db_floor).max(1.0);
    ((db.max(0.0) / span) * 255.0 + 0.5).min(255.0) as u8
}

#[cfg(test)]
mod tests {
    use super::*;

    const FLOOR: f32 = -96.0;
    const CEIL: f32 = 0.0;

    fn b(db: f32) -> u8 {
        db_to_byte(db, FLOOR, CEIL)
    }

    #[test]
    fn a_level_survives_the_trip_into_bytes_and_back() {
        assert_eq!(b(0.0), 255, "full scale is not the ceiling");
        assert_eq!(b(-96.0), 0, "the floor is not zero");
        assert_eq!(b(-200.0), 0, "below the floor did not clamp");
        assert_eq!(b(50.0), 255, "above the ceiling did not clamp");
        /* Halfway up the scale, within a byte's worth of rounding. */
        assert!((b(-48.0) as i16 - 128).abs() <= 1, "{} is not halfway", b(-48.0));
    }

    #[test]
    fn a_byte_round_trips_through_the_decibel_it_stands_for() {
        for db in [-80.0f32, -48.0, -12.0, 0.0] {
            let back = byte_to_db(db_to_byte(db, FLOOR, CEIL), FLOOR, CEIL);
            assert!((back - db).abs() < 0.5, "{db} came back as {back}");
        }

        /*
         * THE FLOOR IS THE ONE LEVEL THAT CANNOT ROUND-TRIP, and that is the
         * design rather than a rounding loss: byte 0 means "at or below the
         * floor", so -96 and silence and -200 are all the same byte and there
         * is nothing to come back to. It returns -infinity instead of
         * pretending, which is exactly what lets sum_column add four silent
         * channels and still get silence.
         */
        assert_eq!(db_to_byte(FLOOR, FLOOR, CEIL), 0);
        assert_eq!(byte_to_db(0, FLOOR, CEIL), f32::NEG_INFINITY);
        assert!(byte_to_db(1, FLOOR, CEIL).is_finite());
    }

    #[test]
    fn two_equal_sources_sum_to_three_decibels_more() {
        /*
         * THE NUMBER THAT SAYS THE ARITHMETIC IS RIGHT. Two uncorrelated
         * sources of equal level measure +3 dB together, not +6 (that is
         * amplitude addition, which assumes they are phase locked) and not
         * double the byte (that is adding decibels, which multiplies them).
         */
        let a = [db_to_byte(-20.0, FLOOR, CEIL); 4];
        let mut out = [0u8; 4];
        PowerTable::new(FLOOR, CEIL).sum_column(&[&a, &a], &mut out);

        let got = byte_to_db(out[0], FLOOR, CEIL);
        assert!((got - -17.0).abs() < 0.6, "two -20 dB sources summed to {got}, wanted -17");

        /* And four of them are +6 over one. */
        PowerTable::new(FLOOR, CEIL).sum_column(&[&a, &a, &a, &a], &mut out);
        let four = byte_to_db(out[0], FLOOR, CEIL);
        assert!((four - -14.0).abs() < 0.6, "four -20 dB sources summed to {four}");
    }

    #[test]
    fn silence_adds_nothing_at_all() {
        let quiet = [0u8; 4];
        let loud = [db_to_byte(-24.0, FLOOR, CEIL); 4];
        let mut out = [0u8; 4];

        PowerTable::new(FLOOR, CEIL).sum_column(&[&loud, &quiet], &mut out);
        assert_eq!(out[0], loud[0], "silence moved a source that was already there");

        /*
         * AND FOUR SILENCES ARE STILL SILENCE. This is what byte_to_db's
         * -infinity is for: if byte 0 were treated as the floor instead, four
         * of them would sum to 6 dB above it and the picture would lift off
         * black for no reason.
         */
        PowerTable::new(FLOOR, CEIL).sum_column(&[&quiet, &quiet, &quiet, &quiet], &mut out);
        assert_eq!(out, [0, 0, 0, 0], "silence summed to something");
    }

    #[test]
    fn a_sum_clamps_rather_than_wrapping() {
        let hot = [255u8; 3];
        let mut out = [0u8; 3];
        PowerTable::new(FLOOR, CEIL).sum_column(&[&hot, &hot, &hot, &hot], &mut out);
        assert_eq!(out, [255, 255, 255], "a sum past the ceiling did not clamp");
    }

    #[test]
    fn a_sum_of_one_is_that_one_and_of_none_is_silence() {
        let a = [10u8, 90, 200];
        let mut out = [7u8; 3];
        PowerTable::new(FLOOR, CEIL).sum_column(&[&a], &mut out);
        for i in 0..3 {
            assert!((out[i] as i16 - a[i] as i16).abs() <= 1, "one source changed: {:?}", out);
        }
        let mut empty = [7u8; 3];
        PowerTable::new(FLOOR, CEIL).sum_column(&[], &mut empty);
        assert_eq!(empty, [0, 0, 0], "no sources is silence, and the tail is cleared");
    }

    #[test]
    fn a_sum_leaves_no_stale_tail() {
        let a = [200u8, 200];
        let b = [200u8, 200, 200];
        let mut out = [7u8; 5];
        PowerTable::new(FLOOR, CEIL).sum_column(&[&a, &b], &mut out);
        assert!(out[0] > 200 && out[1] > 200);
        assert_eq!(&out[2..], &[0, 0, 0], "past the shortest input was left stale");
    }

    #[test]
    fn a_clash_needs_both_sources_present() {
        let floor = b(-60.0);
        let bal = db_span_to_byte(12.0, FLOOR, CEIL);

        /* Two sources meeting at -20 dB: the thing being looked for. */
        assert!(clash_cell(b(-20.0), b(-20.0), floor, bal) > 0);

        /* One loud, one under the floor: not a clash, whatever a product says. */
        assert_eq!(clash_cell(b(0.0), b(-80.0), floor, bal), 0);
        assert_eq!(clash_cell(b(-80.0), b(0.0), floor, bal), 0, "and it is symmetric");

        /* Both under the floor: silence does not fight silence. */
        assert_eq!(clash_cell(b(-70.0), b(-70.0), floor, bal), 0);
    }

    #[test]
    fn a_source_that_simply_wins_is_not_clashing() {
        let floor = b(-60.0);
        let bal = db_span_to_byte(12.0, FLOOR, CEIL);

        /* Both well above the floor, but 30 dB apart -- the quiet one is
         * masked, not competing. This is the case a product conflates with a
         * real clash, and the whole reason the balance gate exists. */
        assert_eq!(clash_cell(b(0.0), b(-30.0), floor, bal), 0);

        /* Inside the window it counts, and more the closer they are. */
        let near = clash_cell(b(-20.0), b(-22.0), floor, bal);
        let far = clash_cell(b(-20.0), b(-30.0), floor, bal);
        assert!(near > 0 && far > 0);
        assert!(near > far, "a closer match did not read stronger: {near} vs {far}");
    }

    #[test]
    fn strength_rises_with_level() {
        let floor = b(-60.0);
        let bal = db_span_to_byte(12.0, FLOOR, CEIL);
        let quiet = clash_cell(b(-50.0), b(-50.0), floor, bal);
        let mid = clash_cell(b(-30.0), b(-30.0), floor, bal);
        let loud = clash_cell(b(-6.0), b(-6.0), floor, bal);
        assert!(quiet < mid && mid < loud, "{quiet} {mid} {loud}");
        assert!(loud > 200, "two sources at -6 dB should read strongly, got {loud}");
    }

    #[test]
    fn the_edges_of_the_settings_do_not_divide_by_zero() {
        /* balance 0 means "only exact ties". */
        assert!(clash_cell(200, 200, 10, 0) > 0);
        assert_eq!(clash_cell(200, 201, 10, 0), 0);
        /* floor 255 means "only full scale". */
        assert!(clash_cell(255, 255, 255, 255) > 0);
    }

    #[test]
    fn a_column_is_the_cell_applied_across_it_and_nothing_stale_is_left() {
        let floor = b(-60.0);
        let bal = db_span_to_byte(12.0, FLOOR, CEIL);
        let a = [b(-20.0), b(-20.0), b(-80.0)];
        let bb = [b(-20.0), b(0.0), b(-80.0)];
        let mut out = [7u8; 5];
        clash_column(&a, &bb, &mut out, floor, bal);

        assert_eq!(out[0], clash_cell(a[0], bb[0], floor, bal));
        assert_eq!(out[1], 0, "30 dB apart is not a clash");
        assert_eq!(out[2], 0, "both under the floor");
        /* Past the shortest input, and the 7s must be gone. */
        assert_eq!(&out[3..], &[0, 0], "a stale tail was left to be drawn");
    }


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
    fn power_reads_the_byte_its_amplitude_does() {
        assert_eq!(power_to_byte(0.0, -96.0, 0.0), 0);
        assert_eq!(power_to_byte(f32::NAN, -96.0, 0.0), 0);
        let mut a = 1e-6f32;
        while a < 4.0 {
            let (p, q) = (power_to_byte(a * a, -96.0, 0.0), amplitude_to_byte(a, -96.0, 0.0));
            assert!((p as i16 - q as i16).abs() <= 1, "{a}: {p} vs {q}");
            a *= 1.01;
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
