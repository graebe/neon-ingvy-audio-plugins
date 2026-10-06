// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
A rate table: labelled lengths in beats, with `1/4 == 1 beat`.

Each product keeps its own LIST -- the order is its wire and its saved state,
so an entry is only ever appended. The parsing is shared.

THE LABEL IS THE WIRE VALUE, NOT THE INDEX. An index on the wire would couple a
table's order to the host's option list, and a drift between them is not a
visible error -- it is the right word on screen at the wrong subdivision.
*/

pub struct Rate {
    pub label: &'static str,
    pub beats: f64,
}

/// A label, or a bare number as an index (how a host resolves a numeric enum,
/// and what an older state blob may carry), or `default`. The number is read
/// with strtol's leniency -- leading space, trailing junk -- from a slice of
/// the input, never a collected String: this runs on the audio callback.
pub fn index_from(table: &[Rate], default: usize, val: &str) -> usize {
    if val.is_empty() {
        return default;
    }
    for (i, r) in table.iter().enumerate() {
        if val == r.label {
            return i;
        }
    }
    let s = val.trim_start();
    let run = s
        .bytes()
        .take_while(|c| c.is_ascii_digit() || *c == b'-' || *c == b'+')
        .count();
    if let Ok(n) = s[..run].parse::<i64>() {
        if n >= 0 && (n as usize) < table.len() {
            return n as usize;
        }
    }
    default
}

#[cfg(test)]
mod tests {
    use super::*;

    static TABLE: &[Rate] = &[
        Rate { label: "1/2", beats: 2.0 },
        Rate { label: "1/4", beats: 1.0 },
        Rate { label: "1/8", beats: 0.5 },
        Rate { label: "1/16", beats: 0.25 },
        Rate { label: "1/32", beats: 0.125 },
    ];
    const DEFAULT: usize = 1;

    #[test]
    fn a_label_an_index_or_the_default() {
        let at = |v: &str| index_from(TABLE, DEFAULT, v);
        for (i, r) in TABLE.iter().enumerate() {
            assert_eq!(at(r.label), i);
            assert_eq!(at(&i.to_string()), i);
        }
        /* strtol's leniency: leading space, trailing junk. */
        assert_eq!(at(" 3"), 3);
        assert_eq!(at("3/x"), 3);
        assert_eq!(at("+2"), 2);
        /* Out of range, malformed or absent is the default, not the nearest end. */
        for v in ["", "-1", "99", "+-3", "-", "junk"] {
            assert_eq!(at(v), DEFAULT, "{v:?}");
        }
    }
}
