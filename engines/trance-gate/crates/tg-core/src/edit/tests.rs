// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! A key and a value, read once and applied anywhere.

use super::Edit;
use crate::params::Param;
use crate::Instance;

fn get(p: &Instance, k: &str) -> String {
    let mut buf = [0u8; 8192];
    let n = p.get_param(k, &mut buf);
    String::from_utf8(buf[..n as usize].to_vec()).unwrap()
}

/* Every key the string door serves, in each spelling it takes. */
const EDITS: &[(&str, &str)] = &[
    ("slot", "3"), ("length", "40"), ("rate", "1/32T"), ("rate", "4"), ("rate", "-1"),
    ("attack", "12.5"), ("decay", "40"), ("sustain", "0.4"), ("hold", "0.6"), ("release", "30"),
    ("amount", "0.8"), ("fade", "0.7"), ("fade_soft", "On"), ("fade_soft", "0"),
    ("fade_dir", "Out"), ("legato", "on"), ("curve", "S-Curve"), ("curve", "Exp"), ("curve", "7"),
    ("time_mode", "%"), ("time_mode", "Step"), ("cursor", "9"), ("step", "Tie"), ("step", "On"),
    ("step", "0"), ("step", "5"), ("step_amount", "0.5"), ("step_amount", "3"), ("step_order", "2"),
    ("randomize", "77"), ("randomize", "Roll"), ("randomize", ""), ("pattern", "F0F0F1"),
    ("ties", "0x0100"), ("cursor", "500"), ("slot", "0"), ("step_order", "0"),
];

#[test]
fn a_read_edit_lands_exactly_as_the_string_door_does() {
    /* One engine takes each pair through set_param, the other reads it first
     * and applies the result -- the shell's way. Every readout agrees after
     * every step, so the two doors cannot have drifted. */
    let mut door = Instance::new(48000.0);
    let mut read = Instance::new(48000.0);
    for &(k, v) in EDITS {
        door.set_param(k, v);
        let edit = Edit::parse(k, v).unwrap_or_else(|| panic!("{k} is an edit"));
        read.apply_edit(&edit);
        for key in ["state", "params", "ui", "cursor", "step", "step_amount", "step_order"] {
            assert_eq!(get(&read, key), get(&door, key), "{key} after {k}={v}");
        }
    }
}

#[test]
fn each_key_is_read_as_its_wire_says() {
    use Edit::*;
    assert_eq!(Edit::parse("slot", "3"), Some(Num(Param::Slot, 3.0)));
    assert_eq!(Edit::parse("rate", "1/16"), Some(Num(Param::Rate, crate::rates::index_from("1/16") as f64)));
    assert_eq!(Edit::parse("curve", "S-Curve"), Some(Num(Param::Curve, 2.0)));
    assert_eq!(Edit::parse("time_mode", "%"), Some(Num(Param::TimeMode, 1.0)));
    assert_eq!(Edit::parse("fade_dir", "out"), Some(Num(Param::FadeDir, 1.0)));
    assert_eq!(Edit::parse("cursor", "-3"), Some(Cursor(0)), "an index is never negative");
    assert_eq!(Edit::parse("step", "Tie"), Some(Step(2)));
    assert_eq!(Edit::parse("step", "9"), Some(Step(2)), "three states, clamped");
    assert_eq!(Edit::parse("step_amount", "1.5"), Some(StepAmount(1.0)));
    assert_eq!(Edit::parse("step_order", "0"), Some(StepOrder(1)), "ranks start at 1");
    assert_eq!(Edit::parse("randomize", "42"), Some(Randomize(Some(42))), "a seed pins the roll");
    assert_eq!(Edit::parse("randomize", ""), Some(Randomize(None)), "a press walks the generator");
}

#[test]
fn what_is_not_an_edit_reads_as_none() {
    for hold in ["Hold", "hold", "0", "Off", "off"] {
        assert_eq!(Edit::parse("randomize", hold), None, "{hold} rolls nothing");
    }
    assert_eq!(Edit::parse("state", "{\"sv\":7}"), None, "a patch has its own reader");
    assert_eq!(Edit::parse("phase", "0.5"), None, "a readout is not an edit");
    assert_eq!(Edit::parse("", "1"), None);
}

#[test]
fn an_edit_moves_the_revision_and_a_non_edit_does_not() {
    let mut p = Instance::new(44100.0);
    let rev = p.state_rev();
    p.apply_edit(&Edit::Cursor(3));
    assert_ne!(p.state_rev(), rev);
    let rev = p.state_rev();
    p.set_param("no-such-key", "1");
    p.set_param("randomize", "Hold");
    assert_eq!(p.state_rev(), rev, "nothing the blob carries can have changed");
}
