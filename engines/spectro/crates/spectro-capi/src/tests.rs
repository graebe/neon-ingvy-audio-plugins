// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The spectro_* and srecv_* C ABI, called the way the shell calls it.
 *
 * spectro-core and spectro-recv test the analysis. tests/spectro_columns.c and
 * tests/srecv_api.c test the hand-written headers against the release
 * staticlib -- which cargo's coverage cannot see. These make the ABI's own
 * promises from Rust, through the same raw pointers: null is a no-op
 * everywhere, out-of-range arguments are clamped rather than trusted, buffers
 * are filled only as far as the caller said they reach, and the sizes a caller
 * sizes by (bands, the source list) are the ones the data arrives in.
 *
 * NO LIVE BUS HERE. A sender would need bus-core, which this crate reaches only
 * through spectro-recv; spectro-recv/tests/receiver.rs and worker.rs make the
 * end-to-end claim with a real `bus_core::Writer`. The .cargo/config.toml
 * namespace keeps the probing below away from any Live session's buses.
 */

use super::*;
use core::ptr::{null, null_mut};

const SR: f32 = 48_000.0;
const BANDS: usize = 256; /* SPECTRO_BANDS */
const FFT: c_int = 8192; /* SPECTRO_FFT_SIZE */
const HOP: c_int = 1024; /* SPECTRO_HOP */
const COLUMN_CAPACITY: usize = 256; /* SPECTRO_COLUMN_CAPACITY */

/// A sine, continuing from `*phase`, `n` samples long.
fn sine(hz: f32, amp: f32, n: usize, phase: &mut f64) -> Vec<f32> {
    let step = 2.0 * core::f64::consts::PI * hz as f64 / SR as f64;
    (0..n)
        .map(|_| {
            let s = amp * phase.sin() as f32;
            *phase += step;
            s
        })
        .collect()
}

/// Push `frames` of a tone in host-sized blocks.
unsafe fn push_sine(p: *mut Spectro, hz: f32, amp: f32, frames: usize, phase: &mut f64) {
    let tone = sine(hz, amp, frames, phase);
    for block in tone.chunks(512) {
        spectro_push_f32(p, block.as_ptr(), block.len() as c_int);
    }
}

fn peak_band(col: &[u8]) -> usize {
    (0..col.len()).fold(0, |best, b| if col[b] > col[best] { b } else { best })
}

fn nearest_band(centres: &[f32], hz: f32) -> usize {
    (0..centres.len()).fold(0, |best, b| {
        if (centres[b] - hz).abs() < (centres[best] - hz).abs() { b } else { best }
    })
}

unsafe fn band_hz(p: *const Spectro) -> Vec<f32> {
    let mut hz = vec![0f32; BANDS];
    assert_eq!(spectro_band_hz(p, hz.as_mut_ptr(), BANDS as c_int), BANDS as c_int);
    hz
}

/* --------------------------------------------------------------- analyzer */

#[test]
fn the_sizes_come_from_the_engine() {
    assert_eq!(spectro_pick_fft_size(SR), FFT, "the documented default at 48 kHz");
    assert_eq!(spectro_pick_hop(SR, FFT), HOP);
    assert_eq!(spectro_pick_fft_size(96_000.0), 16_384, "finer bins at 96 kHz, capped there");
    assert_eq!(spectro_pick_fft_size(192_000.0), 16_384);
    /* A nonsense rate still picks something drawable, and a negative window
     * length is clamped at the boundary rather than cast to a huge usize. */
    assert_eq!(spectro_pick_fft_size(f32::NAN), 8192);
    assert!(spectro_pick_hop(SR, -1) > 0);
}

#[test]
fn a_null_analyzer_is_a_no_op_everywhere() {
    /* The shell calls these before OnReset and after its destructor. */
    let x = [0.5f32; 64];
    let mut cols = vec![0xAAu8; BANDS];
    let mut hz = [0f32; 4];
    unsafe {
        spectro_free(null_mut());
        spectro_configure(null_mut(), SR, FFT, HOP, BANDS as c_int, 10.0, 20_000.0, -96.0, 0.0);
        spectro_push_f32(null_mut(), x.as_ptr(), 64);
        spectro_set_range(null_mut(), 100.0, 1000.0);
        assert_eq!(spectro_take_columns(null_mut(), cols.as_mut_ptr(), 1), 0);
        assert_eq!(spectro_bands(null()), 0);
        assert_eq!(spectro_band_hz(null(), hz.as_mut_ptr(), 4), 0);
        assert_eq!(spectro_dropped(null()), 0);
    }
    assert!(cols.iter().all(|&b| b == 0xAA), "a null analyzer wrote columns");
}

