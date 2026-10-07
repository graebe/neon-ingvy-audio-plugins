// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The audio thread allocates nothing, asserted rather than claimed.
 *
 * Every entry point of this engine may run on an audio callback -- on the
 * Move, set_param and get_param as much as process -- so "no allocation
 * outside Instance::new" is a claim about all three. It stays true only until
 * somebody collects a String in a parser that looked harmless;
 * `rates::index_from` did exactly that for a numeric rate, on every write from
 * the Move's knob.
 *
 * WHAT AN EDIT, A LOAD, AN IMPORT OR A PASTE HANDS THE AUDIO THREAD is a
 * value read elsewhere -- an Edit, a Patch, a SlotFile, a Clip -- and applying
 * one is measured here. So are the text doors that read and apply in one
 * call, with the texts a build writes. And set_param("state") with every text
 * a damaged patch could hold: it is what the Move's set_param reads on its
 * audio callback, whatever Schwung restores, so a blob it refuses must be
 * refused without the error serde_json would box (see the state module's note
 * on threads). An import or a paste of a refused text is not measured: the
 * Move has neither, and the plugin's shell reads both as they are posted, on
 * its main thread.
 *
 * The allocator counts allocations AND frees: a free is the same lock as a
 * malloc, and a temporary that is allocated before the window and dropped
 * inside it is still a lock taken on the audio thread.
 *
 * THIS FILE MUST HOLD EXACTLY ONE TEST. The counter is global and cargo runs
 * tests in threads, so a second test in this binary could allocate inside the
 * measured window and the failure would look like a real regression.
 */

use tg_core::edit::Edit;
use tg_core::params::Param;
use tg_core::paste::Clip;
use tg_core::slotfile::{Kind, SlotFile};
use tg_core::state::Patch;
use tg_core::{Instance, Transport};

#[global_allocator]
static ALLOCATOR: ni_testkit::Counting = ni_testkit::Counting;

/* A state blob damaged every way that matters to its reader: empty, not an
 * object, unfinished, mistyped, escaped, nested too deep, a number no double
 * holds -- each a text serde_json would refuse or copy. Every prefix and
 * suffix of a real blob is measured too, below. */
const DAMAGED: &[&str] = &[
    "",
    "   ",
    "null",
    "7",
    "\"state\"",
    "[{\"sv\":7}]",
    "{\"sv\":7,",
    "{\"sv\":}",
    "{\"sv\":\"7\"",
    "{\"sv\":7,}",
    "{\"sv\":7}}",
    "{\"sv\":07}",
    "{\"sv\":7} trailing",
    "{\"sv\":7,\"rate\":\"1\\/16\"}",
    "{\"sv\":7,\"r\\u0061te\":\"1/16\"}",
    "{\"sv\":7,\"x\":[[[1]]]}",
    "{\"sv\":7,\"x\":{\"y\":{\"z\":{}}}}",
    "{\"sv\":1e999}",
    "{\"sv\":7,\"amount\":tru}",
];

/* Every key the string door serves, with a value of each shape it takes. */
const SETS: &[(&str, &str)] = &[
    ("rate", "1/32T"),
    ("rate", "3"),
    ("rate", " 11 junk"),
    ("rate", "-4"),
    ("slot", "2"),
    ("length", "31"),
    ("attack", "12.5"),
    ("decay", "40"),
    ("sustain", "0.4"),
    ("hold", "0.6"),
    ("release", "30"),
    ("amount", "0.8"),
    ("cursor", "5"),
    ("step", "Tie"),
    ("step", "1"),
    ("step_amount", "0.5"),
    ("step_order", "2"),
    ("fade", "0.7"),
    ("fade_soft", "On"),
    ("fade_dir", "Out"),
    ("randomize", "Roll"),
    ("randomize", "1234"),
    ("legato", "1"),
    ("curve", "S-Curve"),
    ("time_mode", "%"),
    ("pattern", "F0F0"),
    ("ties", "0100"),
    ("slot", "0"),
];

