/*!
The rate table.

THE LABEL IS THE WIRE VALUE, NOT THE INDEX. `rate` is declared to the host as
type "rate", whose option list the host GENERATES from include_bars /
include_triplets. Reporting an index would couple this table's order to that
generator's, and a drift between them is not a visible error -- it is the gate
running at the wrong subdivision with the right word on screen.

Beats per step, with 1/4 == 1 beat. The triplet values match the host's LFO
table exactly so a rate reads the same here as it does on an LFO. Bars are
excluded: a bar-long step is not a gate.
*/

pub struct Rate {
    pub label: &'static str,
    pub beats: f64,
}

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

pub fn index_from(val: &str) -> usize {
    if val.is_empty() {
        return RATE_DEFAULT;
    }
    for (i, r) in RATES.iter().enumerate() {
        if val == r.label {
            return i;
        }
    }
    /* A bare number is an index -- the host resolves a numeric enum value
     * that way, and an older state blob may carry one. Parsed with the same
     * leniency strtol has: leading digits, trailing anything.
     *
     * A SLICE OF THE INPUT, NOT A COLLECTED STRING. This runs on the audio
     * callback (the Move's knob writes a numeric rate), and the String that
     * used to be built here was a malloc and a free per write. The run it
     * takes is the same one -- digits and signs -- so what parses is too. */
    let s = val.trim_start();
    let run = s
        .bytes()
        .take_while(|c| c.is_ascii_digit() || *c == b'-' || *c == b'+')
        .count();
    if let Ok(n) = s[..run].parse::<i64>() {
        if n >= 0 && (n as usize) < RATES.len() {
            return n as usize;
        }
    }
    RATE_DEFAULT
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
