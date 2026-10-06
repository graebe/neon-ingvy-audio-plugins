// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

use super::*;
use crate::text::{write_degree, write_description, write_name, write_notes};
use music_core::DisplayBuffer;
use rstest::rstest;

const ON: u8 = 0x90;
const OFF: u8 = 0x80;
const CC: u8 = 0xB0;

fn play(d: &mut Detector, notes: &[u8]) -> Vec<NoteEvent> {
    let mut events = Vec::new();
    for &n in notes {
        d.on_midi(&[ON, n, 100], 0, |e| events.push(e));
    }
    events
}

fn release(d: &mut Detector, notes: &[u8]) -> Vec<NoteEvent> {
    let mut events = Vec::new();
    for &n in notes {
        d.on_midi(&[OFF, n, 0], 0, |e| events.push(e));
    }
    events
}

fn name(d: &Detector) -> String {
    let mut b = DisplayBuffer::<64>::new();
    write_name(d.reading(), d.names(), &mut b).unwrap();
    b.as_str().to_string()
}

fn description(d: &Detector) -> String {
    let mut b = DisplayBuffer::<96>::new();
    write_description(d.reading(), d.names(), &mut b).unwrap();
    b.as_str().to_string()
}

fn degree(d: &Detector) -> String {
    let mut b = DisplayBuffer::<16>::new();
    write_degree(d.reading(), &mut b).unwrap();
    b.as_str().to_string()
}

fn notes(d: &Detector) -> String {
    let mut b = DisplayBuffer::<128>::new();
    write_notes(d.reading(), d.names(), &mut b).unwrap();
    b.as_str().to_string()
}

fn in_key(tonic: i32, mode: i32) -> Detector {
    let mut d = Detector::new(48000.0);
    d.set_param(Param::Tonic, tonic);
    d.set_param(Param::Mode, mode);
    d
}

// --- naming ---------------------------------------------------------------

#[rstest]
#[case(&[60, 64, 67], "C", "C major", "I")]
#[case(&[52, 55, 60], "C/E", "C major · 1st inversion", "I")]
#[case(&[55, 60, 64], "C/G", "C major · 2nd inversion", "I")]
#[case(&[57, 60, 64, 67], "Am7", "A minor 7", "vi7")]
#[case(&[48, 52, 55, 69], "C6", "C major 6", "I6")]
#[case(&[48, 57, 64, 67], "C6", "C major 6", "I6")]
#[case(&[52, 57, 60, 67], "Am7/E", "A minor 7 · 2nd inversion", "vi7")]
#[case(&[55, 59, 62, 65], "G7", "G dominant 7", "V7")]
#[case(&[54, 57, 60, 64], "F#m7b5", "F# half-diminished 7", "#ivø7")]
#[case(&[58, 62, 65], "A#", "A# major", "bVII")] // sharps chosen; Auto: see below
fn chords_are_named_with_their_bass(
    #[case] played: &[u8],
    #[case] want_name: &str,
    #[case] want_description: &str,
    #[case] want_degree: &str,
) {
    let mut d = in_key(0, 0);
    d.set_param(Param::Spelling, 1); // sharps, so the table reads one way
    play(&mut d, played);
    assert_eq!(d.reading().kind(), Kind::Chord);
    assert_eq!(name(&d), want_name);
    assert_eq!(description(&d), want_description);
    assert_eq!(degree(&d), want_degree);
}

#[test]
fn one_note_is_a_note() {
    let mut d = Detector::new(48000.0);
    play(&mut d, &[60]);
    assert_eq!(d.reading().kind(), Kind::Note);
    assert_eq!(name(&d), "C4");
    assert_eq!(description(&d), "note · MIDI 60");
    assert_eq!(degree(&d), "");
}

#[test]
fn one_pitch_in_octaves_is_still_a_note() {
    let mut d = Detector::new(48000.0);
    play(&mut d, &[48, 60, 72]);
    assert_eq!(d.reading().kind(), Kind::Note);
    assert_eq!(name(&d), "C3");
    assert_eq!(description(&d), "C in 3 octaves");
}

#[rstest]
#[case(&[60, 64], "C–E", "major 3rd")]
#[case(&[60, 63], "C–Eb", "minor 3rd")]
#[case(&[48, 64], "C–E", "major 3rd + 1 octave")]
#[case(&[36, 64], "C–E", "major 3rd + 2 octaves")]
#[case(&[60, 66], "C–F#", "tritone")]
#[case(&[60, 67], "C5", "perfect 5th · power chord")]
#[case(&[48, 60, 64], "C–E", "major 3rd + 1 octave")]
fn two_pitch_classes_are_an_interval(
    #[case] played: &[u8],
    #[case] want_name: &str,
    #[case] want_description: &str,
) {
    let mut d = Detector::new(48000.0);
    play(&mut d, played);
    assert_eq!(d.reading().kind(), Kind::Interval);
    assert_eq!(name(&d), want_name);
    assert_eq!(description(&d), want_description);
}

