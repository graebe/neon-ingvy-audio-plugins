// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The kick tap: every kick frame under the sweep its own timeline sample had on
this track -- whichever track the host ran first, across a loop, ahead of this
one or behind it -- and by arrival, said so, when the transport is stopped.

The tap is driven directly, with frames and timelines, for the alignment; the
last tests put a real bus (bus-core, in the namespace cargo gives every test)
and the C ABI in front of it.
*/

use super::*;
use bus_core::Writer;
use std::ptr::null_mut;

const SR: u32 = 48_000;

/// This track's sweep for frames `from..from + n`: frame f's sweep is
/// f / 100_000, so a filed sweep names the frame it came from.
fn sweeps(from: u64, n: usize) -> Vec<f32> {
    (0..n).map(|i| (from + i as u64) as f32 / 100_000.0).collect()
}

/// `n` stereo kick frames whose mid is the timeline sample they carry, so a
/// filed kick names the frame it was.
fn kick(from: i64, n: usize) -> Vec<f32> {
    (0..n).flat_map(|i| [(from + i as i64) as f32 + 0.25, (from + i as i64) as f32 - 0.25]).collect()
}

fn tap() -> Tap {
    let mut t = Tap::new();
    t.prepare(SR);
    t
}

/// Everything `t` can file now.
fn file_all(t: &mut Tap) -> Vec<(f32, f32)> {
    let (mut k, mut s) = (vec![0f32; 4096], vec![0f32; 4096]);
    let n = t.file(&mut k, &mut s);
    k[..n].iter().copied().zip(s[..n].iter().copied()).collect()
}

/// A filed pair is right when the kick's timeline sample and the sweep's
/// frame are the same sample: this track noted timeline `t0 + f` at frame f.
fn assert_aligned(pairs: &[(f32, f32)], t0: i64) {
    for &(k, s) in pairs {
        let frame = (s * 100_000.0).round() as i64;
        assert_eq!(k as i64, t0 + frame, "kick {k} filed under frame {frame}");
    }
}

#[test]
fn a_kick_published_after_this_tracks_block_is_filed_under_the_same_sample() {
    let mut t = tap();
    t.note(Some(10_000), &sweeps(0, 256));
    t.offer(&kick(10_000, 256), Some(10_000));
    let pairs = file_all(&mut t);
    assert_eq!(pairs.len(), 256);
    assert_aligned(&pairs, 10_000);
    assert_eq!(t.status(), ScKickStatus::Aligned);
}

#[test]
fn a_kick_published_before_this_tracks_block_waits_for_it() {
    /* The kick's track ran first: its frames are newer than anything noted,
     * so they wait -- and are filed exactly once this block is noted. */
    let mut t = tap();
    t.note(Some(10_000), &sweeps(0, 256));
    t.offer(&kick(10_256, 256), Some(10_256));
    assert!(file_all(&mut t).is_empty(), "filed before its frames were noted");
    t.note(Some(10_256), &sweeps(256, 256));
    let pairs = file_all(&mut t);
    assert_eq!(pairs.len(), 256);
    assert_aligned(&pairs, 10_000);
}

#[test]
fn a_kick_a_few_blocks_behind_still_finds_its_frames() {
    let mut t = tap();
    for b in 0..8u64 {
        t.note(Some(500 + b as i64 * 512), &sweeps(b * 512, 512));
    }
    t.offer(&kick(500 + 512, 512), Some(500 + 512));
    let pairs = file_all(&mut t);
    assert_eq!(pairs.len(), 512);
    assert_aligned(&pairs, 500);
}

#[test]
fn a_kick_older_than_the_history_is_dropped_not_misfiled() {
    let mut t = tap();
    t.note(Some(0), &sweeps(0, HISTORY + 1024));
    t.offer(&kick(0, 512), Some(0));
    assert!(file_all(&mut t).is_empty());
    assert_eq!(t.dropped(), 512);
}

#[test]
fn across_a_loop_each_pass_is_its_own() {
    /* The host jumped from 96_000 back to 0. A kick from just before the jump,
     * arriving late, is filed under the first pass; one after it under the
     * second -- not extrapolated across the jump. */
    let mut t = tap();
    t.note(Some(95_744), &sweeps(0, 256));
    t.note(Some(0), &sweeps(256, 256));
    t.offer(&kick(95_900, 100), Some(95_900));
    let first = file_all(&mut t);
    assert_eq!(first.len(), 100);
    assert_aligned(&first, 95_744);

    t.offer(&kick(10, 100), Some(10));
    let second = file_all(&mut t);
    assert_eq!(second.len(), 100);
    for &(k, s) in &second {
        assert_eq!(k as i64 + 256, (s * 100_000.0).round() as i64);
    }
}

