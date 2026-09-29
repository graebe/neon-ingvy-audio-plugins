/*
 * The segment header -- the layout Rust and C both agree on.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * ONE WRITER WRITES EVERY FIELD HERE. Readers only read.
 *
 * That is worth stating before the fields, because it is what makes the whole
 * design safe without a single lock: a reader keeps its cursor in its OWN
 * process memory, never in the segment, so no two participants ever write to
 * the same byte. Two readers cannot interfere with each other because neither
 * of them writes anything at all.
 *
 * The one apparent exception is `label`, which is text and cannot be read
 * atomically -- see `label` / `set_label` below for the seqlock that covers it.
 */

use core::sync::atomic::{AtomicU32, AtomicU64, Ordering};

/* 'NIB1' -- Neon Ingvy Bus, layout 1. Written LAST by whoever creates the
 * segment, so a reader that sees it knows every other field is already there.
 * A reader that maps a half-initialised segment and trusts `ring_frames` would
 * compute a size from a zero and read the whole thing as silence. */
pub const MAGIC: u32 = 0x4E49_4231;

/* Bumped when anything above changes shape. A reader REFUSES a mismatch rather
 * than interpreting it: an old Spectrogram reading a new Listen-In's segment
 * with shifted field offsets is not a degraded picture, it is noise at full
 * scale into somebody's monitors. */
pub const ABI_VERSION: u32 = 1;

/* Always stereo, always interleaved. A mono source is duplicated by the sender
 * -- the same choice the Spectrogram's passthrough makes -- so that a reader
 * never has to ask how many channels arrived. */
pub const CHANNELS: u32 = 2;

/*
 * 131072 frames: 2.7 s at 48 kHz, 1.37 s at 96 kHz, 1 MiB of audio per slot.
 *
 * A receiver polling at 60 Hz needs 17 ms of it. The remaining two and a half
 * seconds are slack for an editor that is hidden, stalled behind a modal, or
 * on a laptop that just woke up -- and when even that runs out the reader
 * resyncs and says how much it lost. Falling behind is a reported event here,
 * never a silent wrong answer.
 *
 * A power of two so the wrap is a mask rather than a divide, which matters
 * because the wrap is computed on the audio thread once per block.
 */
pub const RING_FRAMES: u32 = 1 << 17;

/*
 * The audio starts at a fixed offset rather than at `size_of::<Header>()`.
 *
 * C computing `sizeof(struct abus_header)` and Rust computing its own layout
 * agree today, and would keep agreeing right up until someone adds a field and
 * one of the two compilers pads differently. A constant both sides spell out
 * cannot drift, and `header_fits` below fails the build if the struct ever
 * outgrows it.
 */
pub const DATA_OFFSET: usize = 128;

pub const STATE_FREE: u32 = 0;
pub const STATE_CLAIMED: u32 = 1;

pub const LABEL_BYTES: usize = 32;

/// Total bytes in a segment: the header, then the interleaved ring.
pub const fn segment_size() -> usize {
    DATA_OFFSET + (RING_FRAMES as usize) * (CHANNELS as usize) * core::mem::size_of::<f32>()
}

#[repr(C)]
pub struct Header {
    pub magic: AtomicU32,
    pub abi_version: u32,
    pub ring_frames: u32,
    pub channels: u32,
    pub sample_rate: AtomicU32,
    /* Bumped on every claim and on every sample-rate change. A reader that
     * sees it move throws its cursor away and starts again: the samples
     * behind that cursor were measured under a different regime, and a
     * spectrogram drawn across the seam would show a transient that never
     * happened. */
    pub epoch: AtomicU32,
    /* Bumped once per audio block. This is the ONLY evidence a reader has that
     * a writer is alive: a process can die without unlinking, and a pid alone
     * is not enough because pids are reused. */
    pub heartbeat: AtomicU32,
    pub writer_pid: AtomicU32,
    pub state: AtomicU32,
    pub label_seq: AtomicU32,
    _reserved: [u32; 2],
    /* Total frames ever written, never wrapped. The wrap is applied when
     * indexing, so a reader can subtract two of these and get a true distance
     * -- which is exactly the "how far behind am I" question the ring cannot
     * answer once the indices have wrapped. 64 bits is 4.7 million years at
     * 96 kHz; 32 would have wrapped in twelve hours. */
    pub write_frames: AtomicU64,
    pub label: [u8; LABEL_BYTES],
}

