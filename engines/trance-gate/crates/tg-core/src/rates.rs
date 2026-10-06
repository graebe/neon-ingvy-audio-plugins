/*!
The rate table: beats per step. The parsing is `ni_dsp::rate`'s; the list is
this product's, and its order is the wire.

`rate` is declared to the host as type "rate", whose option list the host
GENERATES from include_bars / include_triplets -- so the label, not the index,
is what is reported. The triplets match the host's LFO table. Bars are
excluded: a bar-long step is not a gate.
*/

pub use ni_dsp::rate::Rate;

pub static RATES: &[Rate] = &[
    Rate { label: "1/1T",  beats: 8.0 / 3.0 },
    Rate { label: "1/2",   beats: 2.0 },
    Rate { label: "1/2T",  beats: 4.0 / 3.0 },
    Rate { label: "1/4",   beats: 1.0 },
    Rate { label: "1/4T",  beats: 2.0 / 3.0 },
    Rate { label: "1/8",   beats: 0.5 },
    Rate { label: "1/8T",  beats: 1.0 / 3.0 },
    Rate { label: "1/16",  beats: 0.25 },   // index 7 -- the default
    Rate { label: "1/16T", beats: 1.0 / 6.0 },
    Rate { label: "1/32",  beats: 0.125 },
    Rate { label: "1/32T", beats: 1.0 / 12.0 },
    Rate { label: "1/64",  beats: 0.0625 },
    /* APPENDED, never inserted: RATE_DEFAULT is an index into this table and
     * so is the numeric form a state blob may carry, so putting 1/128
     * anywhere but the end would silently re-point every saved patch. */
    Rate { label: "1/128", beats: 0.03125 },
];

pub const RATE_DEFAULT: usize = 7;

/// A label, a bare number as an index, or [`RATE_DEFAULT`].
pub fn index_from(val: &str) -> usize {
    ni_dsp::rate::index_from(RATES, RATE_DEFAULT, val)
}

/*
 * THE BAR, AND THE LENGTHS THAT FILL ONE.
 *
 * A pattern that is a whole number of bars -- or half of one -- is the length
 * a user is reaching for nine times out of ten, and which numbers those are
 * depends on the Rate and the host's meter: 32 at 1/32 in 4/4, 24 at 1/16T,
 * 12 at 1/16 in 3/4. The editor's Length control holds on them (its
 * "detents"), and it is told them rather than working them out, so this table
 * stays the only place that knows what a step is worth.
 */

/// The host's time signature. Anything a host could not mean -- a numerator
/// of 0, a denominator that is not a power of two -- is 4/4, which is also
/// what a host that says nothing gets.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Meter {
    num: u8,
    den: u8,
}

impl Meter {
    pub const COMMON: Meter = Meter { num: 4, den: 4 };

    pub fn new(num: i32, den: i32) -> Meter {
        let num_ok = (1..=99).contains(&num);
        let den_ok = matches!(den, 1 | 2 | 4 | 8 | 16 | 32);
        if num_ok && den_ok {
            Meter { num: num as u8, den: den as u8 }
        } else {
            Meter::COMMON
        }
    }

    pub fn num(self) -> u32 {
        self.num as u32
    }

    pub fn den(self) -> u32 {
        self.den as u32
    }

    /// One bar, in quarter notes: the unit [`Rate::beats`] is in.
    pub fn bar_beats(self) -> f64 {
        self.num as f64 * 4.0 / self.den as f64
    }
}

impl Default for Meter {
    fn default() -> Self {
        Meter::COMMON
    }
}

/// Steps in one bar of `meter` at rate `rate_idx` (clamped to the table). Not
/// necessarily whole: 1/2 in 3/4 is a step and a half.
pub fn steps_per_bar(rate_idx: usize, meter: Meter) -> f64 {
    let rate = &RATES[rate_idx.min(RATES.len() - 1)];
    meter.bar_beats() / rate.beats
}

/// The bar multiples a detent is offered at: half a bar, one, two and four.
pub const DETENT_BARS: [f64; 4] = [0.5, 1.0, 2.0, 4.0];

/// The pattern lengths that are [`DETENT_BARS`] at this rate and meter --
/// those that are a whole number of steps within `1..=max`, ascending. A fixed
/// array and a count: this is formatted on the audio thread.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub struct Detents {
    len: usize,
    steps: [u16; DETENT_BARS.len()],
}

impl Detents {
    pub fn as_slice(&self) -> &[u16] {
        &self.steps[..self.len]
    }
}

