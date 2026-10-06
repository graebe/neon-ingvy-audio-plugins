// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The bridge and the handoff, one thread at a time. The two-thread stress
 * tests are in tests/stress.rs and the allocation check in tests/no_alloc.rs,
 * each its own binary. The ring and the triple buffer are rtrb's and
 * triple_buffer's, and their own suites test them; what is tested here is what
 * this crate builds on them.
 */

use super::*;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

/* ----------------------------------------------------------------- bridge */

/// A model whose state is a count and a sum of the commands applied, so any
/// lost, duplicated or reordered command shows up as a wrong number -- and
/// which keeps, as a view, how much work answering its readers took.
#[derive(Default)]
struct Tally {
    count: u64,
    sum: u64,
    /// Commands applied, and restores from a frame, over the model's life.
    applies: u64,
    restores: u64,
}

#[derive(Clone)]
struct TallyFrame {
    count: u64,
    sum: u64,
    text: Text,
}

impl Model for Tally {
    type Command = u64;
    type Frame = TallyFrame;
    fn new_frame(&self) -> TallyFrame {
        TallyFrame { count: 0, sum: 0, text: Text::new(32) }
    }
    fn apply(&mut self, cmd: &u64) {
        self.count += 1;
        self.sum += cmd;
        self.applies += 1;
    }
    fn publish(&self, f: &mut TallyFrame) {
        f.count = self.count;
        f.sum = self.sum;
        let c = self.count;
        f.text.fill(|out| {
            out[0] = b'0' + (c % 10) as u8;
            1
        });
    }
    fn restore(&mut self, f: &TallyFrame) {
        self.count = f.count;
        self.sum = f.sum;
        self.restores += 1;
    }
}

/* Twelve commands fit the ring; the rest wait on the main side. */
fn tally() -> Bridge<Tally> {
    Bridge::new(Tally::default(), Some(Tally::default()), 12, 1000)
}

fn seen(b: &Bridge<Tally>) -> (u64, u64, bool) {
    b.read(|r| match r.pending {
        Some(v) => (v.count, v.sum, true),
        None => (r.frame.count, r.frame.sum, false),
    })
}

fn block(b: &Bridge<Tally>) {
    unsafe {
        b.begin();
        b.end(1);
    }
}

#[test]
fn a_command_reaches_the_engine_at_the_next_block() {
    let b = tally();
    assert_eq!(seen(&b), (0, 0, false), "the first frame is published at construction");
    b.post(5);
    unsafe {
        assert_eq!(b.begin().count, 1);
        b.end(1);
    }
    assert_eq!(seen(&b), (1, 5, false), "published because a command was applied");
    assert_eq!(b.outstanding(), 0);
}

#[test]
fn a_reader_sees_its_own_edits_with_no_audio_thread() {
    let b = tally();
    for v in 1..=10u64 {
        b.post(v);
        assert_eq!(seen(&b), (v, v * (v + 1) / 2, true));
    }
    assert_eq!(b.outstanding(), 10);
    block(&b);
    assert_eq!(seen(&b), (10, 55, false), "the engine caught up; the frame answers");
}

#[test]
fn with_no_audio_thread_a_read_applies_only_what_the_view_has_not_seen() {
    /* No frame comes to confirm anything, so the outbox only grows. A read
     * that replayed all of it every time made a session of N edits cost
     * N(N+1)/2 applies; it is N, from one restore. */
    let b = tally();
    let work = |b: &Bridge<Tally>| b.read(|r| r.pending.map(|v| (v.applies, v.restores)));
    for v in 1..=200u64 {
        b.post(v);
        assert_eq!(seen(&b), (v, v * (v + 1) / 2, true));
        assert_eq!(seen(&b), (v, v * (v + 1) / 2, true), "a second read applies nothing more");
    }
    assert_eq!(work(&b), Some((200, 1)));

    /* A new frame is a new starting point: the view is restored from it and
     * brought forward through what is still outstanding -- 188, the ring
     * having carried twelve. */
    block(&b);
    assert_eq!(seen(&b), (200, 20100, true));
    assert_eq!(work(&b), Some((200 + 188, 2)));
    b.post(201);
    assert_eq!(seen(&b), (201, 20301, true));
    assert_eq!(work(&b), Some((200 + 188 + 1, 2)), "and only the newest after that");
}

#[test]
fn a_full_ring_holds_commands_back_rather_than_losing_them() {
    let b = tally();
    for v in 1..=100u64 {
        b.post(v);
    }
    assert_eq!(seen(&b), (100, 5050, true), "the view counts the ones still waiting");
    let mut blocks = 0;
    while b.outstanding() > 0 {
        block(&b);
        blocks += 1;
        assert!(blocks < 100, "the outbox drains");
    }
    assert!(blocks > 1, "it took more than one ring's worth");
    assert_eq!(seen(&b), (100, 5050, false));
}

#[test]
fn the_cadence_is_a_hundred_publishes_a_second_at_any_rate() {
    assert_eq!(publish_every(48000.0), 480);
    assert_eq!(publish_every(96000.0), 960);
    /* A host that has not named its rate yet, or named nonsense. */
    for unknown in [0.0, -1.0, f64::NAN] {
        assert_eq!(publish_every(unknown), 441, "{unknown}");
    }
    assert_eq!(publish_every(50.0), 1, "never a period of no frames");
}

#[test]
fn the_frame_republishes_on_its_cadence() {
    let b = tally();
    b.set_publish_every(100);
    unsafe {
        b.begin().count = 42; /* a change no command made */
        b.end(99);
    }
    assert_eq!(seen(&b).0, 0, "not due yet");
    block(&b);
    assert_eq!(seen(&b).0, 42, "due at 100 frames");
}

