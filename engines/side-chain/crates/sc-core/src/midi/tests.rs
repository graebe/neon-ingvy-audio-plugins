// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! What a note means: the channel and note filter, Trigger vs Gate, velocity,
//! and the panic.

use crate::midi::{Action, Midi};
use crate::tests::{note_off, note_on};

/* ------------------------------------------------------------------- midi */


#[test]
fn midi_filters_channel_and_note() {
    let mut m = Midi {
        channel: 2,
        note: 36,
        ..Default::default()
    };
    assert!(m.decode(&note_on(1, 36, 100)).is_none(), "wrong channel");
    assert!(m.decode(&note_on(2, 37, 100)).is_none(), "wrong note");
    assert!(matches!(
        m.decode(&note_on(2, 36, 100)),
        Some(Action::Trigger(_))
    ));

    /* Omni takes every channel. */
    let mut omni = Midi {
        channel: 0,
        note: 36,
        ..Default::default()
    };
    for ch in 1..=16 {
        assert!(matches!(
            omni.decode(&note_on(ch, 36, 100)),
            Some(Action::Trigger(_))
        ));
    }
}

#[test]
fn a_note_on_at_velocity_zero_is_a_note_off() {
    /* Hosts and hardware both send it. Read as a trigger at zero depth it
     * leaves Gate mode holding a note that was never pressed. */
    let mut m = Midi {
        gate: true,
        ..Default::default()
    };
    assert!(matches!(
        m.decode(&note_on(1, 36, 100)),
        Some(Action::Trigger(_))
    ));
    assert_eq!(m.held, 1);
    assert_eq!(m.decode(&note_on(1, 36, 0)), Some(Action::Release));
    assert_eq!(m.held, 0);
}

#[test]
fn gate_mode_releases_only_on_the_last_note_off() {
    let mut m = Midi {
        gate: true,
        ..Default::default()
    };
    m.decode(&note_on(1, 36, 100));
    m.decode(&note_on(1, 36, 100));
    assert_eq!(m.held, 2);
    assert_eq!(m.decode(&note_off(1, 36)), None, "one note still down");
    assert_eq!(m.decode(&note_off(1, 36)), Some(Action::Release));
}

#[test]
fn trigger_mode_ignores_note_offs() {
    let mut m = Midi::default();
    m.decode(&note_on(1, 36, 100));
    assert_eq!(m.decode(&note_off(1, 36)), None);
}

#[test]
fn velocity_scales_depth_only_when_asked_to() {
    let mut m = Midi {
        vel_sens: 0.0,
        ..Default::default()
    };
    assert_eq!(m.decode(&note_on(1, 36, 1)), Some(Action::Trigger(1.0)));

    m.vel_sens = 1.0;
    let Some(Action::Trigger(s)) = m.decode(&note_on(1, 36, 64)) else {
        panic!("no trigger")
    };
    assert!((s - 64.0 / 127.0).abs() < 1e-12, "scale {s}");
    let Some(Action::Trigger(s)) = m.decode(&note_on(1, 36, 127)) else {
        panic!("no trigger")
    };
    assert!((s - 1.0).abs() < 1e-12);
}

#[test]
fn a_panic_resets_whatever_the_note_filter_says() {
    /* A panic that only arrived on the trigger note would not be a panic. */
    for cc in [120u8, 123u8] {
        let mut m = Midi {
            note: 36,
            gate: true,
            ..Default::default()
        };
        m.decode(&note_on(1, 36, 100));
        assert_eq!(m.held, 1);
        assert_eq!(m.decode(&[0xB0, cc, 0]), Some(Action::Reset), "CC {cc}");
        assert_eq!(m.held, 0);
    }
}

#[test]
fn a_short_message_is_rejected_not_indexed_into() {
    let mut m = Midi::default();
    assert!(m.decode(&[]).is_none());
    assert!(m.decode(&[0x90]).is_none());
    assert!(m.decode(&[0x90, 36]).is_none());
}

#[test]
fn a_panic_obeys_the_channel_filter_and_omni_hears_every_channel() {
    /* The note filter never applies to a panic (above); the channel filter
     * always has. A channel mode message speaks for its own channel, so a
     * ducker on channel 2 keeps its held note through channel 1's. */
    let mut m = Midi {
        channel: 2,
        gate: true,
        ..Default::default()
    };
    m.decode(&note_on(2, 36, 100));
    assert_eq!(m.decode(&[0xB0, 123, 0]), None, "channel 1's All Notes Off");
    assert_eq!(m.held, 1);
    assert_eq!(m.decode(&[0xB1, 120, 0]), Some(Action::Reset), "channel 2's");
    assert_eq!(m.held, 0);

    let mut omni = Midi {
        channel: 0,
        ..Default::default()
    };
    for ch in 0..16u8 {
        for cc in [120u8, 123u8] {
            assert_eq!(
                omni.decode(&[0xB0 | ch, cc, 0]),
                Some(Action::Reset),
                "ch {} CC {cc}",
                ch + 1
            );
        }
    }
}

