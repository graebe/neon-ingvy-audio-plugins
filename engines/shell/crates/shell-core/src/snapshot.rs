/*!
A triple buffer: one writer publishes whole values, one reader takes the
latest, and neither ever waits for the other.

Three slots, each owned by exactly one party at any moment: the writer's back
slot, the reader's front slot, and the middle one that is handed across. The
writer fills its back slot and swaps it into the middle with a FRESH mark; the
reader, seeing the mark, swaps its front slot for the middle. The swap is one
atomic exchange on a byte, so a slot is never shared -- which is what a seqlock
cannot promise in Rust without making every byte of the payload an atomic.

A reader that is slower than the writer skips intermediate values, which is
exactly what a readout wants: the latest, whole, never torn.
*/

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicU8, Ordering};

const FRESH: u8 = 0b100;
const INDEX: u8 = 0b011;

pub struct TripleBuffer<T> {
    slots: [UnsafeCell<T>; 3],
    middle: AtomicU8,
    back: UnsafeCell<u8>,
    front: UnsafeCell<u8>,
}

// SAFETY: every slot is owned by exactly one side at a time, and ownership
// moves only through the atomic `middle`; see the module comment.
unsafe impl<T: Send> Sync for TripleBuffer<T> {}
unsafe impl<T: Send> Send for TripleBuffer<T> {}

impl<T> TripleBuffer<T> {
    /// Allocates whatever `make` allocates, three times; construction only.
    pub fn new(mut make: impl FnMut() -> T) -> TripleBuffer<T> {
        TripleBuffer {
            slots: [
                UnsafeCell::new(make()),
                UnsafeCell::new(make()),
                UnsafeCell::new(make()),
            ],
            back: UnsafeCell::new(0),
            middle: AtomicU8::new(1),
            front: UnsafeCell::new(2),
        }
    }

    /// The slot the writer fills next. Its previous contents are whatever was
    /// published two swaps ago, so a writer must rewrite all of it.
    ///
    /// # Safety
    /// Writer side only, and one writer at a time.
    #[allow(clippy::mut_from_ref)]
    pub unsafe fn back(&self) -> &mut T {
        &mut *self.slots[*self.back.get() as usize].get()
    }

    /// Hand the back slot to the reader. Allocation-free and wait-free.
    ///
    /// # Safety
    /// Writer side only, and one writer at a time.
    pub unsafe fn publish(&self) {
        let back = *self.back.get();
        let prev = self.middle.swap(back | FRESH, Ordering::AcqRel);
        *self.back.get() = prev & INDEX;
    }

    /// The latest published value, and whether it is newer than the one the
    /// previous call returned.
    ///
    /// # Safety
    /// Reader side only, and one reader at a time. The reference must be
    /// dropped before the next call.
    pub unsafe fn latest(&self) -> (&T, bool) {
        let fresh = self.middle.load(Ordering::Relaxed) & FRESH != 0;
        if fresh {
            let front = *self.front.get();
            let prev = self.middle.swap(front, Ordering::AcqRel);
            *self.front.get() = prev & INDEX;
        }
        (&*self.slots[*self.front.get() as usize].get(), fresh)
    }
}
