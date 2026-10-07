// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A saved patch is the patch that was saved, for any patch the edits can make.
 *
 * tests/fixtures.rs pins every format a build has written to what it reads to;
 * this asks the question the other way round, over patches nobody wrote by
 * hand: an engine taken anywhere by any sequence of edits -- every key, every
 * slot, values in range and out of it -- writes a blob that another engine
 * loads into the same patch.
 *
 * "The same" is the format's, not the engine's: the blob writes an amount to
 * three decimals and a stage to two, and the host's own parameters carry the
 * exact values when a project reopens. So the engine that saved is compared
 * with the ones that loaded through what the format writes -- the blob, slot
 * by slot, and the slot files -- and the two that loaded, by the text door and
 * from a patch read once as a plugin's audio thread is handed it, with each
 * other through every readout.
 *
 * What it would catch: a field the writer leaves out or the reader skips, a
 * number whose decimals do not survive being read back, a slot's own sound or
 * its arrival order lost when it equals or differs from the top level's -- any
 * patch that drifts every time it is saved and reopened.
 *
 * THE SEED IS FIXED, so the quick tier sees the same cases on every run, and a
 * failure writes no regression file into the tree: it prints its smallest
 * case, which the same seed finds again.
 */

use proptest::prelude::*;
use proptest::test_runner::{Config, RngSeed};
use tg_core::edit::Edit;
use tg_core::mask::Mask;
use tg_core::params::Param;
use tg_core::slotfile::Kind;
use tg_core::state::Patch;
use tg_core::{Instance, MAX_STEPS, SLOTS};

fn mask(bits: u128) -> Mask {
    let mut m = Mask::new();
    for i in 0..MAX_STEPS {
        m.set(i, (bits >> i) & 1 != 0);
    }
    m
}

/* A value for any of the fifteen: whole indices and switches, the 0..1 of an
 * amount, and the wide range a stage or an out-of-range push lands in. */
fn value() -> impl Strategy<Value = f64> {
    prop_oneof![(0i32..10).prop_map(f64::from), 0.0f64..=1.0, -5.0f64..260.0]
}

fn edit() -> impl Strategy<Value = Edit> {
    prop_oneof![
        6 => (0i32..15, value()).prop_map(|(p, v)| Edit::Num(Param::from_i32(p).unwrap(), v)),
        1 => (0usize..140).prop_map(Edit::Cursor),
        2 => (0u8..3).prop_map(Edit::Step),
        2 => (0.0f32..=1.0).prop_map(Edit::StepAmount),
        1 => (1usize..140).prop_map(Edit::StepOrder),
        1 => prop::option::of(1u32..u32::MAX).prop_map(Edit::Randomize),
        1 => any::<u128>().prop_map(|b| Edit::Pattern(mask(b))),
        1 => any::<u128>().prop_map(|b| Edit::Ties(mask(b))),
    ]
}

fn get(p: &Instance, key: &str) -> String {
    let mut buf = vec![0u8; 16 * 1024];
    let n = p.get_param(key, &mut buf);
    assert!(n >= 0, "{key} is served");
    String::from_utf8(buf[..n as usize].to_vec()).unwrap()
}

fn export(p: &Instance, kind: Kind) -> String {
    let mut buf = vec![0u8; 16 * 1024];
    let n = p.export(kind, &mut buf);
    assert!(n > 0);
    String::from_utf8(buf[..n as usize].to_vec()).unwrap()
}

fn config() -> Config {
    Config { cases: 256, rng_seed: RngSeed::Fixed(0x7A7E_5EED), failure_persistence: None, ..Config::default() }
}

proptest! {
    #![proptest_config(config())]

    #[test]
    fn a_saved_patch_loads_as_the_patch_that_was_saved(edits in prop::collection::vec(edit(), 0..80)) {
        let mut saved = Instance::new(48000.0);
        for e in &edits {
            saved.apply_edit(e);
        }
        let blob = get(&saved, "state");

        let mut loaded = Instance::new(48000.0);
        loaded.set_param("state", &blob);
        let mut applied = Instance::new(48000.0);
        applied.load(&Patch::parse(&blob).expect("a build's own blob reads"));
        prop_assert_eq!(get(&loaded, "state"), blob.clone(), "written back byte for byte");
        prop_assert_eq!(get(&applied, "state"), blob, "and the same from a read patch");

        /* Every slot, as the host's Slot switches to it on all three. */
        for slot in 0..SLOTS {
            for p in [&mut saved, &mut loaded, &mut applied] {
                p.set_num(Param::Slot, slot as f64);
            }
            let format = (get(&saved, "state"), export(&saved, Kind::Slot));
            prop_assert_eq!((get(&loaded, "state"), export(&loaded, Kind::Slot)), format, "slot {} as written", slot);
            for key in ["state", "params", "length", "rate", "attack", "amount", "fade"] {
                prop_assert_eq!(get(&applied, key), get(&loaded, key), "slot {} {} from a read patch", slot, key);
            }
        }
        prop_assert_eq!(export(&loaded, Kind::Bank), export(&saved, Kind::Bank));
    }
}
