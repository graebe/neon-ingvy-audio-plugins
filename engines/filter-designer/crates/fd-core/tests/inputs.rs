// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! THE ENGINE'S DOORS FOR WHAT IT CANNOT TRUST: the Move's key and value as
//! text (`set_param`, `get_param`) and MIDI bytes (`on_midi`). They take
//! anything. Nothing panics -- a panic aborts the host -- every value stays
//! inside its range, and only a whole CC 120 or 123 resets.

use fd_core::{Engine, Field, Param, PARAM_COUNT};
use proptest::prelude::*;

/// A key the engine owns, or anything at all.
fn key() -> impl Strategy<Value = String> {
    prop_oneof![
        (0..PARAM_COUNT).prop_map(|i| Param::from_index(i).unwrap().info().key.to_string()),
        Just("panic".to_string()),
        any::<String>(),
    ]
}

/// An engine with a resonant bell that has heard something: state to keep
/// or to lose.
fn ringing() -> Engine {
    let mut e = Engine::new(48_000.0);
    e.set_num(Param::Node(0, Field::Type), 1.0);
    e.set_num(Param::Node(0, Field::Gain), 12.0);
    e.set_num(Param::Node(0, Field::Q), 10.0);
    e.reset();
    let (mut l, mut r) = ([0.5f32; 256], [-0.5f32; 256]);
    e.process_f32(&mut l, Some(&mut r));
    e
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(500))]

    #[test]
    fn any_key_and_text_leave_every_value_in_range(k in key(), val in any::<String>()) {
        let mut e = Engine::new(48_000.0);
        let owned = e.set_param(&k, &val);
        prop_assert_eq!(owned, k == "panic" || Param::from_key(&k).is_some());
        for i in 0..PARAM_COUNT {
            let p = Param::from_index(i).unwrap();
            let v = e.num(p);
            prop_assert!(v.is_finite() && p.clamp(v) == v, "{:?} is {} after {:?} = {:?}", p, v, k, val);
        }
        let mut out = [0u8; 64];
        let n = e.get_param(&k, &mut out);
        prop_assert_eq!(n >= 0, Param::from_key(&k).is_some());
        let (mut l, mut r) = ([0.25f32; 64], [-0.25f32; 64]);
        e.process_f32(&mut l, Some(&mut r));
        prop_assert!(l.iter().chain(r.iter()).all(|x| x.is_finite()));
    }

    #[test]
    fn only_a_whole_panic_message_resets(bytes in proptest::collection::vec(any::<u8>(), 0..8)) {
        let mut e = ringing();
        e.on_midi(&bytes);
        let panic = bytes.len() == 3 && bytes[0] & 0xF0 == 0xB0 && matches!(bytes[1], 120 | 123) && bytes[2] < 0x80;
        if panic {
            prop_assert!(e.is_silent());
        } else if !(bytes.len() >= 3 && bytes[0] & 0xF0 == 0xB0 && matches!(bytes[1], 120 | 123)) {
            prop_assert!(!e.is_silent(), "{:02x?} reset the engine", bytes);
        }
    }
}
