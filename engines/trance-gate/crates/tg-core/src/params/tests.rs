// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The two parameter doors: clamps, NaN, and what the string door accepts.

use crate::params::Param;
use crate::Instance;

fn params(p: &Instance) -> String {
    let mut buf = [0u8; 512];
    let n = p.get_param("params", &mut buf);
    String::from_utf8(buf[..n as usize].to_vec()).unwrap()
}

#[test]
fn a_nan_is_dropped_at_the_door_for_every_parameter() {
    /*
     * NaN.clamp is NaN, and `NaN as i32` is 0: a NaN from a host either
     * poisoned the gain for good (Amount, Sustain) or silently moved a
     * DIFFERENT control to its first option (Slot, Rate, Length). Neither is
     * a value, so neither moves anything.
     */
    for i in 0..15 {
        let param = Param::from_i32(i).unwrap();
        let mut p = Instance::new(44100.0);
        p.set_num(Param::Slot, 3.0);
        p.set_num(Param::Rate, 5.0);
        p.set_num(Param::Amount, 0.5);
        let before = params(&p);
        p.set_num(param, f64::NAN);
        assert_eq!(params(&p), before, "{param:?} moved on a NaN");
    }
}

#[test]
fn out_of_range_is_clamped() {
    let mut p = Instance::new(44100.0);
    p.set_num(Param::Amount, 7.0);
    p.set_num(Param::Sustain, -2.0);
    p.set_num(Param::Attack, f64::INFINITY);
    let s = params(&p);
    let f: Vec<&str> = s.split(':').collect();
    assert_eq!(f[6], "1", "amount");
    assert_eq!(f[10], "0", "sustain");
    assert_eq!(f[8], "200", "attack");
}

/*
 * AN INFINITE LENGTH CLAMPS LIKE ANY OTHER. `value as i64` saturates +inf to
 * i64::MAX, and the `+ 1` that turns an option index into a length then
 * overflowed: an abort in a debug build, a one-step pattern in a release one.
 */
#[test]
fn an_infinite_or_huge_length_clamps_to_the_ends_of_the_range() {
    let length = |p: &Instance| {
        let mut buf = [0u8; 16];
        let n = p.get_param("length", &mut buf);
        String::from_utf8(buf[..n as usize].to_vec()).unwrap()
    };
    for (value, want) in [(f64::INFINITY, "127"), (1e30, "127"), (f64::NEG_INFINITY, "0"), (-1e30, "0"), (15.7, "15")] {
        let mut p = Instance::new(44100.0);
        p.set_num(Param::Length, value);
        assert_eq!(length(&p), want, "Length <- {value:e}");
    }
}

/*
 * THE LENGTH DETENTS RIDE ON THE `params` READOUT, appended as field 16: the
 * current slot's Rate in the host's meter, recomputed whenever either moves.
 */
#[test]
fn the_params_readout_ends_with_the_detents_of_the_rate_and_meter() {
    let detents = |p: &Instance| params(p).split(':').nth(16).map(str::to_owned);
    let mut p = Instance::new(44100.0);
    assert_eq!(params(&p).split(':').count(), 17);
    assert_eq!(detents(&p).as_deref(), Some("8,16,32,64"), "1/16 in 4/4 by default");
    p.set_param("rate", "1/32");
    assert_eq!(detents(&p).as_deref(), Some("16,32,64,128"));
    p.set_param("rate", "1/16T");
    assert_eq!(detents(&p).as_deref(), Some("12,24,48,96"));
    p.set_param("rate", "1/16");
    p.set_meter(3, 4);
    assert_eq!(detents(&p).as_deref(), Some("6,12,24,48"));
    p.set_param("rate", "1/1T");
    assert_eq!(detents(&p).as_deref(), Some(""), "none whole: an empty field, still there");
    /* No meter from the host is common time. */
    p.set_meter(0, 0);
    assert_eq!(p.meter(), crate::rates::Meter::COMMON);
    assert_eq!(detents(&p).as_deref(), Some("3,6"));
    /* Per slot, because the Rate is. */
    p.set_param("slot", "2");
    p.set_param("rate", "1/32");
    p.set_param("slot", "0");
    assert_eq!(detents(&p).as_deref(), Some("3,6"));
    p.set_param("slot", "2");
    assert_eq!(detents(&p).as_deref(), Some("16,32,64,128"));
}

