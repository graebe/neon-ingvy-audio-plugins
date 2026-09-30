/*
 * The abus_* C ABI, called the way a plugin calls it.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * bus-core's tests prove the ring and the claim; tests/abus_roundtrip.c proves
 * the hand-written header against this crate's release staticlib. What is
 * left is the layer in between, and it is not nothing: the out-parameters, the
 * error codes, the NULL-handle silence ProcessBlock relies on, the label copy
 * and its terminator. Each is asserted here through the same raw pointers a C
 * caller passes.
 *
 * THE SHARED MEMORY IS REAL, and two things keep that from biting.
 *
 * The namespace: the workspace's .cargo/config.toml sets NIA_BUS_NS for every
 * cargo test, so these segments are this checkout's and never a Live session's
 * (bus-core's `tests_never_touch_the_production_names` guards that).
 *
 * The slots: cargo runs the tests below on parallel threads of ONE process, and
 * two tests on one slot would claim and unlink each other's segment and fail
 * intermittently. So each test owns its slot outright, as bus-core's slots.rs
 * does -- that is what the SLOT_* allocation is. Test binaries run one after
 * another, so reusing a number another crate's binary uses is harmless as long
 * as every test here releases what it claims, which each one does.
 */

use super::*;
use core::ptr::{null, null_mut};

const SLOT_ROUNDTRIP: u32 = 11;
const SLOT_TAKEN: u32 = 10;
const SLOT_NEVER: u32 = 1; /* never claimed by anything in this binary */
const SLOT_BEHIND: u32 = 9;
const SLOT_RATE: u32 = 8;
const SLOT_LABEL: u32 = 7;

const SR: u32 = 48_000;
const CH: usize = bus_core::CHANNELS as usize;

/// Claim `slot` through the ABI, or fail the test saying why not.
unsafe fn claim(slot: u32) -> (*mut AbusWriter, *mut AbusPusher) {
    let mut w = null_mut();
    let mut p = null_mut();
    let rc = abus_writer_claim(slot, SR, &mut w, &mut p);
    assert_eq!(rc, ABUS_OK, "slot {slot} could not be claimed");
    assert!(!w.is_null() && !p.is_null());
    (w, p)
}

unsafe fn open(slot: u32) -> *mut AbusReader {
    let mut r = null_mut();
    assert_eq!(abus_reader_open(slot, &mut r), ABUS_OK, "slot {slot} could not be opened");
    assert!(!r.is_null());
    r
}

/// What `abus_probe` says about a slot, label decoded.
unsafe fn probe(slot: u32) -> Option<(bool, u32, String)> {
    let mut live = -1;
    let mut rate = u32::MAX;
    let mut label = [0xAAu8; 32];
    let found = abus_probe(slot, &mut live, &mut rate, label.as_mut_ptr(), label.len() as u32);
    if found == 0 {
        /* A missing slot still answers every out-parameter. */
        assert_eq!((live, rate, label[0]), (0, 0, 0), "a missing slot left junk behind");
        return None;
    }
    assert_eq!(found, 1);
    let end = label.iter().position(|&b| b == 0).expect("the label was not terminated");
    Some((live == 1, rate, String::from_utf8(label[..end].to_vec()).unwrap()))
}

/// `n` frames of a ramp, so a wrong answer is a wrong number rather than a
/// plausible one: left is the index, right its negation.
fn ramp(n: usize) -> Vec<f32> {
    (0..n).flat_map(|i| [i as f32, -(i as f32)]).collect()
}

#[test]
fn the_constants_are_the_headers() {
    assert_eq!(abus_channels(), 2, "the bus is stereo");
    assert_eq!(abus_max_slot(), 16, "ABUS_MAX_SLOT");
}

#[test]
fn a_null_handle_is_silence_not_a_crash() {
    /* A Listen-In whose slot was taken holds NULL handles, and ProcessBlock
     * calls push unconditionally. */
    let block = ramp(64);
    let mut out = vec![7.0f32; 64 * CH];
    let mut dropped = 99u64;
    let mut resynced = 99i32;
    unsafe {
        abus_pusher_push(null_mut(), block.as_ptr(), 64);
        abus_writer_set_sample_rate(null_mut(), 44_100);
        abus_writer_set_label(null_mut(), c"x".as_ptr().cast());
        abus_writer_release(null_mut());
        abus_pusher_release(null_mut());
        abus_reader_close(null_mut());
        assert_eq!(abus_reader_reattach(null_mut()), 0);
        let got = abus_reader_read(null_mut(), out.as_mut_ptr(), 64, &mut dropped, &mut resynced);
        assert_eq!(got, 0);
    }
    assert_eq!((dropped, resynced), (99, 99), "a refused read wrote its out-params");
    assert!(out.iter().all(|&s| s == 7.0), "a refused read wrote samples");
}

