// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The segment header -- the layout Rust and C both agree on.
 *
 * ONE WRITER WRITES EVERY FIELD HERE, except `owner`, which is written only by
 * compare-and-swap. Readers only read -- they map the segment read-only.
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

use core::sync::atomic::{fence, AtomicU32, AtomicU64, AtomicU8, Ordering};

/* 'NIB1' -- Neon Ingvy Bus. Written LAST by whoever creates the segment, so a
 * reader that sees it knows every other field is already there. A reader that
 * maps a half-initialised segment and trusts `ring_frames` would compute a size
 * from a zero and read the whole thing as silence. */
pub const MAGIC: u32 = 0x4E49_4231;

/* Bumped when the layout OR the protocol changes shape. A reader REFUSES a
 * mismatch rather than interpreting it: an old Spectrogram reading a new
 * Listen-In's segment with shifted field offsets is not a degraded picture, it
 * is noise at full scale into somebody's monitors. And a writer that finds a
 * segment of another version replaces it rather than share a slot under two
 * different claim protocols.
 *
 * 2: ownership is one packed word (`owner`), the segment carries its
 *    `incarnation`, and the heartbeat is gone. */
pub const ABI_VERSION: u32 = 2;

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
 * cannot drift, and the assertion below fails the build if the struct ever
 * outgrows it.
 */
pub const DATA_OFFSET: usize = 128;

pub const LABEL_BYTES: usize = 32;

/// Total bytes in a segment: the header, then the interleaved ring.
pub const fn segment_size() -> usize {
    DATA_OFFSET + (RING_FRAMES as usize) * (CHANNELS as usize) * core::mem::size_of::<f32>()
}

/*
 * THE OWNER WORD: who holds the slot, and how many times it has been claimed,
 * in ONE atomic so that a claim is a single compare-and-swap.
 *
 *   high 32 bits  claim count, bumped by every successful claim
 *   low 32 bits   the holder's pid, or 0 when the slot is free
 *
 * Keeping state and pid in separate words is what let two reclaimers of the
 * same dead pid both win: the second one's "reset to free" landed on the
 * first one's brand-new claim, because the pid that would have told them apart
 * was only written after the claim. Here the pid IS the claim. The count is
 * what keeps a recycled pid from making an old observation look current: a
 * reclaimer that judged pid P dead can only swap out the exact word it judged.
 */
pub const fn owner_word(count: u32, pid: u32) -> u64 {
    ((count as u64) << 32) | pid as u64
}
pub const fn owner_pid(word: u64) -> u32 {
    word as u32
}
pub const fn owner_count(word: u64) -> u32 {
    (word >> 32) as u32
}

/*
 * EVERY FIELD IS AN ATOMIC, the shape fields and the label included.
 *
 * The segment is mapped by several threads and several processes at once, and a
 * `&Header` is handed to all of them; a plain field written through one of those
 * shared references would be a write the compiler is entitled to assume never
 * happens. Atomics say what is actually going on, and cost nothing here: the
 * shape fields are read once per open, the label once per probe.
 */
#[repr(C)]
pub struct Header {
    pub magic: AtomicU32,
    pub abi_version: AtomicU32,
    pub ring_frames: AtomicU32,
    pub channels: AtomicU32,
    pub sample_rate: AtomicU32,
    /* Bumped on every claim and on every sample-rate change. A reader that
     * sees it move throws its cursor away and starts again: the samples
     * behind that cursor were measured under a different regime, and a
     * spectrogram drawn across the seam would show a transient that never
     * happened. */
    pub epoch: AtomicU32,
    pub label_seq: AtomicU32,
    _reserved: AtomicU32,
    /* Which segment this is, set once at creation: two segments that have
     * lived under one name never share it. A reader compares it with whatever
     * the name opens NOW to learn that its own mapping has been orphaned. */
    pub incarnation: AtomicU64,
    /* See `owner_word`. */
    pub owner: AtomicU64,
    /* Total frames ever written, never wrapped. The wrap is applied when
     * indexing, so a reader can subtract two of these and get a true distance
     * -- which is exactly the "how far behind am I" question the ring cannot
     * answer once the indices have wrapped. 64 bits is 4.7 million years at
     * 96 kHz; 32 would have wrapped in twelve hours. */
    pub write_frames: AtomicU64,
    pub label: [AtomicU8; LABEL_BYTES],
}

