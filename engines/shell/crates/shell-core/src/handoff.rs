// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
An object the main thread builds and frees, lent to the audio thread.

The shell's resources that allocate or map memory -- a bus pusher, an analyzer
receiver -- have to be created and destroyed on the main thread, and the audio
thread has to use them in between. A plain pointer swap is not enough: the old
object may be mid-use on the audio thread when it is replaced, and freeing it
then is a use-after-free.

So the object is reference-counted, with basedrop's `SharedCell` holding the
live one:

  audio: held = cell.get()   ... use it ...   drop(held)       (one block)
  main:  cell.replace(next)  -- drops the main thread's reference to the old one
         collect()           -- frees whatever nothing references any more

A block's copy keeps a retired object alive until the block lets go of it. And
whoever drops the LAST reference does not free: basedrop queues the object for
its collector (see `reclaim`), and only [`Handoff::collect`] -- the main
thread's -- frees it. So the free never lands on the audio thread, even when the
audio thread is the one that lets go last.

WHY basedrop AND NOT arc-swap. Both are lock-free for a reader. arc-swap's
`load` pays for that with a debt slot in a node it registers for each thread,
allocated on that thread's first load behind a thread-local with a destructor,
and its references are `Arc`s: whichever thread lets go last frees the object,
right there -- for a bus pusher, an unmap on the audio thread. basedrop's
reader is two counter increments and a load (`SharedCell::get`), its last
release a swap and a store onto an intrusive queue, nothing allocated and
nothing freed. Its writer pays instead: `replace` spins until no `get` is in
flight -- a few instructions of the audio thread's -- and the writer is the
main thread.
*/

use crate::reclaim::Reclaim;
use basedrop::{Shared, SharedCell};
use core::cell::UnsafeCell;

pub struct Handoff<T: Send + Sync + 'static> {
    /// The object a block is lent, or `None` for nothing.
    live: SharedCell<Option<T>>,
    /// The audio thread's copy, from `acquire` to `release`. Touched by those
    /// two alone.
    held: UnsafeCell<Option<Shared<Option<T>>>>,
    /// Declared last, so dropped last: by the time it collects, `live` and
    /// `held` have let go of everything.
    reclaim: Reclaim,
}

// SAFETY: `held` is touched only by `acquire` and `release`, whose contract is
// one audio thread at a time; everything else is basedrop's, which is Sync for
// a value that is Send and Sync.
unsafe impl<T: Send + Sync + 'static> Sync for Handoff<T> {}

impl<T: Send + Sync + 'static> Default for Handoff<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T: Send + Sync + 'static> Handoff<T> {
    /// Nothing installed. Allocates; construction only.
    pub fn new() -> Handoff<T> {
        let reclaim = Reclaim::new();
        Handoff {
            live: SharedCell::new(Shared::new(reclaim.handle(), None)),
            held: UnsafeCell::new(None),
            reclaim,
        }
    }

    /// The audio thread's hold on the live object, or `None`. Wait-free and
    /// allocation-free.
    ///
    /// # Safety
    /// The audio thread only, one at a time. Pair it with
    /// [`Handoff::release`] before the block ends, and use the reference only
    /// until then.
    pub unsafe fn acquire(&self) -> Option<&T> {
        let held = &mut *self.held.get();
        *held = Some(self.live.get());
        held.as_deref().and_then(Option::as_ref)
    }

    /// The audio thread is done with what it acquired. Its copy is dropped
    /// here -- when it is the last one, onto the collector's queue rather than
    /// freed. Wait-free and allocation-free.
    ///
    /// # Safety
    /// As [`Handoff::acquire`]; nothing it returned is used afterwards.
    pub unsafe fn release(&self) {
        *self.held.get() = None;
    }

    /// Install `next` and retire the object before it, which
    /// [`Handoff::collect`] frees once no block holds it. Main thread only;
    /// allocates.
    pub fn set(&self, next: Option<T>) {
        drop(self.live.replace(Shared::new(self.reclaim.handle(), next)));
    }

    /// A look at the object last installed. Main thread only.
    pub fn with_current<R>(&self, f: impl FnOnce(Option<&T>) -> R) -> R {
        let current = self.live.get();
        f(Option::as_ref(&current))
    }

    /// Free every retired object no block holds any more, and return how
    /// many are still held. Main thread only.
    pub fn collect(&self) -> usize {
        /* Every allocation still alive is a retired object a block holds,
         * except the one live object. */
        self.reclaim.collect().saturating_sub(1)
    }
}
