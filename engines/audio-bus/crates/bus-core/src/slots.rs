// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Claiming, releasing and probing -- the parts that do touch shared memory.
 *
 * These use real segments, so they need real slot numbers, and cargo runs
 * tests in parallel threads of ONE process. Two tests on slot 3 would fight
 * over the same shm object and fail each other intermittently -- the worst kind
 * of test. So each test here owns a slot number outright; `SLOT_*` below is
 * that allocation, and it is why the numbers look arbitrary.
 *
 * They also use the high end of the range on purpose: 1..8 are what a person
 * reaches for first, and a test that leaves a segment behind after a crash
 * should not be squatting on the slot they are about to pick.
 */

use crate::{probe, ClaimError, Reader, Writer};

const SLOT_ROUNDTRIP: u32 = 16;
const SLOT_TAKEN: u32 = 15;
const SLOT_RELEASE: u32 = 14;
const SLOT_PROBE: u32 = 13;
const SLOT_RATE: u32 = 12;
const SLOT_REATTACH: u32 = 6;
const SLOT_ORPHAN: u32 = 5;

#[test]
fn audio_crosses_a_real_segment() {
    let (_w, mut p) = Writer::claim(SLOT_ROUNDTRIP, 48_000).expect("claim");
    let mut r = Reader::open(SLOT_ROUNDTRIP).expect("open");

    let src: Vec<f32> = (0..256 * 2).map(|i| i as f32 * 0.5).collect();
    p.push(&src);

    let mut out = vec![0f32; 256 * 2];
    let got = r.read(&mut out);
    assert_eq!(got.frames, 256);
    assert_eq!(out, src);
}

#[test]
fn a_second_sender_on_one_slot_is_told_no() {
    let first = Writer::claim(SLOT_TAKEN, 48_000).expect("the first claim succeeds");

    /*
     * THE COLLISION THAT MUST NOT BE SILENT.
     *
     * Both of these are in one process -- which is exactly the Live case, two
     * Listen-Ins in one set -- so a pid check alone cannot separate them. An
     * earlier draft exempted "our own pid" from the liveness test and the
     * second instance stole the bus from the first without a word.
     */
    match Writer::claim(SLOT_TAKEN, 48_000) {
        Err(ClaimError::Taken) => {}
        Err(e) => panic!("wrong refusal: {e:?}"),
        Ok(_) => panic!("the second sender stole the slot"),
    }
    drop(first);
}

#[test]
fn releasing_frees_the_slot_for_the_next_sender() {
    let first = Writer::claim(SLOT_RELEASE, 48_000).expect("claim");
    drop(first);
    let second = Writer::claim(SLOT_RELEASE, 48_000);
    assert!(second.is_ok(), "a released slot must be re-claimable");
}

#[test]
fn a_slot_nobody_uses_does_not_exist_and_probing_does_not_create_it() {
    /* Probing must never bring a bus into existence -- a receiver walking all
     * sixteen slots to fill a dropdown would otherwise create sixteen. */
    assert!(probe(SLOT_PROBE).is_none(), "nothing has claimed this slot");
    assert!(probe(SLOT_PROBE).is_none(), "and probing did not change that");

    let (mut w, p) = Writer::claim(SLOT_PROBE, 44_100).expect("claim");
    w.set_label("Bass");

    let info = probe(SLOT_PROBE).expect("a claimed slot is visible");
    assert!(info.live);
    assert_eq!(info.sample_rate, 44_100);
    assert_eq!(info.label, "Bass");
    assert_eq!(info.slot, SLOT_PROBE);

    /* The slot is released when the LAST half goes, not the first. */
    drop(w);
    assert!(probe(SLOT_PROBE).is_some_and(|i| i.live), "half a claim released the slot");
    drop(p);
    assert!(
        probe(SLOT_PROBE).is_none(),
        "a clean release unlinks the segment"
    );
}

