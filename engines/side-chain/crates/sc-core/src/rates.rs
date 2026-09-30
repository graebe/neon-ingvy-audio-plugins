/*!
The cycle table: how often the duck fires, in beats per cycle. The parsing is
`ni_dsp::rate`'s; the list is this product's, and its order is the wire.

Unlike the Trance Gate's table, `1/1` is here and first: one duck per bar is
the long swell under a build. So the indices differ between the two products,
and `RATE_DEFAULT` is an index into THIS table.
*/

pub use ni_dsp::rate::Rate;

pub static RATES: &[Rate] = &[
    Rate { label: "1/1",   beats: 4.0 },
    Rate { label: "1/1T",  beats: 8.0 / 3.0 },
    Rate { label: "1/2",   beats: 2.0 },
    Rate { label: "1/2T",  beats: 4.0 / 3.0 },
    Rate { label: "1/4",   beats: 1.0 },      // index 4 -- the default
    Rate { label: "1/4T",  beats: 2.0 / 3.0 },
    Rate { label: "1/8",   beats: 0.5 },
    Rate { label: "1/8T",  beats: 1.0 / 3.0 },
    Rate { label: "1/16",  beats: 0.25 },
    Rate { label: "1/16T", beats: 1.0 / 6.0 },
    Rate { label: "1/32",  beats: 0.125 },
    /* APPENDED, never inserted: RATE_DEFAULT is an index into this table and
     * so is the numeric form a host or a saved state may carry, so putting a
     * new rate anywhere but the end would silently re-point every session. */
    Rate { label: "1/32T", beats: 1.0 / 12.0 },
];

/// `1/4`. The classic four-on-the-floor duck, and what a user who opens the
/// plugin over a house loop expects to hear without touching anything.
pub const RATE_DEFAULT: usize = 4;

/// A label, a bare number as an index, or [`RATE_DEFAULT`].
pub fn index_from(val: &str) -> usize {
    ni_dsp::rate::index_from(RATES, RATE_DEFAULT, val)
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
}