#[test]
fn a_stopped_transport_files_by_arrival_and_says_so() {
    /* Neither side has a timeline: the newest kick frame under this track's
     * newest frame. */
    let mut t = tap();
    t.note(None, &sweeps(0, 512));
    t.offer(&kick(0, 128), None);
    let pairs = file_all(&mut t);
    assert_eq!(pairs.len(), 128);
    assert_eq!(pairs[127].1, sweeps(511, 1)[0]);
    assert_eq!(pairs[0].1, sweeps(384, 1)[0]);
    assert_eq!(t.status(), ScKickStatus::ByArrival);
}

#[test]
fn one_side_without_a_timeline_is_by_arrival_too() {
    let mut t = tap();
    t.note(None, &sweeps(0, 256));
    t.offer(&kick(7_000, 64), Some(7_000));
    let pairs = file_all(&mut t);
    assert_eq!(pairs.len(), 64);
    assert_eq!(t.status(), ScKickStatus::ByArrival);

    let mut t = tap();
    t.note(Some(0), &sweeps(0, 256));
    t.offer(&kick(0, 64), None);
    assert_eq!(file_all(&mut t).len(), 64);
    assert_eq!(t.status(), ScKickStatus::ByArrival);
}

#[test]
fn a_kick_that_never_comes_due_stops_waiting() {
    /* Stamped far ahead of this track -- another loop region, a host's odd
     * position -- it waits a history's length and is then dropped, and what
     * came after it is filed. */
    let mut t = tap();
    t.note(Some(0), &sweeps(0, 64));
    t.offer(&kick(1_000_000, 16), Some(1_000_000));
    assert!(file_all(&mut t).is_empty());
    t.note(Some(64), &sweeps(64, HISTORY));
    t.offer(&kick(HISTORY as i64, 16), Some(HISTORY as i64));
    let pairs = file_all(&mut t);
    assert_eq!(pairs.len(), 16);
    assert_eq!(t.dropped(), 16);
}

#[test]
fn a_full_waiting_room_lets_the_oldest_go() {
    let mut t = tap();
    t.note(Some(0), &sweeps(0, 16));
    t.offer(&kick(100_000, PENDING + 10), Some(100_000));
    assert_eq!(t.dropped(), 10);
}

#[test]
fn the_output_is_filled_to_its_capacity_and_the_rest_keeps() {
    let mut t = tap();
    t.note(Some(0), &sweeps(0, 300));
    t.offer(&kick(0, 300), Some(0));
    let (mut k, mut s) = (vec![0f32; 100], vec![0f32; 100]);
    assert_eq!(t.file(&mut k, &mut s), 100);
    assert_eq!(t.file(&mut k, &mut s), 100);
    assert_eq!(t.file(&mut k, &mut s), 100);
    assert_eq!(t.file(&mut k, &mut s), 0);
}

#[test]
fn the_status_waits_then_goes_silent() {
    let mut t = tap();
    assert_eq!(t.status(), ScKickStatus::Waiting);
    t.note(Some(0), &sweeps(0, 64));
    t.offer(&kick(0, 64), Some(0));
    file_all(&mut t);
    assert_eq!(t.status(), ScKickStatus::Aligned);
    t.note(Some(64), &sweeps(64, SR as usize / 2 + 1));
    assert_eq!(t.status(), ScKickStatus::Silent);
    t.forget();
    assert_eq!(t.status(), ScKickStatus::Waiting);
}

/* ---- on a real bus ---- */

/* One slot per test: cargo runs these in parallel threads of one process. */
const SLOT_DRAIN: u32 = 14;
const SLOT_RATE: u32 = 13;
const SLOT_ABI: u32 = 12;

