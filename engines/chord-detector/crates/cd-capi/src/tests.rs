// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

use super::*;
use std::ffi::CStr;
use std::ptr::{null, null_mut};

fn text(buf: &[c_char]) -> String {
    unsafe { CStr::from_ptr(buf.as_ptr()) }
        .to_str()
        .unwrap()
        .to_string()
}

/// One block through the ABI, the way the JUCE shell runs it.
unsafe fn block(shell: *mut CdShell, midi: &[[u8; 3]], params: &[(i32, i32)]) {
    let core = cd_shell_begin(shell);
    cd_core_begin_block(core, 1, 0.0, 120.0, 4, 4, 1);
    for (i, &(p, v)) in params.iter().enumerate() {
        let _ = i;
        cd_core_set_param(core, p, v);
    }
    for (i, m) in midi.iter().enumerate() {
        cd_core_on_midi(core, m.as_ptr(), 3, i as u32);
    }
    cd_core_end_block(core, 512);
    cd_shell_end(shell, 512);
}

unsafe fn read(shell: *mut CdShell) -> CdReading {
    let mut r = CdReading::empty();
    assert!(cd_shell_read(shell, &mut r));
    r
}

#[test]
fn a_chord_played_is_read_back_with_its_texts() {
    unsafe {
        let shell = cd_shell_create(48000.0);
        block(
            shell,
            &[[0x90, 52, 90], [0x90, 55, 90], [0x90, 60, 90]],
            &[],
        );
        let r = read(shell);
        assert_eq!(r.kind, 3);
        assert_eq!(text(&r.name), "C/E");
        assert_eq!(text(&r.description), "C major · 1st inversion");
        assert_eq!(text(&r.degree), "I");
        assert_eq!(text(&r.notes_text), "E3 G3 C4");
        assert_eq!(text(&r.key_name), "C Ionian");
        assert_eq!(r.root, 0);
        assert_eq!(r.bass, 52);
        assert_eq!(r.pitch_classes, 0b1001_0001);
        assert_eq!(r.notes, [1 << 52 | 1 << 55 | 1 << 60, 0]);
        assert_eq!(r.sounding, r.notes);
        assert_eq!(r.scale, music_core::Key::default().pitch_set().bits());
        assert_eq!(r.playing, 1);
        assert_eq!(r.bpm, 120.0);
        cd_shell_destroy(shell);
    }
}

#[test]
fn the_key_and_spelling_reach_the_texts() {
    unsafe {
        let shell = cd_shell_create(48000.0);
        // F Dorian: three flats under Auto.
        block(
            shell,
            &[[0x90, 63, 90], [0x90, 67, 90], [0x90, 70, 90]],
            &[(0, 5), (1, 1)],
        );
        let r = read(shell);
        assert_eq!(text(&r.name), "Eb");
        assert_eq!(text(&r.degree), "VII"); // Eb is F Dorian's own seventh
        assert_eq!(text(&r.key_name), "F Dorian");
        assert_eq!((r.tonic, r.mode, r.signature, r.spelling), (5, 1, -3, 1));

        block(shell, &[], &[(2, 1)]); // Sharps
        let r = read(shell);
        assert_eq!(text(&r.name), "D#");
        assert_eq!(r.spelling, 0);
        cd_shell_destroy(shell);
    }
}

#[test]
fn alternatives_and_hold_are_published() {
    unsafe {
        let shell = cd_shell_create(48000.0);
        block(
            shell,
            &[
                [0x90, 48, 90],
                [0x90, 52, 90],
                [0x90, 55, 90],
                [0x90, 57, 90],
            ],
            &[(3, 1)],
        );
        let r = read(shell);
        assert_eq!(text(&r.name), "C6");
        assert_eq!(r.alternative_count, 1);
        assert_eq!(text(&r.alternatives[0]), "Am7/C");
        assert_eq!(text(&r.alternatives[1]), "");

        block(
            shell,
            &[[0x80, 48, 0], [0x80, 52, 0], [0x80, 55, 0], [0x80, 57, 0]],
            &[],
        );
        let r = read(shell);
        assert_eq!(r.held, 1);
        assert_eq!(text(&r.name), "C6");
        assert_eq!(r.sounding, [0, 0]);
        assert_ne!(r.notes, [0, 0]);
        cd_shell_destroy(shell);
    }
}

#[test]
fn note_events_are_drained_in_order_and_a_reset_stops_everything() {
    unsafe {
        let shell = cd_shell_create(48000.0);
        block(shell, &[[0x90, 60, 100], [0x90, 64, 80]], &[]);
        let mut events = [CdNoteEvent {
            at: 0.0,
            note: 0,
            velocity: 0,
        }; 8];
        let n = cd_shell_drain(shell, events.as_mut_ptr(), events.len());
        assert_eq!(n, 2);
        assert_eq!((events[0].note, events[0].velocity), (60, 100));
        assert_eq!((events[1].note, events[1].velocity), (64, 80));
        assert!(events[1].at > events[0].at, "stamped at their offsets");
        assert_eq!(cd_shell_drain(shell, events.as_mut_ptr(), 8), 0);

        let core = cd_shell_begin(shell);
        cd_core_reset(core);
        cd_core_end_block(core, 64);
        cd_shell_end(shell, 64);
        assert_eq!(cd_shell_drain(shell, events.as_mut_ptr(), 8), 2);
        assert!(events[..2].iter().all(|e| e.velocity == 0));
        assert_eq!(read(shell).kind, 0);
        cd_shell_destroy(shell);
    }
}