#[test]
fn a_cluster_with_no_name_lists_its_pitches() {
    let mut d = Detector::new(48000.0);
    play(&mut d, &[60, 61, 62]);
    assert_eq!(d.reading().kind(), Kind::Unnamed);
    assert_eq!(name(&d), "C Db D");
    assert_eq!(description(&d), "no common name · 3 pitch classes");
    assert_eq!(degree(&d), "");
}

#[test]
fn the_notes_are_listed_low_to_high_in_the_keys_spelling() {
    let mut d = in_key(3, 0); // Eb major: flats under Auto
    play(&mut d, &[67, 63, 58]);
    assert_eq!(notes(&d), "Bb3 Eb4 G4");
    assert_eq!(name(&d), "Eb/Bb");
    d.set_param(Param::Spelling, 1);
    assert_eq!(notes(&d), "A#3 D#4 G4");
    assert_eq!(name(&d), "D#/A#");
}

#[test]
fn alternatives_are_the_other_roots_over_the_same_bass() {
    let mut d = Detector::new(48000.0);
    play(&mut d, &[48, 52, 55, 57]); // C E G A, C in the bass
    assert_eq!(name(&d), "C6");
    let alternatives: Vec<String> = d.reading().alternatives().map(|c| c.to_string()).collect();
    assert_eq!(alternatives, ["Am7/C"]);
}

#[test]
fn a_diminished_seventh_has_three_alternatives() {
    let mut d = Detector::new(48000.0);
    play(&mut d, &[59, 62, 65, 68]);
    assert_eq!(d.reading().alternatives().count(), 3);
}

#[test]
fn only_the_lowest_sixteen_notes_are_named_but_all_are_kept() {
    let mut d = Detector::new(48000.0);
    let many: Vec<u8> = (0..20).map(|i| 36 + i * 3).collect();
    play(&mut d, &many);
    assert_eq!(d.reading().notes().count_ones(), 20);
    assert_eq!(d.reading().bass().unwrap().midi(), 36);
}

// --- what sounds ------------------------------------------------------------

#[test]
fn a_note_on_at_velocity_zero_is_a_release() {
    let mut d = Detector::new(48000.0);
    play(&mut d, &[60]);
    d.on_midi(&[ON, 60, 0], 0, |_| {});
    assert_eq!(d.sounding(), 0);
    assert_eq!(d.reading().kind(), Kind::Empty);
}

#[test]
fn channels_merge_into_one_chord() {
    let mut d = Detector::new(48000.0);
    d.on_midi(&[ON, 60, 100], 0, |_| {});
    d.on_midi(&[ON | 1, 64, 100], 0, |_| {});
    d.on_midi(&[ON | 9, 67, 100], 0, |_| {});
    assert_eq!(name(&d), "C");
}

#[test]
fn the_pedal_keeps_released_notes_sounding_until_it_lifts() {
    let mut d = Detector::new(48000.0);
    d.on_midi(&[CC, 64, 127], 0, |_| {});
    play(&mut d, &[60, 64, 67]);
    let ended = release(&mut d, &[60, 64, 67]);
    assert!(ended.is_empty(), "nothing stops while the pedal is down");
    assert_eq!(name(&d), "C");
    assert!(d.pedal());

    let mut lifted = Vec::new();
    d.on_midi(&[CC, 64, 0], 0, |e| lifted.push(e));
    assert_eq!(lifted.len(), 3);
    assert!(lifted.iter().all(|e| e.velocity == 0));
    assert_eq!(d.reading().kind(), Kind::Empty);
}

#[test]
fn a_key_struck_again_under_the_pedal_stops_with_its_own_release() {
    let mut d = Detector::new(48000.0);
    d.on_midi(&[CC, 64, 127], 0, |_| {});
    play(&mut d, &[60]);
    release(&mut d, &[60]);
    let again = play(&mut d, &[60]);
    assert!(again.is_empty(), "it never stopped, so it does not start");
    d.on_midi(&[CC, 64, 0], 0, |_| {});
    assert_eq!(d.sounding(), 1 << 60, "the key is down, the pedal let go");
    release(&mut d, &[60]);
    assert_eq!(d.sounding(), 0);
}

