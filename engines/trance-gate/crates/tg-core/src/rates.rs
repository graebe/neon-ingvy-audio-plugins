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
