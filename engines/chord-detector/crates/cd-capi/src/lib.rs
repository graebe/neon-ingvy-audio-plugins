// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The `cd_*` C ABI: NI Chord-Detector's engine as its JUCE shell holds it.

WHAT THIS CRATE IS ALLOWED TO DO: null-check, bound-check, copy into C memory,
and hand the call to `cd-core`. What a chord is called is decided there.

THE HEADER IS GENERATED. `build.rs` runs cbindgen over this file and writes
`engines/chord-detector/include/cd_capi.h`; the C test compiles against it and
links this archive, so a declaration that drifted from its definition would
have to drift in cbindgen first.

THREE THREADS AND WHAT EACH MAY CALL:

  any              cd_param_*                    the parameter table
  the audio thread cd_shell_begin, cd_core_*,    one block, in that order:
                   cd_shell_end                  begin, clock, MIDI, params,
                                                 end of block, end
  the message      cd_shell_read,                the latest reading; the note
  thread           cd_shell_drain                events since the last drain
  construction     cd_shell_create, _destroy,
                   cd_shell_post_sample_rate

THE WAY OUT IS TWO PATHS, because the two things an editor shows age
differently. What is sounding and what it is called is a STATE: only the latest
matters, so it is a frame in shell-core's triple buffer (`CdReading`), and a
reader that is slower than the audio thread skips frames it would have
overdrawn anyway. The history is a STREAM: every start and stop matters, so the
events go through an rtrb ring (`CdNoteEvent`), and one that does not fit is
counted, never silently lost.

EVERY ENTRY POINT THE AUDIO THREAD CALLS IS ALLOCATION-FREE. The workspace
sets `panic = "abort"`, because unwinding out of `extern "C"` is undefined.
*/

use std::os::raw::c_char;
use std::sync::Mutex;

use cd_core::text::{write_chord, write_degree, write_description, write_name, write_notes};
use cd_core::{Detector, Names, NoteEvent, Param, Transport, PARAM_COUNT};
use music_core::{Key, Mode, Note, Pitch, Spelling};
use shell_core::{publish_every, Bridge, Model};

#[cfg(test)]
mod tests;

/* --------------------------------------------------------------- the frame */

/// The longest readout, in bytes with its NUL.
pub const CD_NAME_MAX: usize = 32;
/// The longest line under it.
pub const CD_DESCRIPTION_MAX: usize = 96;
/// The longest roman numeral.
pub const CD_DEGREE_MAX: usize = 16;
/// The longest list of sounding notes. Sixty notes fit; more end the list
/// early, which a display has no room for anyway.
pub const CD_NOTES_MAX: usize = 320;
/// The longest key name, "F# Mixolydian".
pub const CD_KEY_MAX: usize = 24;
/// The most alternative readings.
pub const CD_ALTERNATIVES: usize = 8;
/// The longest alternative reading.
pub const CD_ALTERNATIVE_MAX: usize = 24;