#[test]
fn the_defaults_are_the_headers() {
    unsafe {
        let s = spectro_new();
        assert!(!s.is_null());
        assert_eq!(spectro_bands(s), BANDS as c_int, "SPECTRO_BANDS");
        let hz = band_hz(s);
        assert!((hz[0] - 10.0).abs() < 1.0, "the axis starts at SPECTRO_F_MIN");
        assert!(hz.windows(2).all(|w| w[1] > w[0]), "the band centres ascend");
        assert_eq!(spectro_dropped(s), 0);
        spectro_free(s);
    }
}

#[test]
fn a_degenerate_call_is_refused_without_touching_the_buffer() {
    unsafe {
        let s = spectro_new();
        let mut cols = vec![0xAAu8; BANDS];
        let mut hz = [7f32; 4];
        assert_eq!(spectro_take_columns(s, null_mut(), 4), 0);
        assert_eq!(spectro_take_columns(s, cols.as_mut_ptr(), 0), 0);
        assert_eq!(spectro_take_columns(s, cols.as_mut_ptr(), -1), 0);
        assert_eq!(spectro_band_hz(s, null_mut(), 4), 0);
        assert_eq!(spectro_band_hz(s, hz.as_mut_ptr(), 0), 0);
        assert!(cols.iter().all(|&b| b == 0xAA));
        assert_eq!(hz, [7.0; 4]);

        /* A short axis buffer is filled as far as it reaches, and no further. */
        assert_eq!(spectro_band_hz(s, hz.as_mut_ptr(), 3), 3);
        assert_eq!(hz[3], 7.0, "past `max`");

        /* Pushing nothing produces nothing. */
        let x = [1.0f32; 16];
        spectro_push_f32(s, null(), 16);
        spectro_push_f32(s, x.as_ptr(), 0);
        spectro_push_f32(s, x.as_ptr(), -5);
        assert_eq!(spectro_take_columns(s, cols.as_mut_ptr(), 1), 0);
        spectro_free(s);
    }
}

#[test]
fn a_wild_configuration_is_clamped_rather_than_trusted() {
    unsafe {
        let s = spectro_new();
        spectro_configure(s, SR, -1, -1, -5, 10.0, 20_000.0, -96.0, 0.0);
        assert_eq!(spectro_bands(s), 1, "no bands is one band, not a divide by zero");
        spectro_configure(s, SR, FFT, HOP, 100_000, 10.0, 20_000.0, -96.0, 0.0);
        assert_eq!(spectro_bands(s), 1024, "and a silly count is capped");
        spectro_configure(s, SR, FFT, HOP, 64, 10.0, 20_000.0, -96.0, 0.0);
        assert_eq!(spectro_bands(s), 64, "a sane one is taken as asked");
        spectro_free(s);
    }
}

#[test]
fn a_tone_lands_in_its_band_and_silence_is_the_floor() {
    unsafe {
        let s = spectro_new();
        spectro_configure(s, SR, FFT, HOP, BANDS as c_int, 10.0, 20_000.0, -96.0, 0.0);
        let hz = band_hz(s);
        let mut cols = vec![0u8; BANDS * COLUMN_CAPACITY];
        let mut phase = 0.0;

        push_sine(s, 1000.0, 1.0, 16_384, &mut phase);
        let got = spectro_take_columns(s, cols.as_mut_ptr(), COLUMN_CAPACITY as c_int) as usize;
        assert!(got > 0, "a tone produced columns");
        let last = &cols[(got - 1) * BANDS..got * BANDS];
        let peak = peak_band(last);
        assert!(peak.abs_diff(nearest_band(&hz, 1000.0)) <= 1, "the peak is the 1 kHz band");
        assert!(last[peak] > 250, "a full-scale tone reaches the top of the ramp");

        /* Long enough to flush the window entirely. */
        let quiet = [0f32; 512];
        for _ in 0..64 {
            spectro_push_f32(s, quiet.as_ptr(), 512);
        }
        let got = spectro_take_columns(s, cols.as_mut_ptr(), COLUMN_CAPACITY as c_int) as usize;
        assert!(got > 0);
        assert!(cols[(got - 1) * BANDS..got * BANDS].iter().all(|&b| b == 0), "silence is exactly the floor");
        spectro_free(s);
    }
}