#[test]
fn a_mirror_counts_the_detents_in_the_engines_meter() {
    let mut p = Instance::new(44100.0);
    p.set_meter(7, 8);
    let mut state = [0u8; 8192];
    let n = p.get_param("state", &mut state) as usize;
    let mut m = Instance::new(44100.0);
    m.mirror(core::str::from_utf8(&state[..n]).unwrap(), &p.playhead());
    assert_eq!(m.meter(), p.meter());
    assert_eq!(params(&m), params(&p));
}

/*
 * THE NAMED FIELDS ARE THE READOUTS' FIELDS: one name per field, in order, so
 * a reader that looks a field up by name finds what a reader that counts does.
 */
#[test]
fn the_field_names_count_the_readouts_fields() {
    use crate::params::{PARAMS_FIELDS, UI_FIELDS};
    let read = |p: &Instance, key: &str| {
        let mut buf = [0u8; 2048];
        let n = p.get_param(key, &mut buf);
        String::from_utf8(buf[..n as usize].to_vec()).unwrap()
    };
    let p = Instance::new(44100.0);
    assert_eq!(read(&p, "ui").split(':').count(), UI_FIELDS.len());
    assert_eq!(read(&p, "params").split(':').count(), PARAMS_FIELDS.len());
    assert_eq!(read(&p, "ui_fields"), UI_FIELDS.join(":"));
    assert_eq!(read(&p, "params_fields"), PARAMS_FIELDS.join(":"));
    /* Spot checks that the names are the right fields, not only enough. */
    let params = read(&p, "params");
    let at = |name: &str| PARAMS_FIELDS.iter().position(|n| *n == name).unwrap();
    assert_eq!(params.split(':').nth(at("rate")), Some("1/16"));
    assert_eq!(params.split(':').nth(at("hold")), Some("1"));
    assert_eq!(read(&p, "ui").split(':').nth(UI_FIELDS.iter().position(|n| *n == "length").unwrap()), Some("16"));
}

/*
 * `devms`: THE ENGINE'S OWN CLOCK, appended last. Exactly the frames it has
 * processed at its rate, never going back, carried across a rate change and
 * into a mirror -- and the readout at its longest (128 steps, every depth and
 * order set, the last slot) still answers whole: get_param refuses a text
 * that does not fit rather than cut it.
 */
#[test]
fn devms_is_the_time_the_engine_has_processed() {
    use crate::params::UI_FIELDS;
    let at = UI_FIELDS.iter().position(|n| *n == "devms").unwrap();
    assert_eq!(at, UI_FIELDS.len() - 1, "appended, never inserted");
    let devms = |p: &Instance| {
        let mut buf = [0u8; 2048];
        let n = p.get_param("ui", &mut buf);
        let ui = core::str::from_utf8(&buf[..n as usize]).unwrap().to_owned();
        ui.split(':').nth(at).unwrap().parse::<f64>().unwrap()
    };
    let mut p = Instance::new(48000.0);
    assert_eq!(devms(&p), 0.0);
    let mut l = [0.0f32; 480];
    let mut r = [0.0f32; 480];
    let mut last = 0.0;
    for _ in 0..100 {
        p.process_f32_split(&mut l, &mut r, 480, None);
        let now = devms(&p);
        assert!(now > last, "{now} after {last}");
        last = now;
    }
    assert_eq!(last, 1000.0, "100 blocks of 10 ms");
    /* A new rate counts on from where the old one left off. */
    p.set_sample_rate(44100.0);
    assert_eq!(devms(&p), 1000.0);
    p.process_f32_split(&mut l, &mut r, 441, None);
    assert_eq!(devms(&p), 1010.0);
    /* A mirror reports the engine's clock. */
    let mut m = Instance::new(44100.0);
    m.mirror("", &p.playhead());
    assert_eq!(devms(&m), 1010.0);
}

#[test]
fn the_longest_ui_readout_still_answers_whole() {
    let mut p = Instance::new(44100.0);
    p.set_param("slot", "7");
    p.set_param("length", "127");
    for i in 0..128 {
        p.set_param("cursor", &i.to_string());
        p.set_param("step", if i % 3 == 0 { "Tie" } else { "On" });
        p.set_param("step_amount", "0.5");
        p.set_param("step_order", &(128 - i).to_string());
    }
    let mut l = [0.0f32; 128];
    let mut r = [0.0f32; 128];
    for _ in 0..10_000 {
        p.process_f32_split(&mut l, &mut r, 128, None);
    }
    let mut buf = [0u8; 1024];
    let n = p.get_param("ui", &mut buf);
    assert!(n > 0, "the longest `ui` does not fit 1 KB");
    assert!(n < 900, "{n} bytes: within reach of a 1 KB read");
}
