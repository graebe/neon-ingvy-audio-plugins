// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * `chain_params`, WRITTEN FROM THE ENGINE'S SCHEMA.
 *
 * Schwung's knob grid reads one JSON array to draw a module's controls. It used
 * to be a literal captured from the C shell's output, and it agreed with the
 * engine by care -- Width from 0.05 here, from 0 in the engine. Now every
 * automatable parameter's range, default and options come from
 * `tg_core::schema::PARAMS`, and this file says only how the grid DRAWS each one
 * (its label, short label, knob step, envelope grouping) and declares the
 * controls that exist only on the Move: the step under the cursor, its level,
 * the randomiser, the read-only feeds and the ring page.
 *
 * THE ORDER IS THE GRID'S. The four envelope keys stay contiguous: a viz group
 * whose members do not land on one row of the 2x4 page is dropped whole. The
 * fade's three come after `amount`, so nothing new pushes a control off the
 * ring page's first eight. `randomize` is an enum with a "Hold" because
 * released Schwung has no write-only type, and it writes the LABEL so "Roll"
 * walks the engine's generator rather than pinning seed 1.
 */

use std::fmt::Write;
use tg_core::params::Param;
use tg_core::rates::RATES;
use tg_core::schema::{spec, text, HostKind};

/// How the grid draws one of the engine's parameters.
struct Knob {
    param: Param,
    name: &'static str,
    short: Option<&'static str>,
    /// A continuous knob's step and unit.
    step: f64,
    unit: &'static str,
    /// The `viz` object, verbatim, for the envelope group.
    viz: Option<&'static str>,
}

const fn knob(param: Param, name: &'static str, short: Option<&'static str>) -> Knob {
    Knob { param, name, short, step: 0.0, unit: "", viz: None }
}

