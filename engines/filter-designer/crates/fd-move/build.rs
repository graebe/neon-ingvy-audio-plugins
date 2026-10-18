// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * `chain_params`, WRITTEN FROM THE ENGINE'S TABLE (ni_params::chain). This
 * says how the grid draws NI Filter Designer -- two pages, the grid's names
 * and each knob's step -- and every way the Move differs from the plugin:
 * the frequencies as the Move's own steps (src/steps.rs), sent by name; the
 * two Q knobs on a narrower range; and the defaults the module starts at
 * (src/setup.rs). src/params.rs says why the module is what it is.
 */

use fd_core::{Field, Param, Unit};
use ni_params::chain::{self, Entry, Knob, Wire};

#[path = "src/setup.rs"]
mod setup;
#[path = "src/steps.rs"]
mod steps;

/// Where the module starts `p`: its set-up, or the engine's default.
fn start(p: Param) -> f64 {
    setup::SET_UP.iter().find(|(q, _)| *q == p).map_or(p.default_value(), |&(_, v)| v)
}

fn knob(p: Param, name: &str, wire: Wire) -> Entry {
    Entry::Knob(Knob::new(p.info().key, name, wire))
}

/// Number `p` as the engine declares it, stepping by `step`.
fn number(p: Param, step: f64) -> Wire {
    let i = p.info();
    Wire::float(i.kind, step, i.unit.map_or("", Unit::label)).starting(start(p))
}

/// Choice `p`'s labels, by index.
fn choice(p: Param) -> Wire {
    Wire::index(p.info().kind).starting(start(p))
}

/// Frequency `p` as the Move's steps, which the engine reads by name.
fn frequency(p: Param) -> Wire {
    let key = p.info().key;
    let (_, options) = steps::FREQUENCY_OPTIONS.iter().find(|(k, _)| *k == key).expect("a frequency knob");
    Wire::steps(options, start(p), |o| p.parse_value(o))
}

fn main() {
    let (hp, lp) = (|f| Param::Node(0, f), |f| Param::Node(1, f));
    chain::write(&[
        /* Page 1: the isolator. */
        knob(Param::IsoLow, "Low", number(Param::IsoLow, 0.5)),
        knob(Param::IsoMid, "Mid", number(Param::IsoMid, 0.5)),
        knob(Param::IsoHigh, "High", number(Param::IsoHigh, 0.5)),
        knob(Param::IsoFreqLo, "Low-Mid", frequency(Param::IsoFreqLo)),
        knob(Param::IsoFreqHi, "Mid-High", frequency(Param::IsoFreqHi)),
        knob(Param::IsoSlope, "Slope", choice(Param::IsoSlope)),
        knob(Param::IsoOn, "Isolator", choice(Param::IsoOn)),
        knob(Param::Output, "Output", number(Param::Output, 0.5)),
        /* Page 2: the two cuts. A Q past 8 rings, and below 0.5 sags: the
         * useful range of a cut on a device without a curve to look at. */
        knob(hp(Field::On), "HP On", choice(hp(Field::On))),
        knob(hp(Field::Freq), "HP Freq", frequency(hp(Field::Freq))),
        knob(hp(Field::Order), "HP Slope", choice(hp(Field::Order))),
        knob(hp(Field::Q), "HP Q", number(hp(Field::Q), 0.05).within(0.5, 8.0)),
        knob(lp(Field::On), "LP On", choice(lp(Field::On))),
        knob(lp(Field::Freq), "LP Freq", frequency(lp(Field::Freq))),
        knob(lp(Field::Order), "LP Slope", choice(lp(Field::Order))),
        knob(lp(Field::Q), "LP Q", number(lp(Field::Q), 0.05).within(0.5, 8.0)),
    ]);
    println!("cargo:rerun-if-changed=src/setup.rs");
    println!("cargo:rerun-if-changed=src/steps.rs");
}