#[rstest]
#[case(120)]
#[case(123)]
fn a_panic_stops_everything_and_clears_a_held_chord(#[case] controller: u8) {
    let mut d = Detector::new(48000.0);
    d.set_param(Param::Hold, 1);
    d.on_midi(&[CC, 64, 127], 0, |_| {});
    play(&mut d, &[60, 64, 67]);
    let mut stopped = Vec::new();
    d.on_midi(&[CC, controller, 0], 0, |e| stopped.push(e));
    assert_eq!(stopped.len(), 3);
    assert_eq!(d.sounding(), 0);
    assert!(!d.pedal());
    assert_eq!(d.reading().kind(), Kind::Empty);
}

#[test]
fn a_shell_reset_is_a_panic() {
    let mut d = Detector::new(48000.0);
    play(&mut d, &[60, 64]);
    let mut stopped = 0;
    d.reset(|_| stopped += 1);
    assert_eq!(stopped, 2);
    assert_eq!(d.reading().kind(), Kind::Empty);
}

#[test]
fn messages_that_change_nothing_leave_the_reading_alone() {
    let mut d = Detector::new(48000.0);
    play(&mut d, &[60, 64, 67]);
    let serial = d.serial();
    for msg in [
        &[0xF8][..],
        &[ON, 60],
        &[CC, 1, 64],
        &[OFF, 61, 0],
        &[0xE0, 0, 64],
    ] {
        d.on_midi(msg, 0, |_| panic!("nothing started or stopped"));
    }
    assert_eq!(d.serial(), serial);
}

// --- hold -------------------------------------------------------------------

#[test]
fn hold_keeps_the_last_reading_after_release() {
    let mut d = Detector::new(48000.0);
    d.set_param(Param::Hold, 1);
    play(&mut d, &[57, 60, 64]);
    release(&mut d, &[57, 60, 64]);
    assert_eq!(name(&d), "Am");
    assert!(d.reading().is_held());
    assert_eq!(d.sounding(), 0);

    play(&mut d, &[62]);
    assert_eq!(name(&d), "D4");
    assert!(!d.reading().is_held());
}

#[test]
fn hold_keeps_the_whole_chord_while_its_keys_come_up() {
    let mut d = Detector::new(48000.0);
    d.set_param(Param::Hold, 1);
    play(&mut d, &[60, 64, 67]);
    release(&mut d, &[64]);
    assert_eq!(name(&d), "C", "the chord played, not what is left of it");
    assert!(!d.reading().is_held(), "two keys are still down");
    assert_eq!(
        d.sounding().count_ones(),
        2,
        "the keyboard still sees the truth"
    );
    release(&mut d, &[60, 67]);
    assert!(d.reading().is_held());
}

#[test]
fn without_hold_what_is_left_is_named() {
    let mut d = Detector::new(48000.0);
    play(&mut d, &[60, 64, 67]);
    release(&mut d, &[64]);
    assert_eq!(d.reading().kind(), Kind::Interval);
    assert_eq!(name(&d), "C5");
}

#[test]
fn turning_hold_off_clears_a_held_reading() {
    let mut d = Detector::new(48000.0);
    d.set_param(Param::Hold, 1);
    play(&mut d, &[60, 64, 67]);
    release(&mut d, &[60, 64, 67]);
    d.set_param(Param::Hold, 0);
    assert_eq!(d.reading().kind(), Kind::Empty);
}

#[test]
fn changing_the_key_rereads_a_held_chord_and_keeps_it_held() {
    let mut d = Detector::new(48000.0);
    d.set_param(Param::Hold, 1);
    play(&mut d, &[57, 60, 64]);
    release(&mut d, &[57, 60, 64]);
    assert_eq!(degree(&d), "vi");
    d.set_param(Param::Tonic, 9);
    d.set_param(Param::Mode, 5);
    assert_eq!(degree(&d), "i");
    assert!(d.reading().is_held());
}

// --- parameters -------------------------------------------------------------

#[test]
fn the_table_is_in_index_order_with_unique_keys() {
    for (i, p) in Param::ALL.iter().enumerate() {
        assert_eq!(*p as usize, i);
        assert_eq!(Param::from_i32(i as i32), Some(*p));
        assert_eq!(Param::from_key(p.info().key), Some(*p));
        assert!((p.info().default) < p.choices());
        for q in &Param::ALL[i + 1..] {
            assert_ne!(p.info().key, q.info().key);
        }
    }
    assert_eq!(Param::from_i32(-1), None);
    assert_eq!(Param::from_i32(PARAM_COUNT as i32), None);
    assert_eq!(Param::from_key("nope"), None);
}

