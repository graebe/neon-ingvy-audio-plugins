/*!
The cycle table: how often the duck fires.

Beats per cycle, with `1/4 == 1 beat`. The triplet values match the host's LFO
table exactly, so a rate reads the same here as it does on an LFO -- and the
same as it does in the Trance Gate, whose `rates.rs` this is adapted from.

WHY THIS TABLE IS NOT `tg-core`'s, AND WHERE IT DIFFERS.

`tg-core` excludes bar-length rates on the stated grounds that "a bar-long step
is not a gate", and for a step sequencer that is right. A DUCKER at one duck
per bar is a different and entirely real thing -- it is the long swell under a
build -- so `1/1` is here and it is the first entry.

That shifts every index against the Trance Gate's table. The two are separate
products with separate state, so nothing reads across; what matters is that
`RATE_DEFAULT` below is an index into THIS table.

THE LABEL IS THE WIRE VALUE, NOT THE INDEX, for the string door. An index on
the wire would couple this table's order to the shell's option list, and a
drift between them is not a visible error -- it is the duck firing at the wrong
subdivision with the right word on screen.
*/

pub struct Rate {
    pub label: &'static str,
    pub beats: f64,
}

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

pub fn index_from(val: &str) -> usize {
    if val.is_empty() {
        return RATE_DEFAULT;
    }
    for (i, r) in RATES.iter().enumerate() {
        if val == r.label {
            return i;
        }
    }
    /* A bare number is an index -- the host resolves a numeric enum value that
     * way. Parsed with the same leniency strtol has: leading digits, trailing
     * anything, so "4" and "4 " and "4/x" all land on 4. */
    let digits: String = val
        .trim_start()
        .chars()
        .take_while(|c| c.is_ascii_digit() || *c == '-' || *c == '+')
        .collect();
    if let Ok(n) = digits.parse::<i64>() {
        if n >= 0 && (n as usize) < RATES.len() {
            return n as usize;
        }
    }
    RATE_DEFAULT
}
