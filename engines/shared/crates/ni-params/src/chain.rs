// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
`chain_params`, written from an engine's table: what a Move module's build.rs
calls, so the knob grid's ranges, defaults, options and units are the
engine's own.

Schwung's knob grid reads one JSON array to draw a module's controls. Each
module used to keep it as a literal, which agreed with the engine by care.
Now a module's build script lists its knobs in grid order -- each one taken
from the engine's [`Kind`](crate::Kind) and unit -- and says only what the
Move does differently, where everyone can see it: a shorter name, a narrower
range, a default the module sets up, a frequency offered as a dozen named
steps because a Schwung float turns linearly. Controls that exist only on the
Move are given as raw JSON ([`Entry::Raw`]).

BUILD TIME ONLY. It allocates freely, and nothing in a plugin or a module
calls it. Trance Gate keeps its own generator, which reads its schema's host
column (tg-move/build.rs).
*/

use std::fmt::Write;

use crate::Kind;

/// How a knob travels on the wire.
#[derive(Clone, Debug, PartialEq)]
pub enum Wire {
    /// A number, in the engine's units.
    Float { min: f64, max: f64, default: f64, step: f64, unit: String },
    /// One of `options`, sent as its index.
    Index { options: Vec<String>, default: usize },
    /// One of `options`, sent as its label, which the engine reads.
    Name { options: Vec<String>, default: String },
}

impl Wire {
    /// A number knob on `kind`'s range and default; `kind` is a
    /// [`Kind::Float`].
    pub fn float(kind: Kind, step: f64, unit: &str) -> Wire {
        let Kind::Float { min, max, default, .. } = kind else { panic!("{kind:?} is no number") };
        Wire::Float { min, max, default, step, unit: unit.to_string() }
    }

    /// `kind`'s labels, sent by index; `kind` is a [`Kind::Choice`].
    pub fn index(kind: Kind) -> Wire {
        let Kind::Choice { labels, default, .. } = kind else { panic!("{kind:?} is no choice") };
        Wire::Index { options: strings(labels), default }
    }

    /// `kind`'s labels, sent by name; `kind` is a [`Kind::Choice`].
    pub fn names(kind: Kind) -> Wire {
        let Kind::Choice { labels, default, .. } = kind else { panic!("{kind:?} is no choice") };
        Wire::Name { options: strings(labels), default: labels[default].to_string() }
    }

    /// The Move's own steps for a number, sent by name; the default is the
    /// option the engine reads (`parse`) as `default`, and there must be one.
    pub fn steps(options: &[&str], default: f64, parse: impl Fn(&str) -> Option<f64>) -> Wire {
        let at = options.iter().find(|o| parse(o) == Some(default));
        let default = at.unwrap_or_else(|| panic!("no option of {options:?} reads as {default}"));
        Wire::Name { options: strings(options), default: default.to_string() }
    }

    /// The Move's own steps for a number, starting at `default`: one of them.
    pub fn steps_from(options: &[&str], default: &str) -> Wire {
        assert!(options.contains(&default), "{default} is not one of {options:?}");
        Wire::Name { options: strings(options), default: default.to_string() }
    }

    /// The same number on a narrower range than the engine's, which must hold
    /// it.
    pub fn within(self, lo: f64, hi: f64) -> Wire {
        let Wire::Float { min, max, default, step, unit } = self else { panic!("only a number has a range") };
        assert!(min <= lo && lo < hi && hi <= max, "{lo}..{hi} is not inside {min}..{max}");
        Wire::Float { min: lo, max: hi, default: default.clamp(lo, hi), step, unit }
    }

    /// The same knob starting elsewhere: a number at `v`, a choice at index
    /// `v`. The module sets the engine there (its lib.rs).
    pub fn starting(self, v: f64) -> Wire {
        match self {
            Wire::Float { min, max, step, unit, .. } => {
                assert!((min..=max).contains(&v), "{v} is not inside {min}..{max}");
                Wire::Float { min, max, default: v, step, unit }
            }
            Wire::Index { options, .. } => {
                assert!((v as usize) < options.len(), "option {v} of {options:?}");
                Wire::Index { options, default: v as usize }
            }
            Wire::Name { .. } => panic!("a named knob starts at a name: Wire::steps_from"),
        }
    }

    /// The first `n` options only: what the Move can do of the engine's.
    pub fn first(self, n: usize) -> Wire {
        let Wire::Index { mut options, default } = self else { panic!("only an index knob is cut") };
        assert!(n > default && n <= options.len());
        options.truncate(n);
        Wire::Index { options, default }
    }
}

/// One of the engine's parameters, as the grid draws it.
#[derive(Clone, Debug)]
pub struct Knob {
    pub key: String,
    pub name: String,
    pub short: Option<String>,
    pub wire: Wire,
    /// The `viz` object, verbatim.
    pub viz: Option<String>,
}

impl Knob {
    pub fn new(key: &str, name: &str, wire: Wire) -> Knob {
        Knob { key: key.to_string(), name: name.to_string(), short: None, wire, viz: None }
    }

    /// The grid's own name for it.
    pub fn named(self, name: &str) -> Knob {
        Knob { name: name.to_string(), ..self }
    }

    pub fn short(self, short: &str) -> Knob {
        Knob { short: Some(short.to_string()), ..self }
    }

    pub fn viz(self, viz: &str) -> Knob {
        Knob { viz: Some(viz.to_string()), ..self }
    }
}