#[test]
fn drained_from_a_bus_each_run_is_filed_by_its_own_stamp() {
    let (_w, mut p) = Writer::claim(SLOT_DRAIN, SR).expect("claim");
    let mut r = bus_core::Reader::open(SLOT_DRAIN).expect("open");
    let mut t = tap();

    /* The sender's blocks straddle this track's: published before and after,
     * one of them across a loop. */
    p.push_at(&kick(2_000, 128), Some(2_000));
    t.note(Some(2_000), &sweeps(0, 256));
    p.push_at(&kick(2_128, 128), Some(2_128));
    p.push_at(&kick(2_256, 64), Some(2_256));
    let (mut k, mut s) = (vec![0f32; 1024], vec![0f32; 1024]);
    let n = t.drain(&mut r, &mut k, &mut s);
    assert_eq!(n, 256, "the run ahead of this track waits");
    let pairs: Vec<_> = k[..n].iter().copied().zip(s[..n].iter().copied()).collect();
    assert_aligned(&pairs, 2_000);

    t.note(Some(2_256), &sweeps(256, 256));
    let n = t.drain(&mut r, &mut k, &mut s);
    assert_eq!(n, 64);
    let pairs: Vec<_> = k[..n].iter().copied().zip(s[..n].iter().copied()).collect();
    assert_aligned(&pairs, 2_000);
    assert_eq!(t.status(), ScKickStatus::Aligned);
}

#[test]
fn a_bus_at_another_rate_is_read_and_not_drawn() {
    let (_w, mut p) = Writer::claim(SLOT_RATE, 44_100).expect("claim");
    let mut r = bus_core::Reader::open(SLOT_RATE).expect("open");
    let mut t = tap();
    t.note(Some(0), &sweeps(0, 256));
    p.push_at(&kick(0, 256), Some(0));
    let (mut k, mut s) = (vec![0f32; 512], vec![0f32; 512]);
    assert_eq!(t.drain(&mut r, &mut k, &mut s), 0);
    assert_eq!(t.status(), ScKickStatus::OtherRate);
    assert_eq!(r.read(&mut vec![0f32; 64]).frames, 0, "read, so it does not pile up");
}

#[test]
fn the_c_abi_files_through_a_reader_and_survives_nulls() {
    use bus_capi::{abus_reader_close, abus_reader_open, ABUS_OK};
    unsafe {
        /* Nulls everywhere are nothing, not a crash. */
        sc_kick_destroy(null_mut());
        sc_kick_prepare(null_mut(), SR);
        sc_kick_note(null_mut(), 0, 1, null_mut(), 16);
        assert_eq!(sc_kick_drain(null_mut(), null_mut(), null_mut(), null_mut(), 16), 0);
        assert_eq!(sc_kick_status(std::ptr::null()), SC_KICK_OFF);

        let k = sc_kick_create();
        sc_kick_prepare(k, SR);
        assert_eq!(sc_kick_status(k), SC_KICK_OFF);
        let (mut kk, mut ss) = (vec![0f32; 512], vec![0f32; 512]);
        assert_eq!(sc_kick_drain(k, null_mut(), kk.as_mut_ptr(), ss.as_mut_ptr(), 512), 0);
        assert_eq!(sc_kick_status(k), SC_KICK_OFF);

        let (_w, mut p) = Writer::claim(SLOT_ABI, SR).expect("claim");
        let mut r = null_mut();
        assert_eq!(abus_reader_open(SLOT_ABI, &mut r), ABUS_OK);
        assert_eq!(sc_kick_drain(k, r, kk.as_mut_ptr(), ss.as_mut_ptr(), 512), 0);
        assert_eq!(sc_kick_status(k), SC_KICK_WAITING);

        let sw = sweeps(0, 256);
        sc_kick_note(k, 4_000, 1, sw.as_ptr(), 256);
        sc_kick_note(k, 0, 1, sw.as_ptr(), 0);
        p.push_at(&kick(4_000, 256), Some(4_000));
        assert_eq!(sc_kick_drain(k, r, kk.as_mut_ptr(), ss.as_mut_ptr(), 100), 100);
        assert_eq!(sc_kick_drain(k, r, kk.as_mut_ptr(), ss.as_mut_ptr(), 512), 156);
        assert_eq!(sc_kick_status(k), SC_KICK_ALIGNED);
        assert_eq!(sc_kick_drain(k, r, null_mut(), ss.as_mut_ptr(), 512), 0);

        /* Another reader is another source: what waited is forgotten. */
        let mut r2 = null_mut();
        assert_eq!(abus_reader_open(SLOT_ABI, &mut r2), ABUS_OK);
        p.push_at(&kick(9_999_999, 16), Some(9_999_999));
        sc_kick_drain(k, r, kk.as_mut_ptr(), ss.as_mut_ptr(), 512);
        assert_eq!(sc_kick_drain(k, r2, kk.as_mut_ptr(), ss.as_mut_ptr(), 512), 0);

        abus_reader_close(r);
        abus_reader_close(r2);
        sc_kick_destroy(k);
    }
}