#[test]
fn the_range_moves_while_audio_runs_and_an_undrawable_one_is_ignored() {
    unsafe {
        let s = spectro_new();
        let mut cols = vec![0u8; BANDS * COLUMN_CAPACITY];
        let mut phase = 0.0;
        push_sine(s, 1000.0, 1.0, 16_384, &mut phase);

        spectro_set_range(s, 200.0, 4000.0);
        let hz = band_hz(s);
        assert!((hz[0] - 200.0).abs() < 4.0, "the zoomed axis starts at 200 Hz");
        assert!((hz[BANDS - 1] - 4000.0).abs() < 80.0, "and ends at 4 kHz");
        assert_eq!(spectro_take_columns(s, cols.as_mut_ptr(), COLUMN_CAPACITY as c_int), 0,
                   "no column measured on the old scale survives");

        spectro_set_range(s, 0.0, 100.0);
        spectro_set_range(s, 1000.0, 1000.0);
        spectro_set_range(s, f32::NAN, 1000.0);
        assert!((band_hz(s)[0] - 200.0).abs() < 4.0, "an undrawable range left the axis alone");
        spectro_free(s);
    }
}

#[test]
fn an_undrained_ring_drops_and_says_so() {
    unsafe {
        let s = spectro_new();
        let mut cols = vec![0u8; BANDS * COLUMN_CAPACITY];
        let mut phase = 0.0;
        push_sine(s, 1000.0, 0.5, HOP as usize * (COLUMN_CAPACITY + 32) + FFT as usize, &mut phase);
        assert!(spectro_dropped(s) > 0, "an undrained ring reported drops");
        let got = spectro_take_columns(s, cols.as_mut_ptr(), COLUMN_CAPACITY as c_int);
        assert_eq!(got as usize, COLUMN_CAPACITY, "a full ring gives back its whole capacity");

        /* Reconfiguring is a fresh analyzer: its counters start again. */
        spectro_configure(s, SR, FFT, HOP, BANDS as c_int, 10.0, 20_000.0, -96.0, 0.0);
        assert_eq!(spectro_dropped(s), 0);
        spectro_free(s);
    }
}

/* --------------------------------------------------------------- receiver */

const OWN: c_int = 0; /* SRECV_OWN */

fn make() -> *mut Srecv {
    srecv_new(SR, FFT, HOP, BANDS as c_int, 10.0, 20_000.0, -96.0, 0.0)
}

/// Push one block of tone into the own channel and pump it on this thread.
unsafe fn feed(r: *mut Srecv, n: usize, phase: &mut f64) -> c_int {
    let block = sine(1000.0, 0.5, n, phase);
    srecv_push_own(r, block.as_ptr(), n as c_int);
    srecv_pump(r)
}

#[test]
fn a_null_receiver_is_a_no_op_everywhere() {
    let mut buf = vec![0xAAu8; BANDS];
    let mut hz = [0f32; 4];
    let mut clashed = 99;
    let view = [OWN];
    let x = [0.5f32; 8];
    unsafe {
        srecv_free(null_mut());
        srecv_push_own(null_mut(), x.as_ptr(), 8);
        srecv_set_sources(null_mut(), null(), 0);
        srecv_set_range(null(), 10.0, 20_000.0);
        srecv_set_clash(null_mut(), -60.0, 12.0);
        srecv_clash(null(), buf.as_ptr(), buf.as_ptr(), buf.as_mut_ptr(), 1);
        srecv_sum(null(), null(), 0, buf.as_mut_ptr(), 1);
        assert_eq!(srecv_start(null_mut()), 0);
        assert_eq!(srecv_pump(null_mut()), 0);
        assert_eq!(srecv_ready(null()), 0);
        assert_eq!(srecv_channels(null()), 0);
        assert_eq!(srecv_slot_of(null(), 1), 0);
        assert_eq!(srecv_rate_mismatch(null(), 1), 0);
        assert_eq!(srecv_starved(null(), 1), 0);
        assert_eq!(srecv_dropped(null(), 0), 0);
        assert_eq!(srecv_bands(null()), 0);
        assert_eq!(srecv_band_hz(null(), hz.as_mut_ptr(), 4), 0);
        assert_eq!(srecv_take_columns(null_mut(), OWN, buf.as_mut_ptr(), 1), 0);
        let n = srecv_frame(null_mut(), view.as_ptr(), 1, -1, -1, buf.as_mut_ptr(), null_mut(), 1, &mut clashed);
        assert_eq!((n, clashed), (0, 0), "a null receiver frames nothing, and says no clash");
    }
    assert!(buf.iter().all(|&b| b == 0xAA), "a null receiver wrote columns");
}

