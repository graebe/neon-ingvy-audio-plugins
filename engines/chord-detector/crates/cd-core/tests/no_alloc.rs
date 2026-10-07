// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The audio thread allocates nothing, asserted rather than claimed.
 *
 * Everything a shell calls on the audio callback is in here: MIDI of every
 * kind (notes, the pedal, a panic, what is refused), the block's clock, every
 * parameter, and every text a frame is filled with -- the names come from
 * music-core's Display through a fixed buffer, and a String anywhere on that
 * path would show up here.
 *
 * The guard is assert_no_alloc's, and it watches one thread: inside the
 * closure, an allocation or a free on the thread that runs it is a violation.
 * It counts rather than aborts (warn_debug, warn_release), so the assertion
 * can say how many.
 */

use cd_core::text::{write_chord, write_degree, write_description, write_name, write_notes};
use cd_core::{Detector, Param, Transport, PARAM_COUNT};
use music_core::DisplayBuffer;

use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

const MIDI: &[&[u8]] = &[
    &[0x90, 60, 100],
    &[0x90, 64, 100],
    &[0x91, 67, 90],
    &[0xB0, 64, 127], /* the pedal down */
    &[0x80, 64, 0],
    &[0x90, 70, 0],    /* a note-on at velocity 0 */
    &[0xB0, 64, 0],    /* the pedal up */
    &[0x90, 36, 0x80], /* refused: a status byte as velocity */
    &[0x90, 36],       /* cut short */
    &[0xF0, 0x7E, 0x7F, 0xF7],
    &[0xF8],
    &[0xB0, 123, 0], /* a panic */
];

#[test]
fn midi_clock_params_and_texts_allocate_nothing() {
    let mut d = Detector::new(48000.0); /* allocates nothing either, but may */
    let mut name = DisplayBuffer::<64>::new();
    let mut words = DisplayBuffer::<128>::new();
    let mut numeral = DisplayBuffer::<16>::new();
    let mut notes = DisplayBuffer::<512>::new();
    let mut other = DisplayBuffer::<32>::new();
    let mut events = 0usize;

    assert_no_alloc(|| {
        for block in 0..256usize {
            let t = Transport {
                playing: block % 32 < 24,
                ppq: block as f64 * 0.125,
                bpm: 100.0 + (block % 7) as f64,
                num: 3 + (block % 2) as i32,
                den: 4,
            };
            d.begin_block(Some(&t));
            for (i, msg) in MIDI.iter().enumerate() {
                d.on_midi(msg, (i * 17) as u32, |_| events += 1);
            }
            for n in 0..24u8 {
                d.on_midi(&[0x90, 30 + n * 3, 64], 0, |_| events += 1);
            }
            d.set_param(
                Param::from_i32((block % PARAM_COUNT) as i32).unwrap(),
                block as i32 % 13,
            );
            let r = *d.reading();
            let names = d.names();
            let _ = write_name(&r, names, &mut name);
            let _ = write_description(&r, names, &mut words);
            let _ = write_degree(&r, &mut numeral);
            let _ = write_notes(&r, names, &mut notes);
            for chord in r.alternatives() {
                let _ = write_chord(chord, names, &mut other);
            }
            if block % 64 == 63 {
                d.reset(|_| events += 1);
            }
            d.end_block(256);
        }
    });

    let n = violation_count();
    assert_eq!(n, 0, "the audio path allocated or freed {n} times");
    assert!(events > 0);
}