/// What the editor draws from, published by the audio thread.
///
/// Every text is UTF-8 and NUL-terminated. Note sets are 128 bits, two words,
/// note `n` at bit `n % 64` of word `n / 64`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct CdReading {
    /// Changes whenever anything below the clock does.
    pub serial: u32,
    /// 0 empty, 1 note, 2 interval, 3 chord, 4 a set with no name.
    pub kind: u8,
    /// 1 when the reading is shown because Hold kept it after release.
    pub held: u8,
    /// 1 while the sustain pedal is down.
    pub pedal: u8,
    /// 1 while the host's transport plays.
    pub playing: u8,
    /// The chord's root as a pitch class, or -1.
    pub root: i8,
    /// The reading's lowest note, or -1.
    pub bass: i8,
    /// The reading's pitch classes, C at bit 0.
    pub pitch_classes: u16,
    /// The notes the reading was taken from. Under Hold, after release, these
    /// are the held chord's.
    pub notes: [u64; 2],
    /// The notes sounding now.
    pub sounding: [u64; 2],
    /// The key: tonic 0-11, mode 0-6 (Ionian to Locrian), its signature (-5
    /// flats to 6 sharps), its pitch classes, and the Spelling choice the
    /// texts were written with (0 the key's way, 1 sharps, 2 flats) -- what
    /// `cd_write_note` takes to name the history's notes the same way.
    pub tonic: u8,
    pub mode: u8,
    pub signature: i8,
    pub spelling: u8,
    pub scale: u16,
    /// How many of `alternatives` are filled.
    pub alternative_count: u8,
    /// The history's clock: now, in quarters; its tempo; the bar's length in
    /// quarters; and a point on the clock where a bar begins.
    pub now: f64,
    pub bpm: f64,
    pub bar: f64,
    pub bar_origin: f64,
    /// Note events that did not fit the ring since the instance began.
    pub dropped: u32,
    pub name: [c_char; CD_NAME_MAX],
    pub description: [c_char; CD_DESCRIPTION_MAX],
    pub degree: [c_char; CD_DEGREE_MAX],
    pub notes_text: [c_char; CD_NOTES_MAX],
    pub key_name: [c_char; CD_KEY_MAX],
    pub alternatives: [[c_char; CD_ALTERNATIVE_MAX]; CD_ALTERNATIVES],
}

impl CdReading {
    fn empty() -> CdReading {
        CdReading {
            serial: u32::MAX,
            kind: 0,
            held: 0,
            pedal: 0,
            playing: 0,
            root: -1,
            bass: -1,
            pitch_classes: 0,
            notes: [0; 2],
            sounding: [0; 2],
            tonic: 0,
            mode: 0,
            signature: 0,
            spelling: 0,
            scale: 0,
            alternative_count: 0,
            now: 0.0,
            bpm: 0.0,
            bar: 0.0,
            bar_origin: 0.0,
            dropped: 0,
            name: [0; CD_NAME_MAX],
            description: [0; CD_DESCRIPTION_MAX],
            degree: [0; CD_DEGREE_MAX],
            notes_text: [0; CD_NOTES_MAX],
            key_name: [0; CD_KEY_MAX],
            alternatives: [[0; CD_ALTERNATIVE_MAX]; CD_ALTERNATIVES],
        }
    }
}

/// One note starting (`velocity` > 0) or stopping (`velocity` 0), at `at`
/// quarters on the history's clock.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CdNoteEvent {
    pub at: f64,
    pub note: u8,
    pub velocity: u8,
}

/* ---------------------------------------------------------------- the text */

/// A `core::fmt::Write` over a C buffer that always leaves it NUL-terminated
/// and stops at the last whole piece that fits.
struct CText<'a> {
    buf: &'a mut [c_char],
    len: usize,
}

impl<'a> CText<'a> {
    fn new(buf: &'a mut [c_char]) -> CText<'a> {
        if let Some(first) = buf.first_mut() {
            *first = 0;
        }
        CText { buf, len: 0 }
    }
}

impl core::fmt::Write for CText<'_> {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        let end = self.len + s.len();
        if end + 1 > self.buf.len() {
            return Err(core::fmt::Error);
        }
        for (dst, src) in self.buf[self.len..end].iter_mut().zip(s.bytes()) {
            *dst = src as c_char;
        }
        self.buf[end] = 0;
        self.len = end;
        Ok(())
    }
}

fn split(set: u128) -> [u64; 2] {
    [set as u64, (set >> 64) as u64]
}

/* --------------------------------------------------------------- the model */

/// The ring's size: a second of notes at a busy 1000 events a second, read
/// sixty times a second.
const EVENTS: usize = 1024;
/// Commands between two blocks. The sample rate is the only one.
const QUEUE: usize = 16;

