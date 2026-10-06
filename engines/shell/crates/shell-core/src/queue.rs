// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
A bounded, allocation-free single-producer single-consumer queue of byte
records.

Variable-length records rather than fixed slots, because the commands it
carries range from a four-byte cursor move to an 8 KB pasted patch: fixed
slots sized for the paste would cost 8 KB per step edit.

A record is a little-endian `u32` length followed by the bytes, and may wrap
around the end of the ring. The producer publishes `tail` with Release after
writing; the consumer publishes `head` with Release after reading. Each side
owns the bytes between the two positions that belong to it, so no byte is
ever touched by both threads at once.
*/

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicUsize, Ordering};

const LEN: usize = 4;

pub struct Queue {
    buf: Box<[UnsafeCell<u8>]>,
    mask: usize,
    max_record: usize,
    /// Consumer position; written by the consumer only.
    head: AtomicUsize,
    /// Producer position; written by the producer only.
    tail: AtomicUsize,
}

// SAFETY: the bytes are partitioned between the two sides by `head` and
// `tail`, as the module comment describes; the positions are atomics.
unsafe impl Sync for Queue {}
unsafe impl Send for Queue {}

impl Queue {
    /// `capacity` is rounded up to a power of two and to at least one record
    /// of `max_record` bytes. Allocates; construction only.
    pub fn new(capacity: usize, max_record: usize) -> Queue {
        let cap = capacity.max(max_record + LEN).max(16).next_power_of_two();
        let buf = (0..cap).map(|_| UnsafeCell::new(0u8)).collect::<Vec<_>>();
        Queue {
            buf: buf.into_boxed_slice(),
            mask: cap - 1,
            max_record,
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
        }
    }

    pub fn capacity(&self) -> usize {
        self.mask + 1
    }

    pub fn max_record(&self) -> usize {
        self.max_record
    }

    fn base(&self) -> *mut u8 {
        UnsafeCell::raw_get(self.buf.as_ptr())
    }

    /* Copy in and out across the wrap point: at most two segments each. */
    unsafe fn write_at(&self, pos: usize, src: &[u8]) {
        let at = pos & self.mask;
        let first = src.len().min(self.capacity() - at);
        core::ptr::copy_nonoverlapping(src.as_ptr(), self.base().add(at), first);
        core::ptr::copy_nonoverlapping(src.as_ptr().add(first), self.base(), src.len() - first);
    }

    unsafe fn read_at(&self, pos: usize, dst: &mut [u8]) {
        let at = pos & self.mask;
        let first = dst.len().min(self.capacity() - at);
        core::ptr::copy_nonoverlapping(self.base().add(at), dst.as_mut_ptr(), first);
        core::ptr::copy_nonoverlapping(self.base(), dst.as_mut_ptr().add(first), dst.len() - first);
    }

    /// Append one record. False when it does not fit right now, or is longer
    /// than `max_record` and never will.
    ///
    /// # Safety
    /// At most one thread may be the producer at any moment.
    pub unsafe fn push(&self, parts: &[&[u8]]) -> bool {
        let n: usize = parts.iter().map(|p| p.len()).sum();
        if n > self.max_record {
            return false;
        }
        let tail = self.tail.load(Ordering::Relaxed);
        let head = self.head.load(Ordering::Acquire);
        let used = tail.wrapping_sub(head);
        if self.capacity() - used < LEN + n {
            return false;
        }
        self.write_at(tail, &(n as u32).to_le_bytes());
        let mut at = tail.wrapping_add(LEN);
        for p in parts {
            self.write_at(at, p);
            at = at.wrapping_add(p.len());
        }
        self.tail.store(at, Ordering::Release);
        true
    }

    /// Take the oldest record into `out`, returning its length. `out` must
    /// hold `max_record` bytes; a shorter one gets the record truncated.
    /// Allocation-free.
    ///
    /// # Safety
    /// At most one thread may be the consumer at any moment.
    pub unsafe fn pop(&self, out: &mut [u8]) -> Option<usize> {
        let head = self.head.load(Ordering::Relaxed);
        let tail = self.tail.load(Ordering::Acquire);
        if head == tail {
            return None;
        }
        let mut len = [0u8; LEN];
        self.read_at(head, &mut len);
        let n = u32::from_le_bytes(len) as usize;
        let take = n.min(out.len());
        self.read_at(head.wrapping_add(LEN), &mut out[..take]);
        self.head.store(head.wrapping_add(LEN + n), Ordering::Release);
        Some(take)
    }

    pub fn is_empty(&self) -> bool {
        self.head.load(Ordering::Acquire) == self.tail.load(Ordering::Acquire)
    }
}