#[test]
fn bad_arguments_are_reported_and_leave_the_outputs_alone() {
    unsafe {
        let mut w = null_mut();
        let mut p = null_mut();
        assert_eq!(abus_writer_claim(0, SR, &mut w, &mut p), ABUS_ERR_BAD_SLOT, "slot 0 is not a bus");
        assert_eq!(abus_writer_claim(abus_max_slot() + 1, SR, &mut w, &mut p), ABUS_ERR_BAD_SLOT);
        assert!(w.is_null() && p.is_null(), "a failed claim wrote a handle");

        assert_eq!(abus_writer_claim(SLOT_NEVER, SR, null_mut(), &mut p), ABUS_ERR_UNAVAILABLE);
        assert_eq!(abus_writer_claim(SLOT_NEVER, SR, &mut w, null_mut()), ABUS_ERR_UNAVAILABLE);

        let mut r = null_mut();
        assert_eq!(abus_reader_open(SLOT_NEVER, null_mut()), ABUS_ERR_UNAVAILABLE);
        assert_eq!(abus_reader_open(SLOT_NEVER, &mut r), ABUS_ERR_UNAVAILABLE, "nobody sends there");
        assert!(r.is_null());

        /* And probing an unused slot does not bring it into being. */
        assert_eq!(probe(SLOT_NEVER), None);
        assert_eq!(probe(SLOT_NEVER), None, "asking twice still does not");
        assert_eq!(abus_probe(SLOT_NEVER, null_mut(), null_mut(), null_mut(), 0), 0);
    }
}

#[test]
fn audio_crosses_the_abi_unchanged() {
    unsafe {
        let (w, p) = claim(SLOT_ROUNDTRIP);
        abus_writer_set_label(w, c"Bass".as_ptr().cast());
        let r = open(SLOT_ROUNDTRIP);

        const N: usize = 480;
        let src = ramp(N);
        abus_pusher_push(p, src.as_ptr(), N as u32);

        let mut out = vec![0f32; N * CH];
        let mut dropped = 99u64;
        let mut resynced = 99i32;
        let got = abus_reader_read(r, out.as_mut_ptr(), N as u32, &mut dropped, &mut resynced);
        assert_eq!(got as usize, N, "every frame pushed comes back");
        assert_eq!((dropped, resynced), (0, 0), "and nothing is reported missing");
        assert_eq!(out, src, "the samples are unchanged");

        /* The out-params are optional, and a degenerate push or read is nothing. */
        abus_pusher_push(p, null(), 64);
        abus_pusher_push(p, src.as_ptr(), 0);
        assert_eq!(abus_reader_read(r, out.as_mut_ptr(), 0, null_mut(), null_mut()), 0);
        assert_eq!(abus_reader_read(r, null_mut(), 64, null_mut(), null_mut()), 0);
        abus_pusher_push(p, src.as_ptr(), 32);
        let got = abus_reader_read(r, out.as_mut_ptr(), N as u32, null_mut(), null_mut());
        assert_eq!(got, 32, "only the one real push arrived");

        /* A probe describes it without opening it. */
        assert_eq!(probe(SLOT_ROUNDTRIP), Some((true, SR, "Bass".to_string())));
        assert_eq!(abus_probe(SLOT_ROUNDTRIP, null_mut(), null_mut(), null_mut(), 0), 1,
                   "every out-parameter may be NULL");

        abus_reader_close(r);
        abus_pusher_release(p);
        abus_writer_release(w);
        assert_eq!(probe(SLOT_ROUNDTRIP), None, "a released slot is gone, not merely idle");
    }
}