/// The opaque engine handle the audio thread holds between begin and end.
pub struct CdCore {
    detector: Detector,
    events: rtrb::Producer<CdNoteEvent>,
    dropped: u32,
    /// What the last published frame showed: the reading's serial, the keys
    /// sounding, the pedal and the drop count.
    shown: (u32, u128, bool, u32),
}

impl CdCore {
    /// Whether anything a frame shows moved since the last publish.
    ///
    /// MIDI and parameters reach the engine directly, not as bridge commands,
    /// so the bridge cannot see that they changed anything and would publish
    /// only when its cadence came round -- a chord would reach the editor up
    /// to a publish interval late. The keys are part of it because under Hold
    /// they move while the reading does not.
    fn changed(&mut self) -> bool {
        let d = &self.detector;
        let now = (d.serial(), d.sounding(), d.pedal(), self.dropped);
        let changed = now != self.shown;
        self.shown = now;
        changed
    }

    /// The engine, and where the notes it starts and stops go: into the ring,
    /// or counted when the ring is full. Split so both can be borrowed at once.
    fn parts(&mut self) -> (&mut Detector, impl FnMut(NoteEvent) + '_) {
        let CdCore {
            detector,
            events,
            dropped,
            ..
        } = self;
        let sink = move |e: NoteEvent| {
            let event = CdNoteEvent {
                at: e.at,
                note: e.note,
                velocity: e.velocity,
            };
            if events.push(event).is_err() {
                *dropped = dropped.wrapping_add(1);
            }
        };
        (detector, sink)
    }
}

/// What another thread may ask of the engine.
#[derive(Clone, Copy)]
pub enum Command {
    SampleRate(f64),
}

impl Model for CdCore {
    type Command = Command;
    type Frame = CdReading;

    fn new_frame(&self) -> CdReading {
        CdReading::empty()
    }

    fn apply(&mut self, cmd: &Command) {
        match *cmd {
            Command::SampleRate(rate) => self.detector.set_sample_rate(rate),
        }
    }

    /// The clock moves every block; the texts are rewritten only when the
    /// serial says the reading changed -- at most three times per change, once
    /// for each of the triple buffer's frames.
    fn publish(&self, f: &mut CdReading) {
        let d = &self.detector;
        let tl = d.timeline();
        f.playing = tl.playing() as u8;
        f.now = tl.now();
        f.bpm = tl.bpm();
        f.bar = tl.bar();
        f.bar_origin = tl.bar_origin();
        f.dropped = self.dropped;
        f.pedal = d.pedal() as u8;
        f.sounding = split(d.sounding());

        let serial = d.serial();
        let names = d.names();
        let spelling_code = d.param(Param::Spelling);
        let key = d.key();
        if f.serial == serial
            && f.spelling == spelling_code
            && f.tonic == key.tonic().value()
            && f.mode == key.mode().index()
        {
            return;
        }

        let r = d.reading();
        f.serial = serial;
        f.kind = r.kind() as u8;
        f.held = r.is_held() as u8;
        f.root = r.chord().map_or(-1, |c| c.root().value() as i8);
        f.bass = r.bass().map_or(-1, |n| n.midi() as i8);
        f.pitch_classes = r.pitch_classes().bits();
        f.notes = split(r.notes());
        f.tonic = key.tonic().value();
        f.mode = key.mode().index();
        f.signature = key.signature();
        f.spelling = spelling_code;
        f.scale = key.pitch_set().bits();

        let _ = write_name(r, names, &mut CText::new(&mut f.name));
        let _ = write_description(r, names, &mut CText::new(&mut f.description));
        let _ = write_degree(r, &mut CText::new(&mut f.degree));
        let _ = write_notes(r, names, &mut CText::new(&mut f.notes_text));
        let _ = core::fmt::write(&mut CText::new(&mut f.key_name), format_args!("{key}"));

        let mut count = 0;
        for (slot, chord) in f.alternatives.iter_mut().zip(r.alternatives()) {
            let _ = write_chord(chord, names, &mut CText::new(slot));
            count += 1;
        }
        for slot in f.alternatives.iter_mut().skip(count) {
            slot[0] = 0;
        }
        f.alternative_count = count as u8;
    }