const _: () = assert!(core::mem::size_of::<Header>() <= DATA_OFFSET);
const _: () = assert!(core::mem::align_of::<Header>() <= 8);

impl Header {
    /// Initialise a freshly created segment. The caller must have zeroed it and
    /// must be the one process whose open created it (`Shm::create_or_open`
    /// said so); `magic` goes last.
    pub fn initialise(&self, sample_rate: u32, incarnation: u64) {
        self.abi_version.store(ABI_VERSION, Ordering::Relaxed);
        self.ring_frames.store(RING_FRAMES, Ordering::Relaxed);
        self.channels.store(CHANNELS, Ordering::Relaxed);
        self.sample_rate.store(sample_rate, Ordering::Relaxed);
        self.epoch.store(1, Ordering::Relaxed);
        self.label_seq.store(0, Ordering::Relaxed);
        self.incarnation.store(incarnation, Ordering::Relaxed);
        self.owner.store(0, Ordering::Relaxed);
        self.write_frames.store(0, Ordering::Relaxed);
        /* LAST, and with Release: everything above must be visible to whoever
         * sees the magic. */
        self.magic.store(MAGIC, Ordering::Release);
    }

    /// Does this look like a segment we understand? Checked by every reader on
    /// every open, because a stale segment from an older build is exactly the
    /// thing that survives a reboot-less upgrade.
    pub fn is_valid(&self) -> bool {
        self.magic.load(Ordering::Acquire) == MAGIC
            && self.abi_version.load(Ordering::Relaxed) == ABI_VERSION
            && self.ring_frames.load(Ordering::Relaxed) == RING_FRAMES
            && self.channels.load(Ordering::Relaxed) == CHANNELS
    }

    /*
     * THE LABEL IS A SEQLOCK, because it is 32 bytes and there is no atomic
     * that wide.
     *
     * The writer makes the sequence odd, writes, and makes it even again. A
     * reader reads the sequence, copies, reads it again, and retries if either
     * is odd or the two differ -- which means it saw a torn write. The retry is
     * bounded: a reader that keeps losing gives up and keeps the name it had,
     * because a stale name in a dropdown is nothing and a spin on the message
     * thread is a beachball.
     *
     * The fences are the standard seqlock pair. A Release STORE only orders what
     * comes before it, so the writer's odd store needs a Release fence AFTER it
     * to keep the byte stores from being seen first; the reader's Acquire fence
     * keeps its byte loads from drifting past the second sequence load. The
     * bytes themselves are relaxed atomics, so a torn read is a wrong answer
     * that gets retried, never undefined behaviour.
     *
     * ONE WRITER: the caller must be the slot's owner, which `Writer` enforces
     * with `&mut self`.
     */
    pub fn set_label(&self, text: &str) {
        let seq = self.label_seq.load(Ordering::Relaxed);
        self.label_seq.store(seq.wrapping_add(1), Ordering::Relaxed);
        fence(Ordering::Release);

        let bytes = truncate_utf8(text, LABEL_BYTES - 1).as_bytes();
        for (i, cell) in self.label.iter().enumerate() {
            cell.store(bytes.get(i).copied().unwrap_or(0), Ordering::Relaxed);
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
            for (o, cell) in out.iter_mut().zip(self.label.iter()) {
                *o = cell.load(Ordering::Relaxed);
            }
            fence(Ordering::Acquire);
            if self.label_seq.load(Ordering::Relaxed) == before {
                out[LABEL_BYTES - 1] = 0;
                return Some(out);
            }
        }
        None
    }
}

/// The longest prefix of `text` that fits in `max` bytes and ends on a
/// character boundary. Cutting inside a character would hand every reader a
/// name that is not UTF-8.
pub fn truncate_utf8(text: &str, max: usize) -> &str {
    if text.len() <= max {
        return text;
    }
    let mut end = max;
    while !text.is_char_boundary(end) {
        end -= 1;
    }
    &text[..end]
}
