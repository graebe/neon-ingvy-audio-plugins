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

#[test]
fn the_version_decides_the_two_readings_that_turn_on_it() {
    use super::Version;
    assert_eq!(Version::of(0), Version::Unversioned);
    assert_eq!(Version::of(-3), Version::Unversioned);
    assert_eq!(Version::of(4), Version::V4);
    assert_eq!(Version::of(9), Version::Newer(9));
    let ms: Vec<i32> = (0..10).filter(|&v| Version::of(v).stages_in_ms()).collect();
    assert_eq!(ms, [1, 2, 3], "v1 to v3 wrote milliseconds");
    let own: Vec<i32> = (0..10).filter(|&v| Version::of(v).slot_sounds()).collect();
    assert_eq!(own, [7, 8, 9], "a slot's own sound is read from v7 on");
}

#[test]
fn a_text_that_is_no_blob_loads_nothing() {
    /* The reader that searched a text for keys would load whatever it could
     * pick out of a broken one -- half a patch. A text that is not one JSON
     * object is now no patch at all. */
    let mut p = Instance::new(44100.0);
    p.set_param("amount", "0.3");
    let before = get(&p, "state");
    for text in ["", "{\"sv\":7,\"amount\":1.0", "{\"amount\":1.0} and more", "amount:1.0", "[{\"amount\":1.0}]"] {
        assert_eq!(super::Patch::parse(text), Err(super::NotAPatch), "{text}");
        p.set_param("state", text);
        assert_eq!(get(&p, "state"), before, "{text}");
        assert_eq!(super::version(text), 0, "{text}");
    }
}

#[test]
fn the_first_of_a_key_wins_and_a_key_no_version_wrote_is_passed_over() {
    /* As the reader that found a key by searching for it always did. */
    let mut p = Instance::new(44100.0);
    p.set_param("state", "{\"sv\":7,\"amount\":0.25,\"amount\":0.75,\"stopped\":1,\"future\":{\"x\":[1,2]}}");
    assert_eq!(get(&p, "amount"), "0.25");
    assert_eq!(super::version("{\"x\":{\"sv\":2},\"sv\":5,\"sv\":6}"), 5);
}

#[test]
fn a_read_blob_is_the_same_patch_wherever_it_is_loaded() {
    /* What the main thread reads, the audio thread applies: one parse, any
     * number of loads, each the same as the text door's. */
    let mut a = Instance::new(44100.0);
    a.set_param("slot", "2");
    a.set_param("length", "40");
    a.set_param("randomize", "5");
    a.set_param("release", "77.25");
    let blob = get(&a, "state");
    let patch = super::Patch::parse(&blob).unwrap();
    assert_eq!(patch.version(), super::Version::V7);
    let (mut b, mut c) = (Instance::new(44100.0), Instance::new(44100.0));
    b.load(&patch);
    c.set_param("state", &blob);
    assert_eq!(get(&b, "state"), blob);
    assert_eq!(get(&c, "state"), blob);
    let rev = b.state_rev();
    b.load(&patch);
    assert_ne!(b.state_rev(), rev, "a load moves the revision");
}
