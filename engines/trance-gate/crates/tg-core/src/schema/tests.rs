// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The schema against everything that obeys it: the engine's doors, the
//! saved blob, and what the iPlug2 build told hosts.

use super::*;
use crate::state::Patch;
use crate::Instance;

#[test]
fn the_table_is_in_abi_order() {
    for (i, s) in PARAMS.iter().enumerate() {
        assert_eq!(s.param as usize, i, "{}", s.key);
        assert_eq!(Param::from_i32(i as i32), Some(s.param));
        assert!(s.min <= s.default && s.default <= s.max, "{}", s.key);
    }
}

#[test]
fn a_fresh_engine_starts_at_the_defaults() {
    let v = Instance::new(48_000.0).numbers();
    for s in &PARAMS {
        assert_eq!(v[s.param as usize] as f32, s.default as f32, "{}", s.key);
    }
}

/*
 * The values that CLAMP: every continuous one, and Length. The discrete ones
 * refuse an out-of-range value instead -- a Slot of 9 changes nothing, a Rate
 * of 40 is the default, a Curve of 7 is Linear -- because a wrong option
 * silently moving to its neighbour is worse than nothing moving
 * (params/tests.rs holds those).
 */
#[test]
fn the_engine_holds_every_value_to_the_tables_range() {
    for s in PARAMS.iter().filter(|s| s.host.kind == HostKind::Continuous || s.param == Param::Length) {
        for (value, want) in [(1e9, s.max), (-1e9, s.min)] {
            let mut p = Instance::new(48_000.0);
            p.set_num(s.param, value);
            assert_eq!(p.numbers()[s.param as usize] as f32, want as f32, "{} <- {value}", s.key);
        }
    }
}

#[test]
fn every_key_moves_its_parameter_through_the_text_door() {
    for s in &PARAMS {
        let mut p = Instance::new(48_000.0);
        let to = if s.default == s.max { s.min } else { s.max };
        /* Rate's text is its label; everything else reads a number. */
        let text = if s.param == Param::Rate { rates::RATES[to as usize].label.to_string() } else { format!("{to}") };
        p.set_param(s.key, &text);
        assert_eq!(p.numbers()[s.param as usize] as f32, to as f32, "{} <- {text}", s.key);
        let mut buf = [0u8; 64];
        assert!(p.get_param(s.key, &mut buf) > 0, "{} has no readout", s.key);
    }
}

#[test]
fn every_blob_key_is_read_back_by_a_load() {
    for s in PARAMS.iter().filter(|s| s.blob.is_some() && s.param != Param::Slot) {
        let key = s.blob.unwrap();
        let to = if s.default == s.max { s.min } else { s.max };
        let blob = format!(r#"{{"sv":7,"{key}":{to},"p0":"1:0:16"}}"#);
        let patch = Patch::parse(&blob).unwrap_or_else(|_| panic!("{blob}"));
        let mut p = Instance::new(48_000.0);
        p.load(&patch);
        assert_eq!(p.numbers()[s.param as usize] as f32, to as f32, "{key}");
    }
}

/*
 * THE HOST COLUMNS ARE THE iPLUG2 BUILD'S. A project stores plain host values
 * by index and a host shows these names, so they are frozen: what that build
 * reported is in tests/fixtures/iplug2/NITranceGate/parameters.json, its
 * Bypass first and the fifteen after it.
 */
#[test]
fn the_host_columns_are_what_the_iplug2_build_reported() {
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/../../../../tests/fixtures/iplug2/NITranceGate/parameters.json");
    let fixture: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(path).unwrap()).unwrap();
    let reported = fixture["parameters"].as_array().unwrap();
    assert_eq!(reported.len(), 1 + PARAMS.len());
    for s in &PARAMS {
        let r = &reported[1 + s.param as usize];
        assert_eq!(r["id"].as_u64(), Some(s.param as u64));
        assert_eq!(r["title"].as_str(), Some(text(s.host.name)), "{}", s.key);
        assert_eq!(r["units"].as_str(), Some(text(s.host.units)), "{}", s.key);
        let (lo, hi) = (s.host_min(), s.host_max());
        let steps = match s.host.kind {
            HostKind::Continuous => 0.0,
            _ => hi - lo,
        };
        assert_eq!(r["stepCount"].as_f64(), Some(steps), "{}", s.key);
        let norm = (s.host_default() - lo) / (hi - lo);
        assert!((r["defaultNormalizedValue"].as_f64().unwrap() - norm).abs() < 1e-9, "{}", s.key);
        if !s.host.options.is_empty() {
            assert_eq!(s.host.options.len() as f64, hi - lo + 1.0, "{}", s.key);
        }
    }
}