#[test]
fn the_claim_goes_with_its_last_half_and_the_reader_follows() {
    unsafe {
        let (w, p) = claim(SLOT_TAKEN);
        let r = open(SLOT_TAKEN);

        /* A second sender is refused, not quietly allowed to overwrite. */
        let mut w2 = null_mut();
        let mut p2 = null_mut();
        assert_eq!(abus_writer_claim(SLOT_TAKEN, SR, &mut w2, &mut p2), ABUS_ERR_TAKEN);
        assert!(w2.is_null() && p2.is_null(), "a refused claim wrote a handle");
        assert_eq!(abus_reader_reattach(r), 0, "a current reader does not move");

        /* A pusher still lent to an audio thread keeps the slot. */
        abus_writer_release(w);
        assert_eq!(abus_writer_claim(SLOT_TAKEN, SR, &mut w2, &mut p2), ABUS_ERR_TAKEN);
        abus_pusher_release(p);

        let (w, p) = claim(SLOT_TAKEN);
        assert_eq!(abus_reader_reattach(r), 1, "the reader moves to the new segment");
        let mut out = vec![0f32; 64 * CH];
        let mut resynced = 0i32;
        let got = abus_reader_read(r, out.as_mut_ptr(), 64, null_mut(), &mut resynced);
        assert_eq!((got, resynced), (0, 1), "and reports the move as a restart");

        let src = ramp(64);
        abus_pusher_push(p, src.as_ptr(), 64);
        let got = abus_reader_read(r, out.as_mut_ptr(), 64, null_mut(), &mut resynced);
        assert_eq!((got, resynced), (64, 0), "then hears the new sender");
        assert_eq!(out, src);

        abus_reader_close(r);
        abus_pusher_release(p);
        abus_writer_release(w);
    }
}

#[test]
fn a_reader_that_fell_behind_is_told_how_much() {
    /* Otherwise a spectrogram draws the missing seconds as a quiet passage
     * that never happened. */
    unsafe {
        let (w, p) = claim(SLOT_BEHIND);
        let r = open(SLOT_BEHIND);
        let big = vec![0.5f32; 8192 * CH];
        for _ in 0..64 {
            abus_pusher_push(p, big.as_ptr(), 8192);
        }
        let mut out = vec![0f32; 480 * CH];
        let mut dropped = 0u64;
        let got = abus_reader_read(r, out.as_mut_ptr(), 480, &mut dropped, null_mut());
        assert!(dropped > 0, "the gap was not reported");
        assert_eq!(got, 480, "and the audio that survived still arrives");
        assert!(out.iter().all(|&s| s == 0.5));
        abus_reader_close(r);
        abus_pusher_release(p);
        abus_writer_release(w);
    }
}

#[test]
fn a_rate_change_is_published_and_restarts_the_stream() {
    unsafe {
        let (w, p) = claim(SLOT_RATE);
        let r = open(SLOT_RATE);
        let block = ramp(64);
        let mut out = vec![0f32; 64 * CH];

        abus_writer_set_sample_rate(w, 96_000);
        /* Applied by the audio thread's next push, the only place the frame
         * count is written. */
        assert_eq!(probe(SLOT_RATE).map(|i| i.1), Some(SR), "applied before the audio thread ran");
        abus_pusher_push(p, block.as_ptr(), 64);
        assert_eq!(probe(SLOT_RATE).map(|i| i.1), Some(96_000));

        let mut resynced = 0i32;
        abus_reader_read(r, out.as_mut_ptr(), 64, null_mut(), &mut resynced);
        assert_eq!(resynced, 1, "the two sides of a rate change are not one signal");

        abus_reader_close(r);
        abus_pusher_release(p);
        abus_writer_release(w);
    }
}

#[test]
fn the_label_is_bounded_and_always_terminated() {
    unsafe {
        let (w, p) = claim(SLOT_LABEL);

        /* 40 bytes of two-byte characters: stored at most 31 bytes, and on a
         * character boundary, so 15 of them. */
        let long = "é".repeat(20) + "\0";
        abus_writer_set_label(w, long.as_ptr());
        assert_eq!(probe(SLOT_LABEL).map(|i| i.2), Some("é".repeat(15)));

        /* A caller's short buffer gets what fits and a terminator. */
        abus_writer_set_label(w, c"Pad Synth".as_ptr().cast());
        let mut small = [0xAAu8; 4];
        assert_eq!(abus_probe(SLOT_LABEL, null_mut(), null_mut(), small.as_mut_ptr(), 4), 1);
        assert_eq!(&small, b"Pad\0");

        /* NULL text is an empty label rather than a crash. */
        abus_writer_set_label(w, null());
        assert_eq!(probe(SLOT_LABEL).map(|i| i.2), Some(String::new()));

        abus_pusher_release(p);
        abus_writer_release(w);
    }
}