enum Entry {
    Knob(Knob),
    /// A Move-only declaration, as JSON.
    Raw(&'static str),
}

fn options(json: &mut String, items: impl Iterator<Item = String>) {
    json.push('[');
    for (i, o) in items.enumerate() {
        let _ = write!(json, "{}\"{o}\"", if i > 0 { "," } else { "" });
    }
    json.push(']');
}

fn knob_json(k: &Knob) -> String {
    let s = spec(k.param);
    let mut j = format!(r#"{{"key":"{}","name":"{}""#, s.key, k.name);
    if let Some(short) = k.short {
        let _ = write!(j, r#","short_name":"{short}""#);
    }
    match (k.param, s.host.kind) {
        /* The rate travels as its LABEL, the one value the engine reads either
         * way and the one a person can read in a patch. */
        (Param::Rate, _) => {
            j.push_str(r#","type":"enum","options":"#);
            options(&mut j, RATES.iter().map(|r| r.label.to_string()));
            let _ = write!(j, r#","default":"{}""#, RATES[s.default as usize].label);
        }
        /* Everything else discrete travels as its option INDEX. */
        (_, HostKind::Integer) => {
            j.push_str(r#","type":"enum","wire_format":"index","options":"#);
            options(&mut j, (s.host_min() as i64..=s.host_max() as i64).map(|n| n.to_string()));
            let _ = write!(j, r#","default":"{}""#, s.default as i64);
        }
        (_, HostKind::Choice | HostKind::Toggle) => {
            j.push_str(r#","type":"enum","wire_format":"index","options":"#);
            options(&mut j, s.host.options.iter().map(|o| text(o).to_string()));
            let _ = write!(j, r#","default":"{}""#, s.default as i64);
        }
        (_, HostKind::Continuous) => {
            let _ = write!(
                j,
                r#","type":"float","min":{},"max":{},"default":{},"step":{},"unit":"{}""#,
                s.min, s.max, s.default, k.step, k.unit
            );
        }
    }
    if let Some(viz) = k.viz {
        let _ = write!(j, r#","viz":{viz}"#);
    }
    j.push('}');
    j
}

/* The 1..128 options a cursor and a length both offer. */
const STEP_OPTIONS: &str = r#"["1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16","17","18","19","20","21","22","23","24","25","26","27","28","29","30","31","32","33","34","35","36","37","38","39","40","41","42","43","44","45","46","47","48","49","50","51","52","53","54","55","56","57","58","59","60","61","62","63","64","65","66","67","68","69","70","71","72","73","74","75","76","77","78","79","80","81","82","83","84","85","86","87","88","89","90","91","92","93","94","95","96","97","98","99","100","101","102","103","104","105","106","107","108","109","110","111","112","113","114","115","116","117","118","119","120","121","122","123","124","125","126","127","128"]"#;

fn main() {
    let percent = |param, name, short, step| Entry::Knob(Knob { step, unit: "%", ..knob(param, name, short) });
    let stage = |param, name, viz| Entry::Knob(Knob { step: 0.1, unit: "%", viz: Some(viz), ..knob(param, name, None) });
    let entries = [
        Entry::Knob(knob(Param::Slot, "Slot", None)),
        Entry::Raw(r#"{"key":"step_amount","name":"Step Amount","short_name":"Step","type":"float","min":0,"max":1,"default":1,"step":0.01,"unit":"%"}"#),
        Entry::Knob(knob(Param::Length, "Len", None)),
        Entry::Knob(knob(Param::Rate, "Rate", None)),
        Entry::Raw(concat!(r#"{"key":"cursor","name":"Step","type":"enum","wire_format":"index","default":"0","options":"#, "CURSOR", "}")),
        Entry::Raw(r#"{"key":"step","name":"Gate","short_name":"Gate","type":"enum","options":["Off","On","Tie"],"default":"Off"}"#),
        Entry::Knob(knob(Param::Legato, "Join Neighbors", Some("Join"))),
        Entry::Knob(knob(Param::TimeMode, "Env Time", Some("Time"))),
        Entry::Knob(knob(Param::Curve, "Env Curve", Some("Curve"))),
        stage(Param::Attack, "Att", r#"{"group":"adsr","role":"attack","kind":"envelope"}"#),
        stage(Param::Decay, "Dec", r#"{"group":"adsr","role":"decay"}"#),
        Entry::Knob(Knob { step: 0.01, unit: "%", viz: Some(r#"{"group":"adsr","role":"sustain"}"#), ..knob(Param::Sustain, "Sus", None) }),
        stage(Param::Release, "Rel", r#"{"group":"adsr","role":"release"}"#),
        percent(Param::Hold, "Width", Some("Width"), 0.01),
        percent(Param::Amount, "All Amount", Some("All"), 0.01),
        percent(Param::Fade, "Fade In", Some("Fade"), 0.01),
        Entry::Knob(knob(Param::FadeSoft, "Fade Shape", Some("Shape"))),
        Entry::Knob(knob(Param::FadeDir, "Fade Dir", Some("Dir"))),
        Entry::Raw(r#"{"key":"randomize","name":"Random","short_name":"Rnd","type":"enum","options":["Hold","Roll"],"default":"Hold"}"#),
        Entry::Raw(r#"{"key":"pattern","name":"Pat","type":"string","access":"read"}"#),
        Entry::Raw(r#"{"key":"ties","name":"Ties","type":"string","access":"read"}"#),
        Entry::Raw(r#"{"key":"phase","name":"Phase","type":"string","access":"read","live":true}"#),
        Entry::Raw(r#"{"key":"gate","name":"Gate","type":"canvas","as_page":true,"show_value":false,"extra_keys":["ui","rate"]}"#),
        /* The web editor's feed, last (src/params.rs says why). */
        Entry::Raw(r#"{"key":"web_ui","name":"Web UI","type":"string","access":"read","extra_keys":["ui","params","state"]}"#),
    ];
    let mut json = String::from("[");
    for (i, e) in entries.iter().enumerate() {
        if i > 0 {
            json.push(',');
        }
        match e {
            Entry::Knob(k) => json.push_str(&knob_json(k)),
            Entry::Raw(r) => json.push_str(&r.replace("CURSOR", STEP_OPTIONS)),
        }
    }
    json.push(']');
    let out = std::path::Path::new(&std::env::var("OUT_DIR").unwrap()).join("chain_params.json");
    std::fs::write(out, json).unwrap();
    println!("cargo:rerun-if-changed=build.rs");
}