#[test]
fn a_touched_block_publishes_off_cadence() {
    let b = tally();
    unsafe {
        b.begin().count = 7;
        b.touch();
        b.end(1);
    }
    assert_eq!(seen(&b).0, 7);
}

#[test]
fn a_model_with_no_view_is_always_answered_from_the_frame() {
    let b = Bridge::new(Tally::default(), None, 16, 1);
    b.post(3);
    assert_eq!(seen(&b), (0, 0, false));
}

#[test]
fn every_frame_of_the_triple_buffer_carries_a_publish() {
    /* A reader that reads after every block sees each publish in turn, as
     * the three frames go round -- none of them still holding an older one. */
    let b = tally();
    for v in 1..=9u64 {
        b.post(v);
        block(&b);
        assert_eq!(seen(&b), (v, v * (v + 1) / 2, false), "after block {v}");
    }
}

/* A payload that counts its own free, carried by every command. */
struct Payload(Arc<AtomicUsize>);

impl Drop for Payload {
    fn drop(&mut self) {
        self.0.fetch_add(1, Ordering::SeqCst);
    }
}

struct Holder;

impl Model for Holder {
    type Command = Shared<Payload>;
    type Frame = ();
    fn new_frame(&self) {}
    fn apply(&mut self, _: &Shared<Payload>) {}
    fn publish(&self, _: &mut ()) {}
    fn restore(&mut self, _: &()) {}
}

#[test]
fn a_payload_lives_until_the_engine_has_confirmed_it_and_no_longer() {
    let freed = Arc::new(AtomicUsize::new(0));
    let b = Bridge::new(Holder, None, 4, 1);
    for _ in 0..10 {
        b.post(Shared::new(b.handle(), Payload(Arc::clone(&freed))));
    }
    unsafe {
        b.begin();
        b.end(1);
    }
    assert_eq!(freed.load(Ordering::SeqCst), 0, "applied, but not yet confirmed to the main side");
    while b.outstanding() > 0 {
        unsafe {
            b.begin();
            b.end(1);
        }
    }
    assert_eq!(freed.load(Ordering::SeqCst), 10, "each freed once, by the collector");

    /* What is still in flight when the bridge goes is freed with it. */
    b.post(Shared::new(b.handle(), Payload(Arc::clone(&freed))));
    drop(b);
    assert_eq!(freed.load(Ordering::SeqCst), 11);
}

#[test]
fn text_copies_out_c_style() {
    let mut t = Text::new(8);
    let mut out = [0xFFu8; 4];
    assert_eq!(t.copy_to(&mut out), -1, "nothing written yet");
    t.fill(|b| {
        b[..5].copy_from_slice(b"hello");
        5
    });
    assert_eq!(t.as_str(), "hello");
    assert_eq!(t.copy_to(&mut out), 3);
    assert_eq!(&out, b"hel\0");
    t.fill(|_| -1);
    assert_eq!(t.get(), None);
}

/* ---------------------------------------------------------------- handoff */

/// A lent object that counts its own free. EACH TEST HAS ITS OWN COUNTER:
/// cargo runs these on parallel threads, and a shared one would let one
/// test's frees land in another's assertion.
struct Lent {
    id: u32,
    freed: Arc<AtomicUsize>,
}

impl Drop for Lent {
    fn drop(&mut self) {
        self.freed.fetch_add(1, Ordering::SeqCst);
    }
}

fn lent(id: u32, freed: &Arc<AtomicUsize>) -> Option<Lent> {
    Some(Lent { id, freed: Arc::clone(freed) })
}

#[test]
fn a_retired_object_outlives_the_block_that_holds_it() {
    let freed = Arc::new(AtomicUsize::new(0));
    let h = Handoff::new();
    h.set(lent(1, &freed));

    /* The audio thread, mid-block. */
    let held = unsafe { h.acquire() }.expect("an object is installed");
    assert_eq!(held.id, 1);
    h.set(lent(2, &freed));
    assert_eq!(h.collect(), 1, "still held: not freed");
    assert_eq!(freed.load(Ordering::SeqCst), 0);
    assert_eq!(held.id, 1, "and still readable");
    unsafe { h.release() };

    assert_eq!(h.collect(), 0);
    assert_eq!(freed.load(Ordering::SeqCst), 1);
    assert_eq!(unsafe { h.acquire() }.map(|o| o.id), Some(2), "the next block gets the new one");
    unsafe { h.release() };
    assert_eq!(h.with_current(|o| o.map(|o| o.id)), Some(2), "the main thread sees what it installed");

    h.set(None);
    assert!(unsafe { h.acquire() }.is_none(), "nothing installed is nothing lent");
    unsafe { h.release() };
    assert_eq!(h.collect(), 0);
    assert_eq!(freed.load(Ordering::SeqCst), 2);
}

#[test]
fn dropping_the_handoff_frees_everything_it_was_given() {
    /* The destructor's path: the live object and a retired one nobody
     * collected go with the handoff, each exactly once. */
    let freed = Arc::new(AtomicUsize::new(0));
    let h = Handoff::new();
    h.set(lent(1, &freed));
    unsafe { h.acquire() };
    h.set(lent(2, &freed));
    unsafe { h.release() };
    assert_eq!(freed.load(Ordering::SeqCst), 0, "not collected yet");
    drop(h);
    assert_eq!(freed.load(Ordering::SeqCst), 2);
}