    /* No view is kept: every parameter reaches the engine from the shell on
     * the audio thread, so a reader is answered from the latest frame. */
    fn restore(&mut self, _: &CdReading) {}
}

/* --------------------------------------------------------------- the shell */

/// The handle a plugin instance holds.
pub struct CdShell {
    bridge: Bridge<CdCore>,
    /// The message thread's end of the event ring. The audio thread never
    /// touches this lock; it only keeps two message-thread callers apart.
    events: Mutex<rtrb::Consumer<CdNoteEvent>>,
}

/// A new engine at `sample_rate` (44.1 kHz if that is not positive), every
/// parameter at its default. Construction only: it allocates.
#[no_mangle]
pub extern "C" fn cd_shell_create(sample_rate: f64) -> *mut CdShell {
    let rate = if sample_rate > 0.0 {
        sample_rate
    } else {
        44100.0
    };
    let (producer, consumer) = rtrb::RingBuffer::new(EVENTS);
    let detector = Detector::new(rate);
    let shown = (detector.serial(), 0, false, 0);
    let core = CdCore {
        detector,
        events: producer,
        dropped: 0,
        shown,
    };
    let bridge = Bridge::new(core, None, QUEUE, publish_every(rate));
    Box::into_raw(Box::new(CdShell {
        bridge,
        events: Mutex::new(consumer),
    }))
}

/// # Safety
/// `shell` is null or from `cd_shell_create`, and no thread uses it after.
#[no_mangle]
pub unsafe extern "C" fn cd_shell_destroy(shell: *mut CdShell) {
    if !shell.is_null() {
        drop(Box::from_raw(shell));
    }
}

/// The host's rate, applied at the top of the next block. Not the audio
/// thread. A rate that is not positive is ignored.
///
/// # Safety
/// `shell` is null or live.
#[no_mangle]
pub unsafe extern "C" fn cd_shell_post_sample_rate(shell: *const CdShell, sample_rate: f64) {
    let Some(shell) = shell.as_ref() else { return };
    if sample_rate.is_nan() || sample_rate <= 0.0 {
        return;
    }
    shell.bridge.post(Command::SampleRate(sample_rate));
    shell.bridge.set_publish_every(publish_every(sample_rate));
}

/// The audio thread, at the top of a block: the engine, for this block only.
///
/// # Safety
/// `shell` is null or live; the audio thread only, one block at a time; the
/// pointer is not used after `cd_shell_end`.
#[no_mangle]
pub unsafe extern "C" fn cd_shell_begin(shell: *const CdShell) -> *mut CdCore {
    match shell.as_ref() {
        Some(shell) => shell.bridge.begin() as *mut CdCore,
        None => std::ptr::null_mut(),
    }
}

/// The audio thread, at the end of a block of `frames`: publishes a frame
/// when one is due.
///
/// # Safety
/// As `cd_shell_begin`, after it.
#[no_mangle]
pub unsafe extern "C" fn cd_shell_end(shell: *const CdShell, frames: u32) {
    let Some(shell) = shell.as_ref() else { return };
    /* The engine again, inside the same block: `begin` hands back the one the
     * caller has finished with (applying any command posted since, which this
     * frame then includes). See CdCore::changed. */
    if shell.bridge.begin().changed() {
        shell.bridge.touch();
    }
    shell.bridge.end(frames);
}

/// Copy the latest reading into `out`. Not the audio thread. Returns false
/// when there is nothing to copy into or from.
///
/// # Safety
/// `shell` is null or live; `out` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn cd_shell_read(shell: *const CdShell, out: *mut CdReading) -> bool {
    let (Some(shell), Some(out)) = (shell.as_ref(), out.as_mut()) else {
        return false;
    };
    shell.bridge.read(|r| *out = *r.frame);
    true
}

