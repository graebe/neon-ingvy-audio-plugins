// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The bridge's way in and way out as one property: any interleaving of posts,
 * blocks and reads -- the two threads' calls, in any order one thread can make
 * them -- with any ring size and any cadence, against what the engine and a
 * reader must see.
 *
 * What it would catch: a command lost, duplicated or reordered between the
 * outbox, the ring and the engine (the engine's tally is always a prefix of
 * what was posted, in order); a frame that is not one the engine published,
 * or one older than a frame already read; a reader with a view shown anything
 * but every command posted so far; a payload freed twice, early or never.
 *
 * THE SEED IS FIXED, so the quick tier sees the same cases on every run: a
 * verdict that can change without a commit is not one. A failure writes no
 * regression file into the tree; it prints its smallest case, which the same
 * seed finds again.
 */

use proptest::prelude::*;
use proptest::test_runner::{Config, RngSeed};
use shell_core::{Bridge, Model, Shared};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

/// A command's payload, counting its own free.
struct Value {
    v: u64,
    freed: Arc<AtomicUsize>,
}

impl Drop for Value {
    fn drop(&mut self) {
        self.freed.fetch_add(1, Ordering::SeqCst);
    }
}

/// A count and a sum of what was applied: a lost, doubled or reordered
/// command shows up as a wrong number.
#[derive(Default)]
struct Tally {
    count: u64,
    sum: u64,
}

#[derive(Clone)]
struct TallyFrame {
    count: u64,
    sum: u64,
}

impl Model for Tally {
    type Command = Shared<Value>;
    type Frame = TallyFrame;
    fn new_frame(&self) -> TallyFrame {
        TallyFrame { count: 0, sum: 0 }
    }
    fn apply(&mut self, cmd: &Shared<Value>) {
        self.count += 1;
        self.sum += cmd.v;
    }
    fn publish(&self, f: &mut TallyFrame) {
        f.count = self.count;
        f.sum = self.sum;
    }
    fn restore(&mut self, f: &TallyFrame) {
        self.count = f.count;
        self.sum = f.sum;
    }
}

#[derive(Clone, Debug)]
enum Op {
    Post(u32),
    /// A block of this many frames.
    Block(u32),
    /// A block that touches, publishing whatever the cadence says.
    Touched(u32),
    Read,
}

fn op() -> impl Strategy<Value = Op> {
    prop_oneof![
        4 => any::<u32>().prop_map(Op::Post),
        2 => (0u32..200).prop_map(Op::Block),
        1 => (0u32..200).prop_map(Op::Touched),
        3 => Just(Op::Read),
    ]
}

fn sum(posted: &[u64], n: u64) -> u64 {
    posted[..n as usize].iter().sum()
}

fn config() -> Config {
    Config { cases: 512, rng_seed: RngSeed::Fixed(0x5EED_B41D), failure_persistence: None, ..Config::default() }
}

proptest! {
    #![proptest_config(config())]

    #[test]
    fn every_command_arrives_once_in_order_and_every_reader_sees_it(
        ops in prop::collection::vec(op(), 0..300),
        capacity in 1usize..8,
        cadence in 1u32..400,
        with_view in any::<bool>(),
    ) {
        let freed = Arc::new(AtomicUsize::new(0));
        let b = Bridge::new(Tally::default(), with_view.then(Tally::default), capacity, cadence);
        let mut posted: Vec<u64> = Vec::new();
        let (mut engine, mut last_frame) = (0u64, 0u64);

        for op in ops {
            match op {
                Op::Post(v) => {
                    posted.push(v as u64);
                    b.post(Shared::new(b.handle(), Value { v: v as u64, freed: Arc::clone(&freed) }));
                }
                Op::Block(frames) | Op::Touched(frames) => unsafe {
                    let e = b.begin();
                    prop_assert!(e.count >= engine && e.count <= posted.len() as u64);
                    prop_assert_eq!(e.sum, sum(&posted, e.count), "the engine has a prefix, in order");
                    engine = e.count;
                    if matches!(op, Op::Touched(_)) {
                        b.touch();
                    }
                    b.end(frames);
                },
                Op::Read => {
                    let (count, frame_sum, pending) =
                        b.read(|r| (r.frame.count, r.frame.sum, r.pending.map(|v| (v.count, v.sum))));
                    prop_assert_eq!(frame_sum, sum(&posted, count), "a frame is one the engine published");
                    prop_assert!(count >= last_frame && count <= engine, "frames never go backwards");
                    last_frame = count;
                    let all = (posted.len() as u64, posted.iter().sum::<u64>());
                    match pending {
                        Some(seen) => {
                            prop_assert!(with_view);
                            prop_assert_eq!(seen, all, "the view holds every command posted");
                        }
                        None => prop_assert!(!with_view || count == all.0, "no view only when none is outstanding"),
                    }
                }
            }
        }

        /* The engine catches up, however far behind the ring left it. */
        let mut blocks = 0;
        while b.outstanding() > 0 {
            unsafe {
                b.begin();
                b.end(0);
            }
            blocks += 1;
            prop_assert!(blocks <= posted.len(), "the outbox drains");
        }
        let all = (posted.len() as u64, posted.iter().sum::<u64>());
        prop_assert_eq!(b.read(|r| (r.frame.count, r.frame.sum)), all);
        prop_assert_eq!(freed.load(Ordering::SeqCst), posted.len(), "each payload freed once it was confirmed");
        drop(b);
        prop_assert_eq!(freed.load(Ordering::SeqCst), posted.len(), "and nothing freed twice");
    }
}
