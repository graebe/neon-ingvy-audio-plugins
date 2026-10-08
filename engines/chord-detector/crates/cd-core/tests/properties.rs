// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! What holds for any MIDI at all.

use cd_core::{Detector, Kind, NoteEvent, Param};
use proptest::prelude::*;

fn any_message() -> impl Strategy<Value = Vec<u8>> {
    prop_oneof![
        (0x80u8..0xF0, 0u8..128, 0u8..128).prop_map(|(s, a, b)| vec![s, a, b]),
        proptest::collection::vec(any::<u8>(), 0..4),
    ]
}

proptest! {
    /// Whatever arrives: no panic, every start is matched by at most one stop,
    /// and what the events say is sounding is what the engine says.
    #[test]
    fn the_events_describe_exactly_what_sounds(
        messages in proptest::collection::vec(any_message(), 0..200),
        hold in 0i32..2,
    ) {
        let mut d = Detector::new(44100.0);
        d.set_param(Param::Hold, hold);
        let mut sounding = 0u128;
        for (i, m) in messages.iter().enumerate() {
            let mut events: Vec<NoteEvent> = Vec::new();
            d.on_midi(m, i as u32, |e| events.push(e));
            for e in events {
                let bit = 1u128 << e.note;
                if e.velocity > 0 {
                    prop_assert_eq!(sounding & bit, 0, "started twice");
                    sounding |= bit;
                } else {
                    prop_assert_ne!(sounding & bit, 0, "stopped while silent");
                    sounding &= !bit;
                }
            }
            prop_assert_eq!(sounding, d.sounding());
            if hold == 0 {
                prop_assert_eq!(d.reading().notes(), d.sounding());
            }
            prop_assert_eq!(d.reading().kind() == Kind::Empty, d.reading().notes() == 0);
        }
    }

    /// The name does not depend on the key -- only the degree and spelling do.
    #[test]
    fn the_key_changes_the_degree_not_the_chord(
        notes in proptest::collection::btree_set(21u8..109, 1..8),
        tonic in 0i32..12, mode in 0i32..7,
    ) {
        let mut a = Detector::new(48000.0);
        let mut b = Detector::new(48000.0);
        b.set_param(Param::Tonic, tonic);
        b.set_param(Param::Mode, mode);
        for n in &notes {
            a.on_midi(&[0x90, *n, 100], 0, |_| {});
            b.on_midi(&[0x90, *n, 100], 0, |_| {});
        }
        prop_assert_eq!(a.reading().chord(), b.reading().chord());
        prop_assert_eq!(a.reading().kind(), b.reading().kind());
    }
}