/// Move up to `max` note events, oldest first, into `out`; returns how many.
/// The message thread.
///
/// # Safety
/// `shell` is null or live; `out` is null or has room for `max` events.
#[no_mangle]
pub unsafe extern "C" fn cd_shell_drain(
    shell: *const CdShell,
    out: *mut CdNoteEvent,
    max: usize,
) -> usize {
    let Some(shell) = shell.as_ref() else {
        return 0;
    };
    if out.is_null() || max == 0 {
        return 0;
    }
    let Ok(mut events) = shell.events.lock() else {
        return 0;
    };
    let mut n = 0;
    while n < max {
        let Ok(event) = events.pop() else { break };
        out.add(n).write(event);
        n += 1;
    }
    n
}

/* ---------------------------------------------- the audio thread, per block */

/// The host's clock for this block. `has_clock` 0 means the host reported
/// nothing; the history then runs at its last tempo.
///
/// # Safety
/// `core` is null or from `cd_shell_begin`, inside that block.
#[no_mangle]
pub unsafe extern "C" fn cd_core_begin_block(
    core: *mut CdCore,
    has_clock: i32,
    ppq: f64,
    bpm: f64,
    num: i32,
    den: i32,
    playing: i32,
) {
    let Some(core) = core.as_mut() else { return };
    let t = Transport {
        playing: playing != 0,
        ppq,
        bpm,
        num,
        den,
    };
    core.detector
        .begin_block(if has_clock != 0 { Some(&t) } else { None });
}

/// One MIDI message of `len` bytes, `offset` samples into the block.
///
/// # Safety
/// `core` as `cd_core_begin_block`; `bytes` is null or holds `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn cd_core_on_midi(
    core: *mut CdCore,
    bytes: *const u8,
    len: usize,
    offset: u32,
) {
    let Some(core) = core.as_mut() else { return };
    if bytes.is_null() || len == 0 {
        return;
    }
    let msg = std::slice::from_raw_parts(bytes, len);
    let (detector, sink) = core.parts();
    detector.on_midi(msg, offset, sink);
}

/// Set parameter `index` to `choice` (clamped). Cheap when nothing changes,
/// so a shell may pass every parameter every block.
///
/// # Safety
/// `core` as `cd_core_begin_block`.
#[no_mangle]
pub unsafe extern "C" fn cd_core_set_param(core: *mut CdCore, index: i32, choice: i32) {
    let Some(core) = core.as_mut() else { return };
    if let Some(p) = Param::from_i32(index) {
        core.detector.set_param(p, choice);
    }
}

/// A panic from the shell: every note stops and a held reading clears.
///
/// # Safety
/// `core` as `cd_core_begin_block`.
#[no_mangle]
pub unsafe extern "C" fn cd_core_reset(core: *mut CdCore) {
    let Some(core) = core.as_mut() else { return };
    let (detector, sink) = core.parts();
    detector.reset(sink);
}

/// The bottom of the block: the history's clock moves on by `frames`.
///
/// # Safety
/// `core` as `cd_core_begin_block`, before `cd_shell_end`.
#[no_mangle]
pub unsafe extern "C" fn cd_core_end_block(core: *mut CdCore, frames: u32) {
    if let Some(core) = core.as_mut() {
        core.detector.end_block(frames as usize);
    }
}

/* ------------------------------------------------------ the parameter table */

/// What a shell declares one parameter with.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CdParamInfo {
    pub choice_count: i32,
    pub default_choice: i32,
    pub automatable: bool,
}

/// How many parameters there are.
#[no_mangle]
pub extern "C" fn cd_param_count() -> i32 {
    PARAM_COUNT as i32
}

