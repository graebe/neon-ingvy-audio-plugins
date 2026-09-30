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
