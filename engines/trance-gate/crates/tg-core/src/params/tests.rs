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
