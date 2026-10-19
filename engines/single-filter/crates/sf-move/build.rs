// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * EACH PRODUCT'S `chain_params`, WRITTEN FROM ITS TABLE (sf_core::params,
 * through ni_params::chain) into OUT_DIR as chain_params_<product>.json.
 * The knobs are the product's parameters in its order, but for two: the
 * Analyzer, a view the Move has no screen for, is left out; and Matched, a
 * cut's design switch, goes last, so Output sits on the first page with the
 * filter. Every way the Move differs from the plugin shows here: Freq and Q
 * as the Move's own steps (src/steps.rs), sent by name.
 */

use ni_filter_node::words::Unit;
use ni_params::chain::{self, Entry, Knob, Wire};
use sf_core::params::{self, Field};
use sf_core::{Shape, SHAPES};

#[path = "src/steps.rs"]
mod steps;

fn key_of(shape: Shape) -> &'static str {
    match shape {
        Shape::HighPass => "high-pass",
        Shape::LowPass => "low-pass",
        Shape::BandPass => "band-pass",
        Shape::Notch => "notch",
        Shape::Bell => "bell",
        Shape::LowShelf => "low-shelf",
        Shape::HighShelf => "high-shelf",
        Shape::Tilt => "tilt",
        Shape::AllPass => "all-pass",
        Shape::Empty => unreachable!("no product"),
    }
}

fn knob(shape: Shape, f: Field) -> Entry {
    let i = params::info(shape, f);
    let default = i.kind.default_value();
    let parse = move |o: &str| params::parse_value(shape, f, o);
    let wire = match f {
        Field::Freq => Wire::steps(&steps::FREQ, default, parse),
        Field::Q if matches!(shape, Shape::Bell | Shape::BandPass | Shape::Notch) => Wire::steps(&steps::Q_WIDTH, default, parse),
        Field::Q => Wire::steps(&steps::Q_SLOPE, default, parse),
        Field::Gain | Field::Output => Wire::float(i.kind, 0.5, Unit::Db.label()).starting(default),
        Field::Ripple => Wire::float(i.kind, 0.05, Unit::Db.label()).starting(default),
        Field::Stopband => Wire::float(i.kind, 1.0, Unit::Db.label()).starting(default),
        _ => Wire::index(i.kind).starting(default),
    };
    Entry::Knob(Knob::new(i.key, i.name, wire))
}

fn main() {
    let out = std::path::PathBuf::from(std::env::var_os("OUT_DIR").expect("OUT_DIR"));
    for shape in SHAPES {
        let mut fields: Vec<Field> = params::layout(shape).iter().copied().filter(|&f| f != Field::Analyzer && f != Field::Matched).collect();
        if params::index_of(shape, Field::Matched).is_some() {
            fields.push(Field::Matched);
        }
        let entries: Vec<Entry> = fields.iter().map(|&f| knob(shape, f)).collect();
        std::fs::write(out.join(format!("chain_params_{}.json", key_of(shape))), chain::json(&entries)).expect("chain_params");
    }
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed=src/steps.rs");
}
