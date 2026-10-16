// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * `chain_params`, WRITTEN FROM THE ENGINE'S TABLE (ni_params::chain). This
 * says only how the grid draws NI Vocalize -- its one page, in order, and
 * each knob's step. src/params.rs says why the module is what it is.
 */

use ni_params::chain::{self, Entry, Knob, Wire};
use vz_core::{Param, Unit};

/// Number `p` as the engine declares it, stepping by `step`.
fn number(p: Param, step: f64) -> Entry {
    let i = p.info();
    Entry::Knob(Knob::new(i.key, i.name, Wire::float(i.kind, step, i.unit.map_or("", Unit::label))))
}

fn main() {
    chain::write(&[
        number(Param::Vocal, 1.0),
        /* 0 A, 1 E, 2 I, 3 O, 4 U, and the way between. */
        number(Param::Vowel, 0.01),
        number(Param::Size, 1.0),
        number(Param::Resonance, 1.0),
        number(Param::Excite, 1.0),
        number(Param::Mix, 1.0),
        number(Param::Output, 0.5),
    ]);
}
