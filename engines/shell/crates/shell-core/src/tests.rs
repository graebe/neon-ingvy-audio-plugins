// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The queue, the triple buffer, the bridge and the handoff, one thread at a
 * time. The two-thread stress tests are in tests/stress.rs and the allocation
 * check in tests/no_alloc.rs, each its own binary.
 */

use super::*;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

/* ------------------------------------------------------------------ queue */

#[test]
fn queue_is_fifo_and_wraps() {
    let q = Queue::new(64, 16);
    let mut out = [0u8; 16];
    /* Enough rounds that records straddle the end of the ring many times. */
    for round in 0u8..200 {
        let a = [round; 5];
        let b = [round.wrapping_add(1); 11];
        unsafe {
            assert!(q.push(&[&a]));
            assert!(q.push(&[&b[..4], &b[4..]]));
            assert_eq!(q.pop(&mut out), Some(5));
            assert_eq!(&out[..5], &a);
            assert_eq!(q.pop(&mut out), Some(11));
            assert_eq!(&out[..11], &b);
            assert_eq!(q.pop(&mut out), None);
        }
    }
    assert!(q.is_empty());
}

#[test]
fn queue_refuses_what_does_not_fit() {
    let q = Queue::new(32, 8);
    assert_eq!(q.capacity(), 32);
    unsafe {
        assert!(!q.push(&[&[0u8; 9]]), "longer than max_record");
        let mut n = 0;
        while q.push(&[&[7u8; 8]]) {
            n += 1;
        }
        assert_eq!(n, 2, "two 12-byte records fit in 32, three do not");
        let mut out = [0u8; 8];
        assert_eq!(q.pop(&mut out), Some(8));
        assert!(q.push(&[&[7u8; 8]]), "room again once one is taken");
    }
}

#[test]
fn queue_empty_record_is_a_record() {
    let q = Queue::new(16, 4);
    let mut out = [0u8; 4];
    unsafe {
        assert!(q.push(&[]));
        assert_eq!(q.pop(&mut out), Some(0));
        assert_eq!(q.pop(&mut out), None);
    }
}

/* --------------------------------------------------------- triple buffer */

#[test]
fn triple_buffer_hands_over_the_latest_whole_value() {
    let t = TripleBuffer::new(|| 0u32);
    unsafe {
        *t.back() = 1;
        t.publish();
        *t.back() = 2;
        t.publish();
        let (v, fresh) = t.latest();
        assert_eq!((*v, fresh), (2, true), "a slow reader skips to the newest");
        let (v, fresh) = t.latest();
        assert_eq!((*v, fresh), (2, false), "and keeps it until there is another");
        *t.back() = 3;
        t.publish();
        assert_eq!(*t.latest().0, 3);
    }
}

/* ----------------------------------------------------------------- bridge */

/// A model whose state is a count and a sum of the commands applied, so any
/// lost, duplicated or reordered command shows up as a wrong number.
struct Tally {
    count: u64,
    sum: u64,
}

struct TallyFrame {
    count: u64,
    sum: u64,
    text: Text,
}

impl Model for Tally {
    type Frame = TallyFrame;
    fn new_frame(&self) -> TallyFrame {
        TallyFrame { count: 0, sum: 0, text: Text::new(32) }
    }
    fn apply(&mut self, cmd: &[u8]) {
        let mut b = [0u8; 8];
        b.copy_from_slice(&cmd[..8]);
        self.count += 1;
        self.sum += u64::from_le_bytes(b);
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
    }
}

fn tally() -> Bridge<Tally> {
    Bridge::new(
        Tally { count: 0, sum: 0 },
        Some(Tally { count: 0, sum: 0 }),
        256,
        8,
        1000,
    )
}

fn seen(b: &Bridge<Tally>) -> (u64, u64, bool) {
    b.read(|r| match r.pending {
        Some(v) => (v.count, v.sum, true),
        None => (r.frame.count, r.frame.sum, false),
    })
}

#[test]
fn a_command_reaches_the_engine_at_the_next_block() {
    let b = tally();
    assert_eq!(seen(&b), (0, 0, false), "the first frame is published at construction");
    assert!(b.post(&5u64.to_le_bytes()));
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
        b.post(&v.to_le_bytes());
        assert_eq!(seen(&b), (v, v * (v + 1) / 2, true));
    }
    assert_eq!(b.outstanding(), 10);
    unsafe {
        b.begin();
        b.end(1);
    }
    assert_eq!(seen(&b), (10, 55, false), "the engine caught up; the frame answers");
}

#[test]
fn a_full_queue_holds_commands_back_rather_than_losing_them() {
    /* 256 bytes of queue, 20 bytes a record: twelve fit, the rest wait. */
    let b = tally();
    for v in 1..=100u64 {
        b.post(&v.to_le_bytes());
    }
    assert_eq!(seen(&b), (100, 5050, true), "the view counts the ones still waiting");
    let mut blocks = 0;
    while b.outstanding() > 0 {
        unsafe {
            b.begin();
            b.end(1);
        }
        blocks += 1;
        assert!(blocks < 100, "the outbox drains");
    }
    assert!(blocks > 1, "it took more than one queue's worth");
    assert_eq!(seen(&b), (100, 5050, false));
}

#[test]
fn an_oversized_command_is_refused_up_front() {
    let b = tally();
    assert!(!b.post(&[0u8; 9]));
    assert_eq!(b.outstanding(), 0);
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
    unsafe {
        b.begin();
        b.end(1);
    }
    assert_eq!(seen(&b).0, 42, "due at 100 frames");
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
    let b = Bridge::new(Tally { count: 0, sum: 0 }, None, 256, 8, 1);
    b.post(&3u64.to_le_bytes());
    assert_eq!(seen(&b), (0, 0, false));
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
