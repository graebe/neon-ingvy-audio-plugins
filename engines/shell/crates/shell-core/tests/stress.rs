/*
 * The bridge and the handoff with real threads on both sides.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * What each test would catch: a command lost, duplicated or reordered between
 * the threads (the tally's sum), a frame read while it was being written (the
 * payload that must be uniform), and an object freed while the audio thread
 * still held it (the canary that must never read as dead).
 */

use shell_core::{Bridge, Handoff, Model};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Arc;
use std::thread;

const PAYLOAD: usize = 256;

struct Tally {
    count: u64,
    sum: u64,
}

struct TallyFrame {
    count: u64,
    sum: u64,
    /* Every word is `count`; a torn read shows as two different values. */
    payload: Box<[u64]>,
}

impl Model for Tally {
    type Frame = TallyFrame;
    fn new_frame(&self) -> TallyFrame {
        TallyFrame { count: 0, sum: 0, payload: vec![0; PAYLOAD].into_boxed_slice() }
    }
    fn apply(&mut self, cmd: &[u8]) {
        let mut b = [0u8; 8];
        b.copy_from_slice(&cmd[..8]);
        self.count += 1;
        self.sum += u64::from_le_bytes(b);
    }
    fn publish(&self, f: &mut TallyFrame) {
        f.count = self.count;
        for w in f.payload.iter_mut() {
            *w = self.count;
        }
        f.sum = self.sum;
    }
    fn restore(&mut self, f: &TallyFrame) {
        self.count = f.count;
        self.sum = f.sum;
    }
}

#[test]
fn commands_and_frames_cross_two_threads_intact() {
    const N: u64 = 50_000;
    let b = Arc::new(Bridge::new(
        Tally { count: 0, sum: 0 },
        Some(Tally { count: 0, sum: 0 }),
        /* Small on purpose, so the outbox and the full-queue path are used. */
        512,
        8,
        64,
    ));
    let stop = Arc::new(AtomicBool::new(false));

    let audio = {
        let b = Arc::clone(&b);
        let stop = Arc::clone(&stop);
        thread::spawn(move || {
            while !stop.load(Ordering::Acquire) {
                unsafe {
                    b.begin();
                    b.end(32);
                }
            }
            /* One last block, so everything posted is applied. */
            unsafe {
                b.begin();
                b.end(32);
            }
        })
    };

    let mut last_frame = 0;
    for v in 1..=N {
        assert!(b.post(&v.to_le_bytes()));
        let (count, sum, frame_count) = b.read(|r| {
            let f = r.frame;
            assert!(f.payload.iter().all(|&w| w == f.count), "a torn frame");
            assert_eq!(f.sum, f.count * (f.count + 1) / 2, "a frame out of step with itself");
            match r.pending {
                Some(v) => (v.count, v.sum, f.count),
                None => (f.count, f.sum, f.count),
            }
        });
        /* THE PREDICTION IS EXACT: whatever the audio thread has or has not
         * drained, the reader sees every command posted so far. */
        assert_eq!(count, v);
        assert_eq!(sum, v * (v + 1) / 2);
        assert!(frame_count >= last_frame, "frames never go backwards");
        last_frame = frame_count;
    }

    while b.outstanding() > 0 {
        thread::yield_now();
    }
    stop.store(true, Ordering::Release);
    audio.join().unwrap();
    assert_eq!(b.read(|r| (r.frame.count, r.pending.is_none())), (N, true));
}

/* ---------------------------------------------------------------- handoff */

const ALIVE: u64 = 0xA11C_E0A1_1CE0_A11C;
const DEAD: u64 = 0xDEAD_DEAD_DEAD_DEAD;

struct Canary(AtomicU64);

fn free_canary(p: *mut Canary) {
    unsafe {
        /* Poisoned before it is freed, so a use after this point is a
         * deterministic failure rather than a read of recycled memory that
         * happens to look fine. */
        (*p).0.store(DEAD, Ordering::SeqCst);
        drop(Box::from_raw(p));
    }
}

#[test]
fn the_audio_thread_never_holds_a_freed_object() {
    let h = Arc::new(Handoff::<Canary>::new());
    let stop = Arc::new(AtomicBool::new(false));
    let blocks = Arc::new(AtomicU64::new(0));

    let audio = {
        let h = Arc::clone(&h);
        let stop = Arc::clone(&stop);
        let blocks = Arc::clone(&blocks);
        thread::spawn(move || {
            while !stop.load(Ordering::Acquire) {
                let p = h.acquire();
                if !p.is_null() {
                    for _ in 0..16 {
                        assert_eq!(unsafe { (*p).0.load(Ordering::SeqCst) }, ALIVE,
                                   "the audio thread saw a freed object");
                    }
                }
                h.release();
                blocks.fetch_add(1, Ordering::Relaxed);
            }
        })
    };

    /* The audio thread is running before the swaps start -- otherwise nothing
     * overlapped and nothing was tested. */
    while blocks.load(Ordering::Relaxed) == 0 {
        thread::yield_now();
    }
    for _ in 0..20_000 {
        h.set(Box::into_raw(Box::new(Canary(AtomicU64::new(ALIVE)))));
        h.collect(free_canary);
    }
    /* Deferral must be temporary: once the audio thread lets go, everything
     * retired is freed. */
    let mut spins = 0;
    while h.collect(free_canary) > 0 {
        thread::yield_now();
        spins += 1;
        assert!(spins < 1_000_000, "a retired object was never freed");
    }
    stop.store(true, Ordering::Release);
    audio.join().unwrap();

    let mut h = Arc::try_unwrap(h).ok().expect("the audio thread has gone");
    h.clear(free_canary);
}