#[test]
fn a_new_receiver_has_only_its_own_channel() {
    assert!(srecv_max_sources() >= 2, "more than the own channel");
    assert_eq!(srecv_max_sources(), spectro_recv::MAX_SOURCES as c_int);
    unsafe {
        let r = make();
        assert!(!r.is_null());
        assert_eq!(srecv_channels(r), 1);
        assert_eq!(srecv_slot_of(r, OWN), 0, "the own channel has no slot");
        assert_eq!(srecv_slot_of(r, -1), 0, "a negative channel is nobody's");
        assert_eq!(srecv_rate_mismatch(r, OWN), 0, "the own channel cannot mismatch itself");
        assert_eq!(srecv_rate_mismatch(r, -1), 0);
        assert_eq!(srecv_starved(r, OWN), 0, "the own channel sets the pace; it cannot starve");
        assert_eq!(srecv_starved(r, -1), 0);
        assert_eq!(srecv_dropped(r, -1), 0);
        assert_eq!(srecv_bands(r), BANDS as c_int);

        let mut hz = vec![0f32; BANDS];
        assert_eq!(srecv_band_hz(r, hz.as_mut_ptr(), BANDS as c_int), BANDS as c_int);
        assert!((hz[0] - 10.0).abs() < 1.0 && hz.windows(2).all(|w| w[1] > w[0]));
        assert_eq!(srecv_band_hz(r, null_mut(), 4), 0);
        assert_eq!(srecv_band_hz(r, hz.as_mut_ptr(), 0), 0);

        /* The range request is honoured the same way as the analyzer's. */
        srecv_set_range(r, 200.0, 4000.0);
        srecv_band_hz(r, hz.as_mut_ptr(), BANDS as c_int);
        assert!((hz[0] - 200.0).abs() < 4.0, "the receiver's axis follows its range");
        srecv_free(r);
    }
}

#[test]
fn the_own_channel_alone_makes_a_picture() {
    unsafe {
        let r = make();
        let mut cols = vec![0u8; BANDS * 32];
        let mut phase = 0.0;
        let mut got = 0;
        let mut pumped = 0;
        for _ in 0..40 {
            pumped += feed(r, 2048, &mut phase);
            got += srecv_take_columns(r, OWN, cols.as_mut_ptr(), 32);
        }
        assert_eq!(pumped, 40 * 2048, "with no thread, the pump runs here and says how much");
        assert!(got > 0, "the own channel produced columns");
        assert_eq!(srecv_dropped(r, OWN), 0, "nothing dropped while draining every tick");

        /* The degenerate drains take nothing. */
        feed(r, 8192, &mut phase);
        assert!(srecv_ready(r) > 0);
        assert_eq!(srecv_take_columns(r, -1, cols.as_mut_ptr(), 32), 0);
        assert_eq!(srecv_take_columns(r, OWN, null_mut(), 32), 0);
        assert_eq!(srecv_take_columns(r, OWN, cols.as_mut_ptr(), 0), 0);
        assert!(srecv_ready(r) > 0, "and left the columns where they were");
        srecv_free(r);
    }
}

