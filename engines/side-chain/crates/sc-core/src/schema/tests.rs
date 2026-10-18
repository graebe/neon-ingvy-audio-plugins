// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The schema against everything that obeys it: the engine's doors and what
//! the iPlug2 build told hosts.

use super::*;
use crate::shape::Curve;
use crate::Instance;
use ni_params::host::text;

#[test]
fn the_table_is_in_abi_order_with_one_spelling_each() {
    for (i, s) in PARAMS.iter().enumerate() {
        assert_eq!(s.param as usize, i, "{}", s.key);
        assert_eq!(Param::from_i32(i as i32), Some(s.param));
        assert_eq!(Param::from_key(s.key), Some(s.param));
        assert!(s.min <= s.default && s.default <= s.max, "{}", s.key);
        for t in &PARAMS[i + 1..] {
            assert_ne!(s.key, t.key);
            assert_ne!(s.host.id, t.host.id);
        }
    }
}

#[test]
fn a_fresh_engine_starts_at_the_defaults() {
    let e = Instance::new(48_000.0);
    for s in &PARAMS {
        assert!((e.num(s.param) - s.default).abs() < 1e-12, "{}: {}", s.key, e.num(s.param));
    }
    /* -24 dB as the amplitude it always started at, to the bit. */
    assert_eq!(e.threshold, 0.063_095_734_448_019_33);
}

/*
 * The values that CLAMP: every continuous one, and the two numbers a choice
 * stands for (Channel, Trigger). Rate, Source, Time, Curve and Mode refuse or
 * fall back instead (params/tests.rs holds those).
 */
#[test]
fn the_engine_holds_every_number_to_the_tables_range() {
    let numbers = PARAMS.iter().filter(|s| s.host.kind == HostKind::Continuous || matches!(s.param, Param::Channel | Param::Note));
    for s in numbers {
        for (value, want) in [(1e9, s.max), (-1e9, s.min)] {
            let mut e = Instance::new(48_000.0);
            e.set_num(s.param, value);
            assert!((e.num(s.param) - want).abs() < 1e-9, "{} <- {value}: {}", s.key, e.num(s.param));
        }
    }
}

#[test]
fn every_key_moves_its_parameter_through_the_text_door() {
    for s in &PARAMS {
        let mut e = Instance::new(48_000.0);
        let to = if s.default == s.max { s.min } else { s.max };
        /* A choice's text is its word, Rate's its label; a number reads as one. */
        let text = match s.host.options.get(to as usize) {
            Some(word) => text(word).to_string(),
            None if s.param == Param::Rate => rates::RATES[to as usize].label.to_string(),
            None => format!("{to}"),
        };
        assert!(e.set_param(s.key, &text));
        assert!((e.num(s.param) - to).abs() < 1e-9, "{} <- {text}", s.key);
        let mut buf = [0u8; 64];
        assert!(e.get_param(s.key, &mut buf) > 0, "{} has no readout", s.key);
    }
}

#[test]
fn the_curves_words_are_the_shapes_own() {
    let words: Vec<&str> = spec(Param::Curve).host.options.iter().map(|w| text(w)).collect();
    assert_eq!(words, Curve::LABELS);
}

/*
 * THE HOST COLUMNS ARE THE iPLUG2 BUILD'S. A project stores plain host values
 * by index and a host shows these names, so they are frozen: what that build
 * reported is in tests/fixtures/iplug2/NISideChain/parameters.json, its Bypass
 * first and the fifteen after it.
 */
#[test]
fn the_host_columns_are_what_the_iplug2_build_reported() {
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/../../../../tests/fixtures/iplug2/NISideChain/parameters.json");
    let fixture: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(path).unwrap()).unwrap();
    let reported = fixture["parameters"].as_array().unwrap();
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
