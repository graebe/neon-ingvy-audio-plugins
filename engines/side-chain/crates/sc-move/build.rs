// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * `chain_params`, WRITTEN FROM THE ENGINE'S SCHEMA (ni_params::chain). This
 * says how the grid draws NI Side-Chain -- each knob's name, short name,
 * step, unit and envelope grouping, in grid order -- and declares the
 * controls only the Move has: the sweep, the shape page and the web editor's
 * feed. Sidechain is cut from Source, and Threshold and Lockout are absent,
 * because the chain host hands over no aux input. src/params.rs says why
 * each is what it is.
 */

use ni_params::chain::{self, Entry, Knob, Wire};
use ni_params::host::text;
use sc_core::params::Param;
use sc_core::rates::RATES;
use sc_core::schema::spec;

fn knob(p: Param, wire: Wire) -> Knob {
    Knob::new(spec(p).key, text(spec(p).host.name), wire)
}

/// Number `p` in the engine's units, stepping by `step`, shown in `unit`.
fn number(p: Param, step: f64, unit: &str) -> Wire {
    let s = spec(p);
    Wire::Float { min: s.min, max: s.max, default: s.default, step, unit: unit.to_string() }
}

/// Choice `p` as `options`, by index.
fn options(p: Param, options: impl Iterator<Item = String>) -> Wire {
    Wire::Index { options: options.collect(), default: spec(p).default as usize }
}

/// Choice `p` in the schema's words, by index.
fn words(p: Param) -> Wire {
    options(p, spec(p).host.options.iter().map(|w| text(w).to_string()))
}

/// A note's name in Live's octave numbering, where 36 is C1: what the
/// plugin's menu says (plugins/side-chain/Params.cpp), and what the engine
/// reads back (sc_core::midi::note_from_name).
fn note_name(n: usize) -> String {
    const PITCH: [&str; 12] = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
    format!("{}{}", PITCH[n % 12], n as i32 / 12 - 2)
}

fn main() {
    let stage = |p, short, viz| knob(p, number(p, 0.1, "%")).short(short).viz(viz);
    let knobs = [
        knob(Param::Source, words(Param::Source).first(2)),
        knob(Param::Rate, options(Param::Rate, RATES.iter().map(|r| r.label.to_string()))),
        knob(Param::Depth, number(Param::Depth, 0.01, "%")),
        stage(Param::Delay, "Dly", r#"{"group":"duck","role":"delay","kind":"envelope"}"#),
        stage(Param::Attack, "Att", r#"{"group":"duck","role":"attack"}"#),
        stage(Param::Hold, "Hold", r#"{"group":"duck","role":"hold"}"#),
        stage(Param::Release, "Rel", r#"{"group":"duck","role":"release"}"#),
        knob(Param::Curve, words(Param::Curve)),
        knob(Param::TimeMode, words(Param::TimeMode)),
        knob(Param::Channel, words(Param::Channel)).short("Ch"),
        knob(Param::Note, options(Param::Note, (0..=spec(Param::Note).max as usize).map(note_name))),
        knob(Param::MidiMode, words(Param::MidiMode)),
        knob(Param::VelSens, number(Param::VelSens, 0.01, "%")).named("Vel Sens").short("Vel"),
    ];
    let mut entries: Vec<Entry> = knobs.into_iter().map(Entry::Knob).collect();
    entries.extend([
        Entry::Raw(r#"{"key":"sweep","name":"Sweep","type":"string","access":"read","live":true}"#),
        Entry::Raw(r#"{"key":"shape","name":"Shape","type":"canvas","as_page":true,"show_value":false,"extra_keys":["ui","rate"]}"#),
        /* The web editor's feed, last (src/params.rs says why). */
        Entry::Raw(r#"{"key":"web_ui","name":"Web UI","type":"string","access":"read","extra_keys":["ui","params","stage_ms"]}"#),
    ]);
    chain::write(&entries);
}
