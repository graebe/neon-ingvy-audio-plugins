// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The claim protocol, on a heap header, through every interleaving that
 * matters.
 *
 * `acquire` takes its liveness check as a parameter, so a test can decide which
 * pids are dead and -- more to the point -- run a SECOND claimer from inside
 * the first one's check: after it has looked at the owner word and before it
 * swaps it. That is the exact window the old two-word protocol lost in, and a
 * thread race would find it once in a million runs where this finds it every
 * time.
 */

use core::sync::atomic::Ordering;
use std::cell::Cell;

use crate::header::{owner_count, owner_pid, owner_word, Header};
use crate::{acquire, release, ClaimError};

const DEAD: u32 = 99;
const A: u32 = 1;
const B: u32 = 2;

fn header() -> Box<Header> {
    /* Every field is an atomic, and all-zero is a valid value for each. */
    Box::new(unsafe { core::mem::zeroed() })
}

fn dead(pid: u32) -> bool {
    pid == DEAD
}

#[test]
fn two_reclaimers_of_one_dead_pid_do_not_both_win() {
    /*
     * THE DOUBLE CLAIM. A slot left held by a crashed process; two senders
     * reclaim it at once. B has read the dead holder and judged it gone; A then
     * runs its whole claim; B carries on. Under the old protocol B's "reset to
     * free" landed on A's brand-new claim -- whose pid was not written yet --
     * and both returned Ok.
     */
    let hdr = header();
    hdr.owner.store(owner_word(7, DEAD), Ordering::Relaxed);

    let a_got = Cell::new(None);
    let first = Cell::new(true);
    let b_got = acquire(&hdr, B, |pid| {
        if first.replace(false) {
            a_got.set(Some(acquire(&hdr, A, dead)));
        }
        dead(pid)
    });
    let a_got = a_got.get().expect("A never ran");

    assert!(a_got.is_ok(), "the first reclaimer should have won: {a_got:?}");
    assert_eq!(b_got, Err(ClaimError::Taken), "both reclaimers hold the slot");
    assert_eq!(owner_pid(hdr.owner.load(Ordering::Relaxed)), A);
}

#[test]
fn a_recycled_pid_does_not_make_an_old_judgement_current() {
    /*
     * B judges DEAD gone. Before B swaps, A claims and releases, and a new
     * process that happens to get pid DEAD claims the slot -- the owner word
     * shows DEAD again, alive this time. A bare-pid swap would succeed and
     * steal the bus; the claim count in the word makes it fail.
     */
    let hdr = header();
    hdr.owner.store(owner_word(3, DEAD), Ordering::Relaxed);

    let first = Cell::new(true);
    let reborn = Cell::new(false);
    let b_got = acquire(&hdr, B, |pid| {
        if first.replace(false) {
            let t = acquire(&hdr, A, dead).expect("A claims");
            assert!(release(&hdr, t));
            acquire(&hdr, DEAD, |_| false).expect("the recycled pid claims a free slot");
            reborn.set(true);
            return true; /* B's judgement, made before the recycling */
        }
        /* Asked again after its swap failed: DEAD is alive now. */
        !(reborn.get() && pid == DEAD)
    });
    assert_eq!(b_got, Err(ClaimError::Taken), "an old judgement stole a live slot");
    assert_eq!(owner_pid(hdr.owner.load(Ordering::Relaxed)), DEAD);
}

#[test]
fn a_live_holder_is_never_displaced_and_a_free_slot_is_taken_once() {
    let hdr = header();
    let t = acquire(&hdr, A, dead).expect("a free slot is claimable");
    assert_eq!(owner_pid(t), A);
    assert_eq!(acquire(&hdr, B, dead), Err(ClaimError::Taken));
    /* Same pid, second instance -- the two-Listen-Ins-in-one-Live case. */
    assert_eq!(acquire(&hdr, A, dead), Err(ClaimError::Taken));
}

#[test]
fn only_the_exact_claim_can_release() {
    let hdr = header();
    let old = acquire(&hdr, A, dead).unwrap();
    assert!(release(&hdr, old));
    let new = acquire(&hdr, A, dead).unwrap();
    assert_ne!(owner_count(old), owner_count(new));

    /* A stale handle from the first claim, same pid, must not free the
     * second one. */
    assert!(!release(&hdr, old), "a stale token released a live claim");
    assert_eq!(hdr.owner.load(Ordering::Relaxed), new);
    assert!(release(&hdr, new));
    assert_eq!(owner_pid(hdr.owner.load(Ordering::Relaxed)), 0);
}

#[test]
fn many_threads_reclaiming_one_dead_slot_produce_one_winner() {
    use std::sync::atomic::AtomicUsize;
    use std::sync::{Arc, Barrier};

    const THREADS: usize = 8;
    const ROUNDS: usize = 2000;

    let hdr: Arc<Header> = Arc::from(header());
    let barrier = Arc::new(Barrier::new(THREADS));
    let wins: Arc<Vec<AtomicUsize>> = Arc::new((0..ROUNDS).map(|_| AtomicUsize::new(0)).collect());

    let handles: Vec<_> = (0..THREADS)
        .map(|t| {
            let (hdr, barrier, wins) = (hdr.clone(), barrier.clone(), wins.clone());
            std::thread::spawn(move || {
                for round in 0..ROUNDS {
                    if t == 0 {
                        /* Every round starts from a crashed holder. */
                        let c = owner_count(hdr.owner.load(Ordering::Relaxed));
                        hdr.owner.store(owner_word(c.wrapping_add(1), DEAD), Ordering::Relaxed);
                    }
                    barrier.wait();
                    if acquire(&hdr, 100 + t as u32, dead).is_ok() {
                        wins[round].fetch_add(1, Ordering::Relaxed);
                    }
                    barrier.wait();
                }
            })
        })
        .collect();
    for h in handles {
        h.join().unwrap();
    }
    for (round, w) in wins.iter().enumerate() {
        assert_eq!(w.load(Ordering::Relaxed), 1, "round {round}: not exactly one winner");
    }
}
