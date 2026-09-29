/*
 * Claiming, releasing and probing -- the parts that do touch shared memory.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
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

use crate::{probe, ClaimError, Writer};

const SLOT_ROUNDTRIP: u32 = 16;
const SLOT_TAKEN: u32 = 15;
const SLOT_RELEASE: u32 = 14;
const SLOT_PROBE: u32 = 13;
const SLOT_RATE: u32 = 12;

#[test]
fn audio_crosses_a_real_segment() {
    let w = Writer::claim(SLOT_ROUNDTRIP, 48_000).expect("claim");
    let mut r = crate::Reader::open(SLOT_ROUNDTRIP).expect("open");

    let src: Vec<f32> = (0..256 * 2).map(|i| i as f32 * 0.5).collect();
    w.push(&src);

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

    let w = Writer::claim(SLOT_PROBE, 44_100).expect("claim");
    w.set_label("Bass");

    let info = probe(SLOT_PROBE).expect("a claimed slot is visible");
    assert!(info.live);
    assert_eq!(info.sample_rate, 44_100);
    assert_eq!(info.label, "Bass");
    assert_eq!(info.slot, SLOT_PROBE);

    drop(w);
    assert!(
        probe(SLOT_PROBE).is_none(),
        "a clean release unlinks the segment"
    );
}

#[test]
fn a_rate_change_restarts_the_stream() {
    let w = Writer::claim(SLOT_RATE, 48_000).expect("claim");
    let mut r = crate::Reader::open(SLOT_RATE).expect("open");

    w.push(&vec![0.25f32; 128 * 2]);
    assert_eq!(r.read(&mut vec![0f32; 128 * 2]).frames, 128);

    w.set_sample_rate(96_000);

    /* The samples either side of a rate change are not the same signal, so the
     * reader is told to start again rather than handed the seam. */
    let got = r.read(&mut vec![0f32; 128 * 2]);
    assert!(got.resynced, "a rate change must resync the reader");
    assert_eq!(r.info().sample_rate, 96_000);
}

#[test]
fn slot_numbers_outside_the_range_are_refused() {
    assert_eq!(Writer::claim(0, 48_000).err(), Some(ClaimError::BadSlot));
    assert_eq!(
        Writer::claim(crate::MAX_SLOT + 1, 48_000).err(),
        Some(ClaimError::BadSlot)
    );
    assert!(crate::Reader::open(0).is_none());
    assert!(crate::Reader::open(crate::MAX_SLOT + 1).is_none());
    assert!(probe(0).is_none());
}