#[test]
fn a_frame_sends_the_view_and_no_clash_without_a_partner() {
    unsafe {
        let r = make();
        let mut sum = vec![0u8; BANDS * 32];
        let mut clash = vec![0u8; BANDS * 32];
        /* A negative channel in the view is skipped, not indexed. */
        let view = [OWN, -3];
        let mut phase = 0.0;
        let mut framed = 0;
        let mut clashed = -1;
        for _ in 0..10 {
            feed(r, 2048, &mut phase);
            framed += srecv_frame(r, view.as_ptr(), 2, OWN, 1, sum.as_mut_ptr(), clash.as_mut_ptr(), 32, &mut clashed);
        }
        assert!(framed > 0, "srecv_frame sends the view");
        assert_eq!(clashed, 0, "and no clash against a channel that is not open");
        assert!(sum.iter().any(|&b| b > 0), "the viewed tone is in the sum");

        /* With nowhere to put them, the columns are drained and dropped. */
        feed(r, 2048, &mut phase);
        assert_eq!(srecv_frame(r, view.as_ptr(), 1, -1, -1, null_mut(), null_mut(), 32, null_mut()), 0);
        assert_eq!(srecv_ready(r), 0);

        /* No room is no frame, and the clash count is still answered. */
        clashed = 99;
        assert_eq!(srecv_frame(r, view.as_ptr(), 1, -1, -1, sum.as_mut_ptr(), null_mut(), 0, &mut clashed), 0);
        assert_eq!(clashed, 0);
        /* A view with nothing drawable in it sends nothing -- but still
         * drains, so the rings cannot fill behind an empty view. */
        feed(r, 8192, &mut phase);
        assert!(srecv_ready(r) > 0);
        assert_eq!(srecv_frame(r, null(), 0, -1, -1, sum.as_mut_ptr(), null_mut(), 32, null_mut()), 0);
        assert_eq!(srecv_ready(r), 0);
        srecv_free(r);
    }
}

#[test]
fn a_clash_needs_both_sources_loud() {
    unsafe {
        let r = make();
        let a = vec![220u8; BANDS];
        let mut b = vec![220u8; BANDS];
        let mut out = vec![0u8; BANDS];
        srecv_set_clash(r, -60.0, 12.0);
        srecv_clash(r, a.as_ptr(), b.as_ptr(), out.as_mut_ptr(), 1);
        assert!(out.iter().all(|&x| x > 0), "two matched sources clash across the column");

        b.fill(0);
        srecv_clash(r, a.as_ptr(), b.as_ptr(), out.as_mut_ptr(), 1);
        assert!(out.iter().all(|&x| x == 0), "a source against silence is not a clash");

        /* A degenerate call writes nothing. */
        out.fill(0xAA);
        srecv_clash(r, a.as_ptr(), null(), out.as_mut_ptr(), 1);
        srecv_clash(r, a.as_ptr(), a.as_ptr(), out.as_mut_ptr(), 0);
        assert!(out.iter().all(|&x| x == 0xAA));
        srecv_free(r);
    }
}

#[test]
fn summing_is_done_in_power_not_in_bytes() {
    /* A byte is linear in dB, so adding bytes would multiply amplitudes: two
     * equal sources must come out about +3 dB, which is ~8 bytes at
     * 96 dB / 255 steps. */
    unsafe {
        let r = make();
        let half = 128u8;
        let a = vec![half; BANDS];
        let silent = vec![0u8; BANDS];
        let mut out = vec![0u8; BANDS];

        let two = [a.as_ptr(), a.as_ptr()];
        srecv_sum(r, two.as_ptr(), 2, out.as_mut_ptr(), 1);
        let lift = out[0] as i32 - half as i32;
        assert!((6..=10).contains(&lift), "two equal sources lifted by {lift} bytes");

        let one = [a.as_ptr()];
        srecv_sum(r, one.as_ptr(), 1, out.as_mut_ptr(), 1);
        assert!((out[0] as i32 - half as i32).abs() <= 1, "one source is itself");

        /* A null source is skipped, and silence adds nothing. */
        let mixed = [a.as_ptr(), null(), silent.as_ptr()];
        srecv_sum(r, mixed.as_ptr(), 3, out.as_mut_ptr(), 1);
        assert!((out[0] as i32 - half as i32).abs() <= 1, "silence moved a source that was there");

        srecv_sum(r, null(), 0, out.as_mut_ptr(), 1);
        assert!(out.iter().all(|&x| x == 0), "summing nothing is silence");

        /* No output or no columns: nothing written. */
        out.fill(0xAA);
        srecv_sum(r, two.as_ptr(), 2, null_mut(), 1);
        srecv_sum(r, two.as_ptr(), 2, out.as_mut_ptr(), 0);
        assert!(out.iter().all(|&x| x == 0xAA));
        srecv_free(r);
    }
}