#[test]
fn a_rate_change_restarts_the_stream() {
    let (mut w, mut p) = Writer::claim(SLOT_RATE, 48_000).expect("claim");
    let mut r = Reader::open(SLOT_RATE).expect("open");

    p.push(&vec![0.25f32; 128 * 2]);
    assert_eq!(r.read(&mut vec![0f32; 128 * 2]).frames, 128);

    /* Posted by the main thread, applied by the audio thread's next push --
     * the only place the frame count is written. */
    w.set_sample_rate(96_000);
    assert_eq!(r.info().sample_rate, 48_000, "applied before the audio thread ran");
    p.push(&vec![0.5f32; 64 * 2]);

    /* The samples either side of a rate change are not the same signal, so the
     * reader is told to start again rather than handed the seam. */
    let got = r.read(&mut vec![0f32; 128 * 2]);
    assert!(got.resynced, "a rate change must resync the reader");
    assert_eq!(r.info().sample_rate, 96_000);

    /* And the new stream flows. */
    p.push(&vec![0.5f32; 64 * 2]);
    assert_eq!(r.read(&mut vec![0f32; 128 * 2]).frames, 64);
}

#[test]
fn slot_numbers_outside_the_range_are_refused() {
    assert_eq!(Writer::claim(0, 48_000).err(), Some(ClaimError::BadSlot));
    assert_eq!(
        Writer::claim(crate::MAX_SLOT + 1, 48_000).err(),
        Some(ClaimError::BadSlot)
    );
    assert!(Reader::open(0).is_none());
    assert!(Reader::open(crate::MAX_SLOT + 1).is_none());
    assert!(probe(0).is_none());
}

#[test]
fn a_reader_follows_a_sender_that_came_back() {
    /*
     * THE ORPHANED READER. A Listen-In is removed and added again: the first
     * sender unlinks its segment on the way out, the second creates a new one
     * under the same name. The reader is still mapping the first -- a writer
     * that stopped, forever -- until it asks the name again.
     */
    let first = Writer::claim(SLOT_REATTACH, 48_000).expect("claim");
    let mut r = Reader::open(SLOT_REATTACH).expect("open");
    assert!(!r.reattach(), "moved while the segment was still current");
    drop(first);

    let (_w, mut p) = Writer::claim(SLOT_REATTACH, 48_000).expect("reclaim");
    p.push(&vec![0.5f32; 64 * 2]);
    let mut out = vec![0f32; 256 * 2];
    assert_eq!(r.read(&mut out).frames, 0, "an orphaned mapping cannot see the new sender");

    assert!(r.reattach(), "the reader did not notice the segment was replaced");
    let got = r.read(&mut out);
    assert!(got.resynced, "moving segments must read as a restart");
    p.push(&vec![0.5f32; 64 * 2]);
    assert_eq!(r.read(&mut out).frames, 64, "the reattached reader hears nothing");
    assert!(!r.reattach(), "moved again with nothing replaced");
}

#[test]
fn a_released_segment_cannot_be_mistaken_for_the_bus() {
    /*
     * THE RELEASE RACE, step by step. A claimer opens the slot a moment before
     * its owner quits. The owner unlinks, then frees -- so by the time the
     * claimer's swap can succeed, the segment it holds is no longer the one
     * the name leads to. The swap does succeed; what `claim` must then do is
     * notice and start over, and `still_named` is how.
     *
     * Windows answers the other way, and is right to: the claimer's own open
     * holds a handle, and a name lives while a handle does, so the segment it
     * claimed is still the bus (see shm/win32.rs). Either way the claim ends
     * up where readers look.
     */
    use crate::header::owner_pid;
    use core::sync::atomic::Ordering;

    let (w, p) = Writer::claim(SLOT_ORPHAN, 48_000).expect("claim");
    let (early, _) = crate::shm::Shm::create_or_open(SLOT_ORPHAN).expect("the claimer's open");
    assert!(crate::still_named(&early));
    drop((w, p));

    assert_eq!(owner_pid(early.header().owner.load(Ordering::Relaxed)), 0, "not released");
    let token = crate::acquire(early.header(), 4242, |_| false).expect("a free segment claims");
    assert_eq!(
        crate::still_named(&early),
        cfg!(windows),
        "a segment the owner gave up is mistaken for the bus, or the bus for an orphan"
    );
    assert!(crate::release(early.header(), token));

    /* And a real claim now lands where readers will look. */
    let (_w, mut p) = Writer::claim(SLOT_ORPHAN, 48_000).expect("claim after the release");
    let mut r = Reader::open(SLOT_ORPHAN).expect("readers find it");
    p.push(&vec![0.5f32; 32 * 2]);
    assert_eq!(r.read(&mut vec![0f32; 64 * 2]).frames, 32);
}
