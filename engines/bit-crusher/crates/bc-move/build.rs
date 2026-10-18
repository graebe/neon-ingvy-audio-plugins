// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * `chain_params`, WRITTEN FROM THE ENGINE'S TABLE (ni_params::chain). This
 * says only how the grid draws NI Bit-Crusher -- the order of its two pages
 * and each knob's step -- and offers Rate and Cutoff as the Move's own steps
 * (src/steps.rs), sent by name, because a Schwung float turns linearly.
 * src/params.rs says why the module is what it is.
 */

use bc_core::{Kind, Param, Unit};
use ni_params::chain::{self, Entry, Knob, Wire};

#[path = "src/steps.rs"]
mod steps;

/// Number `p` as the engine declares it, stepping by `step`.
fn number(p: Param, step: f64) -> Entry {
    let i = p.info();
    Entry::Knob(Knob::new(i.key, i.name, Wire::float(i.kind, step, i.unit.map_or("", Unit::label))))
}

/// Choice `p`'s labels, sent as `wire` makes them.
fn choice(p: Param, wire: fn(Kind) -> Wire) -> Entry {
    let i = p.info();
    Entry::Knob(Knob::new(i.key, i.name, wire(i.kind)))
}

/// Number `p` offered as `options`, which the engine reads by name.
fn steps(p: Param, options: &[&str]) -> Entry {
    let i = p.info();
    Entry::Knob(Knob::new(i.key, i.name, Wire::steps(options, p.default_value(), |o| p.parse_value(o))))
}

fn main() {
    chain::write(&[
        /* Page 1: the crusher. */
        number(Param::Bits, 0.1),
        steps(Param::Rate, &steps::RATE_LABELS),
        number(Param::Drive, 0.5),
        number(Param::Shape, 1.0),
        number(Param::Jitter, 1.0),
        number(Param::Dither, 1.0),
        number(Param::Mix, 1.0),
        number(Param::Output, 0.5),
        /* Page 2: the filter, off as in the plugin; Slope sends its word. */
        choice(Param::Filter, Wire::index),
        steps(Param::Cutoff, &steps::CUTOFF_LABELS),
        number(Param::Peak, 0.5),
        choice(Param::Slope, Wire::names),
    ]);
    println!("cargo:rerun-if-changed=src/steps.rs");
}