/// One entry of the array, in grid order.
pub enum Entry {
    Knob(Knob),
    /// A Move-only declaration, as JSON.
    Raw(&'static str),
}

/// The whole array.
pub fn json(entries: &[Entry]) -> String {
    let mut j = String::from("[");
    for (i, e) in entries.iter().enumerate() {
        if i > 0 {
            j.push(',');
        }
        match e {
            Entry::Knob(k) => knob(&mut j, k),
            Entry::Raw(r) => j.push_str(r),
        }
    }
    j.push(']');
    j
}

/// `entries` as OUT_DIR/chain_params.json, which the module's params.rs
/// includes.
pub fn write(entries: &[Entry]) {
    let out = std::path::Path::new(&std::env::var("OUT_DIR").expect("run from a build script")).join("chain_params.json");
    std::fs::write(out, json(entries)).expect("OUT_DIR is writable");
    println!("cargo:rerun-if-changed=build.rs");
}

fn knob(j: &mut String, k: &Knob) {
    let _ = write!(j, r#"{{"key":{},"name":{}"#, quoted(&k.key), quoted(&k.name));
    if let Some(short) = &k.short {
        let _ = write!(j, r#","short_name":{}"#, quoted(short));
    }
    match &k.wire {
        Wire::Float { min, max, default, step, unit } => {
            let _ = write!(j, r#","type":"float","min":{min},"max":{max},"default":{default},"step":{step},"unit":{}"#, quoted(unit));
        }
        Wire::Index { options, default } => {
            let _ = write!(j, r#","type":"enum","options":{},"wire_format":"index","default":"{default}""#, list(options));
        }
        Wire::Name { options, default } => {
            let _ = write!(j, r#","type":"enum","options":{},"wire_format":"name","default":{}"#, list(options), quoted(default));
        }
    }
    if let Some(viz) = &k.viz {
        let _ = write!(j, r#","viz":{viz}"#);
    }
    j.push('}');
}

fn strings(labels: &[&str]) -> Vec<String> {
    labels.iter().map(|l| l.to_string()).collect()
}

fn quoted(s: &str) -> String {
    format!("\"{}\"", s.replace('\\', "\\\\").replace('"', "\\\""))
}

fn list(options: &[String]) -> String {
    let items: Vec<String> = options.iter().map(|o| quoted(o)).collect();
    format!("[{}]", items.join(","))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::Travel;

    const LEVEL: Kind = Kind::Float { min: -24.0, max: 24.0, default: 0.0, resolution: 0.01, travel: Travel::Linear, bipolar: true };
    const SLOPE: Kind = Kind::Choice { labels: &["12 dB", "24 dB"], default: 1, automatable: true };

    #[test]
    fn each_wire_is_written_in_the_grids_field_order() {
        let entries = [
            Entry::Knob(Knob::new("output", "Output", Wire::float(LEVEL, 0.5, "dB")).short("Out")),
            Entry::Knob(Knob::new("slope", "Slope", Wire::index(SLOPE))),
            Entry::Knob(Knob::new("slope", "Slope", Wire::names(SLOPE)).viz(r#"{"group":"g"}"#)),
            Entry::Raw(r#"{"key":"feed","type":"string"}"#),
        ];
        assert_eq!(
            json(&entries),
            concat!(
                r#"[{"key":"output","name":"Output","short_name":"Out","type":"float","min":-24,"max":24,"default":0,"step":0.5,"unit":"dB"},"#,
                r#"{"key":"slope","name":"Slope","type":"enum","options":["12 dB","24 dB"],"wire_format":"index","default":"1"},"#,
                r#"{"key":"slope","name":"Slope","type":"enum","options":["12 dB","24 dB"],"wire_format":"name","default":"24 dB","viz":{"group":"g"}},"#,
                r#"{"key":"feed","type":"string"}]"#
            )
        );
    }

    #[test]
    fn the_moves_differences_are_held_to_the_engine() {
        assert_eq!(
            Wire::float(LEVEL, 1.0, "dB").within(-12.0, 6.0),
            Wire::Float { min: -12.0, max: 6.0, default: 0.0, step: 1.0, unit: "dB".into() }
        );
        assert_eq!(Wire::index(SLOPE).starting(0.0), Wire::Index { options: vec!["12 dB".into(), "24 dB".into()], default: 0 });
        assert_eq!(Wire::index(SLOPE).first(2), Wire::index(SLOPE));
        let steps = Wire::steps(&["1k", "2k"], 2000.0, |o| o.strip_suffix('k').and_then(|n| n.parse::<f64>().ok()).map(|n| n * 1000.0));
        assert_eq!(steps, Wire::Name { options: vec!["1k".into(), "2k".into()], default: "2k".into() });
    }

    #[test]
    #[should_panic(expected = "is not inside")]
    fn a_range_wider_than_the_engines_is_refused() {
        let _ = Wire::float(LEVEL, 1.0, "dB").within(-30.0, 6.0);
    }

    #[test]
    #[should_panic(expected = "reads as")]
    fn steps_without_the_engines_default_are_refused() {
        let _ = Wire::steps(&["1k"], 2000.0, |_| Some(1000.0));
    }

    #[test]
    fn a_quote_in_a_label_stays_inside_its_string() {
        assert_eq!(quoted(r#"say "hi""#), r#""say \"hi\"""#);
    }
}