#[test]
fn the_source_list_sizes_itself_and_a_dead_slot_opens_nothing() {
    unsafe {
        let need = srecv_slots(null_mut(), 0);
        assert!(need >= 0, "sizing the source list did not fail");
        let mut text = vec![0xAAu8; need as usize + 1];
        assert_eq!(srecv_slots(text.as_mut_ptr(), need + 1), need, "asking twice gave the same size");
        assert_eq!(text[need as usize], 0, "the list is terminated");

        /* A buffer too small still gets a terminator and the size it needs. */
        let mut one = [0xAAu8; 1];
        assert_eq!(srecv_slots(one.as_mut_ptr(), 1), need);
        assert_eq!(one[0], 0);

        let r = make();
        /* A slot nobody in this test binary ever claims. */
        let nothing = [13u32];
        srecv_set_sources(r, nothing.as_ptr(), 1);
        assert_eq!(srecv_channels(r), 1, "a dead slot was opened");
        srecv_set_sources(r, null(), 3);
        assert_eq!(srecv_channels(r), 1, "clearing the selection left something open");
        srecv_free(r);
    }
}

#[test]
fn started_the_analysis_runs_on_its_own_thread() {
    unsafe {
        let r = make();
        assert_eq!(srecv_start(r), 1, "the analysis thread started");
        assert_eq!(srecv_start(r), 1, "and starting it again is not an error");

        /* Paced by what has been drawn rather than by the clock, so a slow or
         * descheduled worker cannot overflow the ring: each block waits (up to
         * a bound that only a broken build reaches) until the thread has drawn
         * to within three blocks of it. 8192-point window, hop 1024. */
        let mut cols = vec![0u8; BANDS * 32];
        let mut phase = 0.0;
        let (mut drawn, mut pumped) = (0, 0);
        for i in 1..=40 {
            pumped += feed(r, 1024, &mut phase);
            let want = if i >= 11 { i - 10 } else { 0 };
            let mut waited = 0;
            loop {
                let avail = srecv_ready(r).min(32);
                drawn += srecv_take_columns(r, OWN, cols.as_mut_ptr(), avail);
                if drawn >= want {
                    break;
                }
                waited += 1;
                assert!(waited < 20_000, "the thread stopped drawing");
                std::thread::sleep(std::time::Duration::from_millis(1));
            }
        }
        assert_eq!(pumped, 0, "srecv_pump left the work to the thread");
        assert!(drawn > 0, "the thread produced columns");
        assert_eq!(srecv_dropped(r, OWN), 0, "and kept up with the feed");
        /* Freeing a running receiver joins its thread; hanging here is the
         * failure. */
        srecv_free(r);
    }
}

/*
 * TWO THREADS CHOOSING SOURCES AT ONCE. The Spectrogram once did exactly this:
 * a host restoring state on its own thread called srecv_set_sources beside the
 * main thread's, and the worker's one-plan hand-over gave one caller the other's
 * plan and left the other parked forever -- auval -stress hung on it. The ABI
 * now serialises the message side, so this must finish, and each caller must
 * leave behind a receiver in a state some caller asked for.
 *
 * A HANG IS THE FAILURE, so it is measured rather than waited out: the callers
 * report through a channel and the test gives up after a bound that only a
 * deadlock reaches, leaving the stuck threads behind for the process exit.
 */
#[test]
fn concurrent_callers_choosing_sources_neither_hang_nor_steal() {
    use std::sync::mpsc;
    use std::time::Duration;

    /* A raw pointer is not Send; the ABI is what is being tested for it. */
    #[derive(Clone, Copy)]
    struct Handle(*mut Srecv);
    unsafe impl Send for Handle {}

    const THREADS: usize = 8;
    const CALLS: usize = 25;

    let r = Handle(make());
    unsafe {
        assert_eq!(srecv_start(r.0), 1, "the analysis thread started");
    }
    let (tx, rx) = mpsc::channel();
    for t in 0..THREADS {
        let tx = tx.clone();
        std::thread::spawn(move || {
            let r = r;
            for i in 0..CALLS {
                /* Slots nobody sends on: opening one fails, so every plan
                 * reaches the worker and the hand-over itself is what races. */
                let slots = [1 + ((t + i) % 16) as u32, 1 + ((t * 7 + i) % 16) as u32];
                unsafe { srecv_set_sources(r.0, slots.as_ptr(), slots.len() as c_int) };
            }
            let _ = tx.send(t);
        });
    }
    drop(tx);
    let mut finished = 0;
    while finished < THREADS {
        match rx.recv_timeout(Duration::from_secs(10)) {
            Ok(_) => finished += 1,
            Err(_) => panic!("{} of {THREADS} callers never came back from srecv_set_sources", THREADS - finished),
        }
    }
    unsafe {
        assert_eq!(srecv_channels(r.0), 1, "no bus was ever open, so only the own channel remains");
        srecv_free(r.0);
    }
}