#[test]
fn nothing_but_the_trigger_note_and_the_two_panics_means_anything() {
    let mut m = Midi {
        gate: true,
        ..Default::default()
    };
    m.decode(&note_on(1, 36, 100));
    let others: [&[u8]; 8] = [
        &[0xB0, 121, 0],           /* Reset All Controllers is not a panic */
        &[0xB0, 36, 127],          /* a controller numbered like the note */
        &[0xA0, 36, 90],           /* aftertouch on the trigger note */
        &[0xC0, 36],               /* a program change */
        &[0xE0, 0, 64],            /* a pitch bend */
        &[0xF8],                   /* the clock */
        &[0xF0, 0x7E, 0x7F, 0xF7], /* SysEx */
        &[36, 100, 0],             /* running status: no status byte of its own */
    ];
    for msg in others {
        assert_eq!(m.decode(msg), None, "{msg:02X?}");
    }
    assert_eq!(m.held, 1, "and none of them touched the held note");
}

#[test]
fn a_longer_buffer_is_read_from_the_front() {
    /* The C ABI and the Move pass up to eight bytes. */
    let mut m = Midi::default();
    assert_eq!(m.decode(&[0x90, 36, 127, 0, 0xFF]), Some(Action::Trigger(1.0)));
    assert_eq!(m.decode(&[0xB0, 123, 0, 0x90, 36, 100]), Some(Action::Reset));
}

#[test]
fn a_data_byte_with_its_top_bit_set_cuts_the_message_short() {
    /* That byte is a status byte, so neither of these is what it looks like:
     * not a note-on at velocity 0x80, not an All Notes Off with an odd value.
     * The hand-written decode read them as both; see the module header. */
    let mut m = Midi {
        gate: true,
        ..Default::default()
    };
    assert_eq!(m.decode(&[0x90, 36, 0x80]), None);
    assert_eq!(m.held, 0);
    m.decode(&note_on(1, 36, 100));
    assert_eq!(m.decode(&[0x80, 36, 0xFF]), None);
    assert_eq!(m.decode(&[0xB0, 123, 0x90]), None);
    assert_eq!(m.held, 1);
}

#[test]
fn a_note_name_is_read_in_lives_numbering() {
    use crate::midi::note_from_name;
    assert_eq!(note_from_name("C-2"), Some(0));
    assert_eq!(note_from_name("C#-2"), Some(1));
    assert_eq!(note_from_name("C1"), Some(36));
    assert_eq!(note_from_name("F#3"), Some(66));
    assert_eq!(note_from_name("G8"), Some(127));
    /* Past the MIDI range, and things that are not names at all. */
    assert_eq!(note_from_name("G#8"), None);
    assert_eq!(note_from_name("36"), None);
    assert_eq!(note_from_name(""), None);
    assert_eq!(note_from_name("H1"), None);
    assert_eq!(note_from_name("C"), None);
    assert_eq!(note_from_name("C-"), None);
}

#[test]
fn the_queue_walks_in_offset_order_and_keeps_arrival_order_within_one() {
    use crate::midi::Queue;
    let mut q = Queue::default();
    q.push(5, Action::Trigger(1.0));
    q.push(2, Action::Release);
    q.push(100, Action::Trigger(0.25)); /* past the block: late, not lost */
    q.push(5, Action::Reset);
    q.push(2, Action::Trigger(0.5));
    q.prepare(8);
    let mut seen = Vec::new();
    for i in 0..8 {
        while let Some(a) = q.pop_at(i) {
            seen.push((i, a));
        }
    }
    assert_eq!(
        seen,
        vec![
            (2, Action::Release),
            (2, Action::Trigger(0.5)),
            (5, Action::Trigger(1.0)),
            (5, Action::Reset),
            (7, Action::Trigger(0.25)),
        ]
    );
}

#[test]
fn a_full_queue_in_reverse_order_is_still_walked_in_order() {
    use crate::midi::{Queue, QUEUE_MAX};
    let mut q = Queue::default();
    for k in 0..QUEUE_MAX + 3 {
        q.push(QUEUE_MAX - 1 - k.min(QUEUE_MAX - 1), Action::Trigger(k as f64 / 100.0));
    }
    assert_eq!(q.dropped(), 3, "the overflow is counted");
    q.prepare(QUEUE_MAX);
    let mut n = 0;
    for i in 0..QUEUE_MAX {
        while let Some(a) = q.pop_at(i) {
            assert_eq!(a, Action::Trigger((QUEUE_MAX - 1 - i) as f64 / 100.0));
            n += 1;
        }
    }
    assert_eq!(n, QUEUE_MAX);
}