#[test]
fn a_full_ring_counts_what_it_drops() {
    unsafe {
        let shell = cd_shell_create(48000.0);
        let core = cd_shell_begin(shell);
        for round in 0..20 {
            for n in 0..128u8 {
                let status = if round % 2 == 0 { 0x90 } else { 0x80 };
                cd_core_on_midi(core, [status, n, 100].as_ptr(), 3, 0);
            }
        }
        cd_core_end_block(core, 64);
        cd_shell_end(shell, 64);
        let r = read(shell);
        assert_eq!(r.dropped as usize, 20 * 128 - EVENTS);
        cd_shell_destroy(shell);
    }
}

#[test]
fn the_parameter_table_is_readable_through_c() {
    unsafe {
        assert_eq!(cd_param_count(), PARAM_COUNT as i32);
        let mut info = CdParamInfo {
            choice_count: 0,
            default_choice: 0,
            automatable: false,
        };
        assert!(cd_param_info(1, &mut info));
        assert_eq!(
            info,
            CdParamInfo {
                choice_count: 7,
                default_choice: 0,
                automatable: true
            }
        );
        assert!(cd_param_info(6, &mut info));
        assert!(!info.automatable);
        assert!(!cd_param_info(7, &mut info));
        assert!(!cd_param_info(0, null_mut()));

        let mut buf = [0 as c_char; 16];
        assert_eq!(cd_param_key(0, buf.as_mut_ptr(), buf.len()), 5);
        assert_eq!(text(&buf), "tonic");
        assert_eq!(cd_param_name(5, buf.as_mut_ptr(), buf.len()), 4);
        assert_eq!(text(&buf), "Span");
        assert_eq!(cd_param_choice(1, 6, buf.as_mut_ptr(), buf.len()), 7);
        assert_eq!(text(&buf), "Locrian");
        assert_eq!(cd_param_choice(1, 7, buf.as_mut_ptr(), buf.len()), -1);
        assert_eq!(cd_param_choice(1, -1, buf.as_mut_ptr(), buf.len()), -1);
        assert_eq!(cd_param_key(99, buf.as_mut_ptr(), buf.len()), -1);
        assert_eq!(cd_param_name(-1, buf.as_mut_ptr(), buf.len()), -1);

        let mut tiny = [0x7f as c_char; 4];
        assert_eq!(
            cd_param_choice(1, 4, tiny.as_mut_ptr(), tiny.len()),
            -1,
            "Mixolydian does not fit"
        );
        assert_eq!(tiny[0], 0, "and the buffer is left an empty string");
        assert_eq!(cd_param_key(0, null_mut(), 8), -1);
        assert_eq!(cd_param_key(0, buf.as_mut_ptr(), 0), -1);
    }
}

#[test]
fn null_handles_are_refused_everywhere() {
    unsafe {
        cd_shell_destroy(null_mut());
        cd_shell_post_sample_rate(null(), 48000.0);
        assert!(cd_shell_begin(null()).is_null());
        cd_shell_end(null(), 64);
        assert!(!cd_shell_read(null(), null_mut()));
        assert_eq!(cd_shell_drain(null(), null_mut(), 4), 0);
        cd_core_begin_block(null_mut(), 1, 0.0, 120.0, 4, 4, 1);
        cd_core_on_midi(null_mut(), [0x90, 60, 1].as_ptr(), 3, 0);
        cd_core_set_param(null_mut(), 0, 1);
        cd_core_reset(null_mut());
        cd_core_end_block(null_mut(), 64);

        let shell = cd_shell_create(-1.0);
        assert!(!cd_shell_read(shell, null_mut()));
        assert_eq!(cd_shell_drain(shell, null_mut(), 4), 0);
        let core = cd_shell_begin(shell);
        cd_core_on_midi(core, null(), 3, 0);
        cd_core_on_midi(core, [0x90, 60, 1].as_ptr(), 0, 0);
        cd_core_set_param(core, 99, 1);
        cd_shell_end(shell, 64);
        cd_shell_post_sample_rate(shell, f64::NAN);
        cd_shell_post_sample_rate(shell, 0.0);
        cd_shell_post_sample_rate(shell, 96000.0);
        assert_eq!(read(shell).kind, 0);
        cd_shell_destroy(shell);
    }
}

#[test]
fn a_host_without_a_clock_still_moves_the_history() {
    unsafe {
        let shell = cd_shell_create(48000.0);
        let core = cd_shell_begin(shell);
        cd_core_begin_block(core, 0, f64::NAN, 0.0, 0, 0, 0);
        cd_core_end_block(core, 24000);
        cd_shell_end(shell, 24000);
        let r = read(shell);
        assert_eq!(r.playing, 0);
        assert_eq!(r.bpm, 120.0);
        assert_eq!(r.now, 1.0);
        assert_eq!(r.bar, 4.0);
        cd_shell_destroy(shell);
    }
}

#[test]
fn text_that_does_not_fit_stops_at_the_last_whole_piece() {
    let mut buf = [0x7f as c_char; 6];
    let mut w = CText::new(&mut buf);
    assert!(core::fmt::Write::write_str(&mut w, "C3").is_ok());
    assert!(core::fmt::Write::write_str(&mut w, " E3").is_ok());
    assert!(core::fmt::Write::write_str(&mut w, " G3").is_err());
    assert_eq!(text(&buf), "C3 E3");
    let mut empty: [c_char; 0] = [];
    assert!(core::fmt::Write::write_str(&mut CText::new(&mut empty), "x").is_err());
}