#[test]
fn choices_are_clamped_and_defaults_apply() {
    let mut d = Detector::new(48000.0);
    for p in Param::ALL {
        assert_eq!(d.param(p), p.info().default);
    }
    d.set_param(Param::Mode, 99);
    assert_eq!(d.param(Param::Mode), 6);
    d.set_param(Param::Mode, -4);
    assert_eq!(d.param(Param::Mode), 0);
    d.set_param(Param::Zoom, 3);
    assert_eq!(params::zoom_percent(d.param(Param::Zoom)), 150);
    assert_eq!(params::span_bars(d.param(Param::HistorySpan)), 4);
}

#[test]
fn the_view_settings_are_kept_but_change_no_reading() {
    let mut d = Detector::new(48000.0);
    play(&mut d, &[60, 64, 67]);
    let serial = d.serial();
    d.set_param(Param::HistoryView, 1);
    d.set_param(Param::HistorySpan, 0);
    d.set_param(Param::Zoom, 0);
    assert_eq!(d.serial(), serial);
    assert_eq!(d.param(Param::HistoryView), 1);
}

#[test]
fn auto_spelling_names_notes_the_way_the_key_writes_them() {
    let mut d = in_key(0, 0); // C major
    assert_eq!(d.names(), Names::Key(d.key()));
    play(&mut d, &[58, 62, 65]); // a borrowed bVII
    assert_eq!(
        name(&d),
        "Bb",
        "C major writes its lowered seventh as B flat"
    );
    assert_eq!(notes(&d), "Bb3 D4 F4");
    d.set_param(Param::Spelling, 1);
    assert_eq!(name(&d), "A#");
    d.set_param(Param::Spelling, 2);
    assert_eq!(name(&d), "Bb");
    assert_eq!(d.names(), Names::Fixed(Spelling::Flats));

    let mut f_sharp = in_key(6, 0); // F# major: its seventh is E#
    play(&mut f_sharp, &[53, 56, 59]); // E# G# B: the vii°
    assert_eq!(name(&f_sharp), "E#dim");
    assert_eq!(degree(&f_sharp), "vii°");
    assert_eq!(f_sharp.key().to_string(), "F# Ionian");
}

// --- the history's clock ----------------------------------------------------

#[test]
fn events_are_stamped_on_the_clock_at_their_offset() {
    let mut d = Detector::new(48000.0);
    let t = Transport {
        playing: true,
        ppq: 0.0,
        bpm: 120.0,
        num: 4,
        den: 4,
    };
    d.begin_block(Some(&t));
    let mut events = Vec::new();
    d.on_midi(&[ON, 60, 90], 24000, |e| events.push(e));
    d.end_block(48000);
    assert_eq!(
        events,
        [NoteEvent {
            note: 60,
            velocity: 90,
            at: 1.0
        }]
    );
    assert_eq!(d.timeline().now(), 2.0);
}

#[test]
fn the_clock_never_goes_back_when_the_host_loops() {
    let mut d = Detector::new(48000.0);
    let mut last = -1.0;
    for block in 0..32 {
        let ppq = (block % 8) as f64 * 0.5; // a one-bar loop, half a beat a block
        let t = Transport {
            playing: true,
            ppq,
            bpm: 120.0,
            num: 4,
            den: 4,
        };
        d.begin_block(Some(&t));
        assert!(d.timeline().now() > last);
        last = d.timeline().now();
        d.end_block(12000);
    }
}

#[test]
fn bar_lines_follow_the_host_while_it_plays_and_carry_on_when_it_stops() {
    let mut d = Detector::new(48000.0);
    d.end_block(4800); // 0.2 quarters before the transport starts
    let t = Transport {
        playing: true,
        ppq: 2.0,
        bpm: 120.0,
        num: 3,
        den: 4,
    };
    d.begin_block(Some(&t));
    let tl = *d.timeline();
    assert_eq!(tl.bar(), 3.0);
    assert!((tl.bar_origin() - (0.2 - 2.0)).abs() < 1e-9);

    d.end_block(4800);
    let stopped = Transport {
        playing: false,
        ppq: 0.0,
        bpm: 90.0,
        num: 3,
        den: 4,
    };
    d.begin_block(Some(&stopped));
    assert!(!d.timeline().playing());
    assert_eq!(d.timeline().bpm(), 90.0);
    assert!((d.timeline().bar_origin() - tl.bar_origin()).abs() < 1e-9);
}

#[test]
fn a_host_without_a_clock_runs_at_the_default_tempo() {
    let mut d = Detector::new(48000.0);
    d.begin_block(None);
    d.end_block(48000);
    assert_eq!(d.timeline().now(), 2.0);
    assert!(!d.timeline().playing());

    let mut broken = Detector::new(0.0);
    broken.end_block(48000);
    assert_eq!(broken.timeline().now(), 0.0);
    broken.set_sample_rate(24000.0);
    broken.end_block(24000);
    assert_eq!(broken.timeline().now(), 2.0);
}