/// Describe parameter `index`. Any thread. False for an index out of range.
///
/// # Safety
/// `out` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn cd_param_info(index: i32, out: *mut CdParamInfo) -> bool {
    let (Some(p), Some(out)) = (Param::from_i32(index), out.as_mut()) else {
        return false;
    };
    let info = p.info();
    *out = CdParamInfo {
        choice_count: info.choices.len() as i32,
        default_choice: info.default as i32,
        automatable: info.automatable,
    };
    true
}

unsafe fn copy_text(text: &str, out: *mut c_char, cap: usize) -> i32 {
    if out.is_null() || cap == 0 {
        return -1;
    }
    let buf = std::slice::from_raw_parts_mut(out, cap);
    let mut w = CText::new(buf);
    match core::fmt::Write::write_str(&mut w, text) {
        Ok(()) => text.len() as i32,
        Err(_) => -1,
    }
}

/// Parameter `index`'s stable key, as a saved state names it, into `out`.
/// Returns its length, or -1 when the index is out of range or `cap` too
/// small. Any thread.
///
/// # Safety
/// `out` is null or has room for `cap` bytes.
#[no_mangle]
pub unsafe extern "C" fn cd_param_key(index: i32, out: *mut c_char, cap: usize) -> i32 {
    match Param::from_i32(index) {
        Some(p) => copy_text(p.info().key, out, cap),
        None => -1,
    }
}

/// Parameter `index`'s display name, as `cd_param_key`.
///
/// # Safety
/// As `cd_param_key`.
#[no_mangle]
pub unsafe extern "C" fn cd_param_name(index: i32, out: *mut c_char, cap: usize) -> i32 {
    match Param::from_i32(index) {
        Some(p) => copy_text(p.info().name, out, cap),
        None => -1,
    }
}

/// The label of choice `choice` of parameter `index`, as `cd_param_key`.
///
/// # Safety
/// As `cd_param_key`.
#[no_mangle]
pub unsafe extern "C" fn cd_param_choice(
    index: i32,
    choice: i32,
    out: *mut c_char,
    cap: usize,
) -> i32 {
    let label = Param::from_i32(index).and_then(|p| {
        usize::try_from(choice)
            .ok()
            .and_then(|c| p.info().choices.get(c))
    });
    match label {
        Some(label) => copy_text(label, out, cap),
        None => -1,
    }
}

/* ------------------------------------------------------- naming a note */

/// How a score writes one note: what a staff needs to place it.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CdWrittenNote {
    /// The letter, 0 C to 6 B.
    pub letter: u8,
    /// Sharps positive, flats negative, -2 to 2.
    pub accidental: i8,
    /// The written octave: C flat 4 sounds as B 3.
    pub octave: i16,
    /// Letter steps up from C in octave 0: one per line and per space.
    pub staff_step: i32,
    /// The name with its octave, `Bb3`.
    pub name: [c_char; 8],
}

/// How MIDI note `midi` is written in the key `tonic` (0-11) / `mode` (0-6)
/// under Spelling choice `spelling` (0 the key's way, 1 sharps, 2 flats) --
/// the same naming the reading's texts use. Any thread; a pure function.
/// Out-of-range values are clamped.
#[no_mangle]
pub extern "C" fn cd_write_note(tonic: i32, mode: i32, spelling: i32, midi: i32) -> CdWrittenNote {
    let key = Key::new(
        Pitch::new(tonic.clamp(0, 11)),
        Mode::from_index(mode.clamp(0, 6) as u8).unwrap_or_default(),
    );
    let names = match spelling {
        1 => Names::Fixed(Spelling::Sharps),
        2 => Names::Fixed(Spelling::Flats),
        _ => Names::Key(key),
    };
    let note = Note::from_midi(midi.clamp(0, 127) as i16);
    let name = names.of(note.pitch());
    let mut out = CdWrittenNote {
        letter: name.letter().index(),
        accidental: name.accidental(),
        octave: name.octave_of(note),
        staff_step: name.staff_step(note),
        name: [0; 8],
    };
    let _ = names.write_note(note, &mut CText::new(&mut out.name));
    out
}
