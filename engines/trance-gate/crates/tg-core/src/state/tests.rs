// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The state blob: what a load may and may not leave behind.

use crate::Instance;

fn get(p: &Instance, k: &str) -> String {
    let mut buf = [0u8; 8192];
    let n = p.get_param(k, &mut buf);
    String::from_utf8(buf[..n as usize].to_vec()).unwrap()
}

#[test]
fn loading_a_shorter_pattern_brings_the_cursor_back_onto_the_ring() {
    /*
     * THE CURSOR IS GLOBAL AND NOT SAVED; THE LENGTH IS PER SLOT AND IS. A
     * patch whose current slot is shorter than where the cursor stood left it
     * past the end -- every edit then landed on a step the ring never draws,
     * and `step_order` read a rank from outside the pattern. `slot` and
     * `length` already re-clamp it; a load has to as well.
     */
    let mut p = Instance::new(44100.0);
    let mut short = Instance::new(44100.0);
    short.set_param("length", "7"); /* 8 steps */
    let blob = get(&short, "state");

    p.set_param("length", "63");
    p.set_param("cursor", "40");
    assert_eq!(get(&p, "cursor"), "40");
    p.set_param("state", &blob);
    assert_eq!(get(&p, "length"), "7");
    assert_eq!(get(&p, "cursor"), "7", "the cursor was left past the end");
}

#[test]
fn a_state_round_trips() {
    let mut a = Instance::new(44100.0);
    a.set_param("slot", "3");
    a.set_param("length", "23");
    a.set_param("rate", "1/32");
    a.set_param("pattern", "F0F0F1");
    a.set_param("cursor", "4");
    a.set_param("step_amount", "0.5");
    a.set_param("randomize", "77");
    a.set_param("amount", "0.625");
    let blob = get(&a, "state");
    let mut b = Instance::new(44100.0);
    b.set_param("state", &blob);
    assert_eq!(get(&b, "state"), blob);
    assert_eq!(get(&b, "params"), get(&a, "params"));
}