const _: () = assert!(core::mem::size_of::<Header>() <= DATA_OFFSET);
const _: () = assert!(core::mem::align_of::<Header>() <= 8);

impl Header {
    /// Initialise a freshly created segment. The caller must have zeroed it and
    /// must be the process that won `O_CREAT | O_EXCL`; `magic` goes last.
    pub fn initialise(&self, sample_rate: u32, pid: u32) {
        self.sample_rate.store(sample_rate, Ordering::Relaxed);
        self.epoch.store(1, Ordering::Relaxed);
        self.heartbeat.store(0, Ordering::Relaxed);
        self.writer_pid.store(pid, Ordering::Relaxed);
        self.state.store(STATE_FREE, Ordering::Relaxed);
        self.label_seq.store(0, Ordering::Relaxed);
        self.write_frames.store(0, Ordering::Relaxed);
        /* The three shape fields are plain, not atomic: they are written here,
         * before `magic`, and never again for the life of the segment. */
        let shape = self as *const Header as *mut Header;
        unsafe {
            (*shape).abi_version = ABI_VERSION;
            (*shape).ring_frames = RING_FRAMES;
            (*shape).channels = CHANNELS;
        }
        /* LAST, and with Release: everything above must be visible to whoever
         * sees the magic. */
        self.magic.store(MAGIC, Ordering::Release);
    }

    /// Does this look like a segment we understand? Checked by every reader on
    /// every open, because a stale segment from an older build is exactly the
    /// thing that survives a reboot-less upgrade.
    pub fn is_valid(&self) -> bool {
        self.magic.load(Ordering::Acquire) == MAGIC
            && self.abi_version == ABI_VERSION
            && self.ring_frames == RING_FRAMES
            && self.channels == CHANNELS
    }

    /*
     * THE LABEL IS A SEQLOCK, because it is 32 bytes and there is no atomic
     * that wide.
     *
     * The writer bumps the sequence to odd, writes, and bumps it to even. A
     * reader reads the sequence, copies, reads it again, and retries if either
     * is odd or the two differ -- which means it saw a torn write. The retry is
     * bounded: a reader that keeps losing gives up and keeps the name it had,
     * because a stale name in a dropdown is nothing and a spin on the message
     * thread is a beachball.
     */
    pub fn set_label(&self, text: &[u8]) {
        let seq = self.label_seq.load(Ordering::Relaxed);
        self.label_seq.store(seq.wrapping_add(1), Ordering::Release);

        let n = core::cmp::min(text.len(), LABEL_BYTES - 1);
        let dst = self.label.as_ptr() as *mut u8;
        unsafe {
            core::ptr::write_bytes(dst, 0, LABEL_BYTES);
            core::ptr::copy_nonoverlapping(text.as_ptr(), dst, n);
        }

        self.label_seq.store(seq.wrapping_add(2), Ordering::Release);
    }

    pub fn label(&self) -> Option<[u8; LABEL_BYTES]> {
        for _ in 0..8 {
            let before = self.label_seq.load(Ordering::Acquire);
            if before & 1 != 0 {
                continue;
            }
            let mut out = [0u8; LABEL_BYTES];
            unsafe {
                core::ptr::copy_nonoverlapping(self.label.as_ptr(), out.as_mut_ptr(), LABEL_BYTES);
            }
            if self.label_seq.load(Ordering::Acquire) == before {
                out[LABEL_BYTES - 1] = 0;
                return Some(out);
            }
        }
        None
    }
}