pub fn detents(rate_idx: usize, meter: Meter, max: usize) -> Detents {
    let per_bar = steps_per_bar(rate_idx, meter);
    let mut out = Detents::default();
    for bars in DETENT_BARS {
        let steps = per_bar * bars;
        let whole = steps.round();
        /* The table's beats are thirds and powers of two, so a length that is
         * whole comes out within rounding of it -- never within 1e-6 of a
         * length that is not. */
        if (steps - whole).abs() < 1e-6 && whole >= 1.0 && whole <= max as f64 {
            out.steps[out.len] = whole as u16;
            out.len += 1;
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_label_an_index_or_the_default() {
        for (i, r) in RATES.iter().enumerate() {
            assert_eq!(index_from(r.label), i);
            assert_eq!(index_from(&i.to_string()), i);
        }
        /* strtol's leniency: leading space, trailing junk. */
        assert_eq!(index_from(" 3"), 3);
        assert_eq!(index_from("3/x"), 3);
        assert_eq!(index_from("+2"), 2);
        /* Out of range, malformed or absent is the default, not the nearest end. */
        for v in ["", "-1", "99", "+-3", "-", "junk"] {
            assert_eq!(index_from(v), RATE_DEFAULT, "{v:?}");
        }
    }

    fn at(label: &str, num: i32, den: i32) -> Vec<u16> {
        detents(index_from(label), Meter::new(num, den), crate::MAX_STEPS).as_slice().to_vec()
    }

    #[test]
    fn steps_per_bar_is_the_bar_over_the_step() {
        let common = Meter::COMMON;
        for (label, want) in [("1/1T", 1.5), ("1/2", 2.0), ("1/2T", 3.0), ("1/4", 4.0),
                              ("1/4T", 6.0), ("1/8", 8.0), ("1/8T", 12.0), ("1/16", 16.0),
                              ("1/16T", 24.0), ("1/32", 32.0), ("1/32T", 48.0),
                              ("1/64", 64.0), ("1/128", 128.0)] {
            let got = steps_per_bar(index_from(label), common);
            assert!((got - want).abs() < 1e-9, "{label}: {got}");
        }
        assert!((steps_per_bar(index_from("1/16"), Meter::new(3, 4)) - 12.0).abs() < 1e-9);
        assert!((steps_per_bar(index_from("1/16"), Meter::new(6, 8)) - 12.0).abs() < 1e-9);
        assert!((steps_per_bar(index_from("1/16"), Meter::new(7, 8)) - 14.0).abs() < 1e-9);
        /* Past the table is its last entry, not a panic. */
        assert_eq!(steps_per_bar(99, common), 128.0);
    }

    /*
     * EVERY RATE, IN FOUR METERS: half a bar, one, two and four, kept when
     * whole and within 1..=128. The 4/4 column is also what the editor's review
     * harness (plugins/trance-gate/ui/test/harness/mock.js) answers with.
     */
    #[test]
    fn the_detents_for_every_rate_and_meter() {
        let table: &[(&str, [&[u16]; 4])] = &[
            /*            4/4                    3/4                  6/8                  7/8 */
            ("1/1T",  [&[3, 6],              &[],                 &[],                 &[]]),
            ("1/2",   [&[1, 2, 4, 8],        &[3, 6],             &[3, 6],             &[7]]),
            ("1/2T",  [&[3, 6, 12],          &[9],                &[9],                &[]]),
            ("1/4",   [&[2, 4, 8, 16],       &[3, 6, 12],         &[3, 6, 12],         &[7, 14]]),
            ("1/4T",  [&[3, 6, 12, 24],      &[9, 18],            &[9, 18],            &[21]]),
            ("1/8",   [&[4, 8, 16, 32],      &[3, 6, 12, 24],     &[3, 6, 12, 24],     &[7, 14, 28]]),
            ("1/8T",  [&[6, 12, 24, 48],     &[9, 18, 36],        &[9, 18, 36],        &[21, 42]]),
            ("1/16",  [&[8, 16, 32, 64],     &[6, 12, 24, 48],    &[6, 12, 24, 48],    &[7, 14, 28, 56]]),
            ("1/16T", [&[12, 24, 48, 96],    &[9, 18, 36, 72],    &[9, 18, 36, 72],    &[21, 42, 84]]),
            ("1/32",  [&[16, 32, 64, 128],   &[12, 24, 48, 96],   &[12, 24, 48, 96],   &[14, 28, 56, 112]]),
            ("1/32T", [&[24, 48, 96],        &[18, 36, 72],       &[18, 36, 72],       &[21, 42, 84]]),
            ("1/64",  [&[32, 64, 128],       &[24, 48, 96],       &[24, 48, 96],       &[28, 56, 112]]),
            ("1/128", [&[64, 128],           &[48, 96],           &[48, 96],           &[56, 112]]),
        ];
        let meters = [(4, 4), (3, 4), (6, 8), (7, 8)];
        assert_eq!(table.len(), RATES.len(), "every rate is in the table");
        for (label, cols) in table {
            for (m, want) in meters.iter().zip(cols.iter()) {
                assert_eq!(at(label, m.0, m.1), want.to_vec(), "{label} in {}/{}", m.0, m.1);
            }
        }
    }

    #[test]
    fn a_meter_no_host_could_mean_is_common_time() {
        for (num, den) in [(0, 4), (-3, 4), (100, 4), (4, 0), (4, 3), (4, 64), (4, -4)] {
            assert_eq!(Meter::new(num, den), Meter::COMMON, "{num}/{den}");
        }
        assert_eq!(Meter::default(), Meter::COMMON);
        let m = Meter::new(7, 8);
        assert_eq!((m.num(), m.den(), m.bar_beats()), (7, 8, 3.5));
    }

    #[test]
    fn a_shorter_maximum_drops_the_long_ones() {
        assert_eq!(detents(index_from("1/32"), Meter::COMMON, 64).as_slice(), &[16, 32, 64]);
        assert_eq!(detents(index_from("1/32"), Meter::COMMON, 0).as_slice(), &[] as &[u16]);
    }
}
