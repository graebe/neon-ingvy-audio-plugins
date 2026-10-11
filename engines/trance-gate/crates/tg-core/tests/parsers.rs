// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * EVERY TEXT THE GATE READS COMES FROM OUTSIDE IT: a project file, a slot
 * file, the clipboard, the Move's preset store. So every reader is fed what
 * nobody wrote -- arbitrary text, and real blobs damaged byte by byte -- and
 * must neither panic (the workspace aborts on one, taking the host with it)
 * nor leave an engine whose gain is anything but a number in 0..1.
 *
 * The seed is fixed, so the quick tier sees the same cases on every run.
 */

use proptest::prelude::*;
use proptest::test_runner::{Config, RngSeed};
use tg_core::paste::{classify, Clip};
use tg_core::slotfile::SlotFile;
use tg_core::state::Patch;
use tg_core::{Instance, Transport};

/// A real blob with something in every field: accents, an order, two slots.
fn real_blob() -> String {
    let mut p = Instance::new(48_000.0);
    p.set_param("pattern", "F0F1");
    p.set_param("cursor", "3");
    p.set_param("step_amount", "0.4");
    p.set_param("step_order", "2");
    p.set_param("slot", "2");
    p.set_param("attack", "37.5");
    p.set_param("fade", "0.25");
    let mut buf = vec![0u8; 8192];
    let n = p.get_param("state", &mut buf);
    String::from_utf8(buf[..n as usize].to_vec()).unwrap()
}

/// Read `text` through every door, then run the engine that loaded it.
fn read_everywhere(text: &str) {
    let _ = Patch::parse(text);
    let _ = Clip::parse(text);
    let _ = classify(text);
    let _ = SlotFile::parse(text);
    let mut p = Instance::new(48_000.0);
    p.set_param("state", text);
    let (mut l, mut r) = ([1f32; 256], [1f32; 256]);
    for b in 0..8 {
        let t = Transport { running: true, beats: b as f64 * 0.01, bpm: 140.0 };
        p.process_f32_split(&mut l, &mut r, 256, Some(&t));
        assert!(l.iter().all(|g| (0.0..=1.0).contains(g)), "gain left 0..1 after reading {text:?}");
        l.fill(1.0);
        r.fill(1.0);
    }
}

/// The characters the formats are made of, so damage stays near-valid.
const ALPHABET: &[u8] = b"{}[]\":,.-+0123456789abcdefABCDEFsvp_ eE\\nt";

proptest! {
    #![proptest_config(Config { cases: 3000, rng_seed: RngSeed::Fixed(0x7061_7273), failure_persistence: None, ..Config::default() })]

    #[test]
    fn arbitrary_text_is_refused_or_read_never_panics(text in ".{0,600}") {
        read_everywhere(&text);
    }

    #[test]
    fn text_made_of_the_formats_own_characters_is_refused_or_read(bytes in proptest::collection::vec(proptest::sample::select(ALPHABET), 0..600)) {
        read_everywhere(core::str::from_utf8(&bytes).unwrap());
    }

    #[test]
    fn a_real_blob_damaged_anywhere_is_refused_or_read(
        edits in proptest::collection::vec((any::<prop::sample::Index>(), 0u8..3, proptest::sample::select(ALPHABET)), 1..8),
    ) {
        let mut b = real_blob().into_bytes();
        for (at, kind, c) in edits {
            let len = b.len();
            let i = at.index(len.max(1));
            match kind {
                0 if len > 0 => b[i.min(len - 1)] = c,
                1 if len > 0 => { b.remove(i.min(len - 1)); }
                _ => b.insert(i.min(len), c),
            }
        }
        read_everywhere(core::str::from_utf8(&b).unwrap());
    }
}