const GETS: &[&str] = &[
    "name", "slot", "length", "rate", "attack", "decay", "sustain", "release", "hold",
    "amount", "legato", "fade", "fade_soft", "fade_dir", "step_order", "time_mode", "curve",
    "ms_per_step", "width_ms", "cursor", "step", "step_amount", "pattern", "ties", "phase",
    "phase:effective", "params", "ui", "state", "no-such-key",
];

#[test]
fn process_set_param_and_get_param_allocate_nothing() {
    let mut p = Instance::new(44100.0); /* allocates, and is allowed to */

    /* Everything the measured window touches is built before it opens. */
    let mut inter = vec![0.25f32; 256 * 2];
    let mut l = vec![0.25f32; 256];
    let mut r = vec![0.25f32; 256];
    let mut i16s = vec![8000i16; 256 * 2];
    let mut out = vec![0u8; 8192];
    /* Slot 6 sounds different in everything, so the switches below recall a
     * whole sound -- glides, re-anchor, rate and fade weights included -- and
     * the blob carries an `s<N>` to read. */
    for (k, v) in [("rate", "1/8T"), ("curve", "Exp"), ("amount", "0.3"), ("fade", "0.4"), ("hold", "0.5")] {
        p.set_param("slot", "5");
        p.set_param(k, v);
    }
    p.set_param("slot", "0");
    let mut state = vec![0u8; 8192];
    let n = p.get_param("state", &mut state) as usize;
    let state = String::from_utf8(state[..n].to_vec()).unwrap();
    let mut file = vec![0u8; 16 * 1024];
    let n = p.export(Kind::Bank, &mut file) as usize;
    let bank = String::from_utf8(file[..n].to_vec()).unwrap();
    let n = p.export(Kind::Slot, &mut file) as usize;
    let slot = String::from_utf8(file[..n].to_vec()).unwrap();
    /* What the main thread reads and the audio thread applies. */
    let patch = Patch::parse(&state).unwrap();
    let banked = SlotFile::parse(&bank).unwrap();
    let pasted = [Clip::parse(&slot).unwrap(), Clip::parse(&bank).unwrap(), Clip::parse(&state).unwrap()];
    let edits: Vec<Edit> = SETS.iter().filter_map(|&(k, v)| Edit::parse(k, v)).collect();

    ni_testkit::arm();
    let mut beats = 0.0;
    for block in 0..64 {
        let t = Transport { running: block % 16 != 15, beats, bpm: 123.0 };
        match block % 3 {
            0 => p.process_f32(&mut inter, 256, Some(&t)),
            1 => p.process_f32_split(&mut l, &mut r, 256, Some(&t)),
            _ => p.process_i16(&mut i16s, 256, Some(&t)),
        }
        beats += 256.0 / 44100.0 * 123.0 / 60.0;
        let (k, v) = SETS[block % SETS.len()];
        p.set_param(k, v);
        p.set_num(Param::from_i32((block % 15) as i32).unwrap(), 0.5);
        p.set_num(Param::Slot, if block % 2 == 0 { 5.0 } else { 0.0 });
        for k in GETS {
            p.get_param(k, &mut out);
        }
    }
    /* The ready values. */
    for edit in &edits {
        p.apply_edit(edit);
    }
    p.load(&patch);
    p.apply_file(3, &banked);
    for clip in &pasted {
        p.apply_clip(2, clip);
    }
    /* The text doors, with what a build writes... */
    p.set_param("state", &state);
    /* ...a newer build's nesting, under a key this one passes over... */
    p.set_param("state", "{\"sv\":7,\"future\":{\"x\":[1,2]},\"more\":[{},[]]}");
    /* ...and what a damaged patch holds. */
    for text in DAMAGED {
        p.set_param("state", text);
    }
    for at in (0..state.len()).filter(|&i| state.is_char_boundary(i)) {
        p.set_param("state", &state[..at]);
        p.set_param("state", &state[at..]);
    }
    let _ = p.import(&bank);
    let _ = p.import(&slot);
    let _ = p.paste(&slot);
    let _ = p.paste(&bank);
    let _ = p.paste(&state);
    p.export(Kind::Bank, &mut file);
    ni_testkit::disarm();

    let (a, f) = (ni_testkit::allocs(), ni_testkit::frees());
    assert_eq!((a, f), (0, 0), "the audio path allocated {a} times and freed {f} times");
}
