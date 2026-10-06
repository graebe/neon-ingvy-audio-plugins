// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
Where what the audio thread lets go of is freed.

basedrop's `Shared` is an `Arc` whose last release does not free: it links the
allocation onto its collector's queue -- a swap and a store, nothing allocated,
no lock -- and the collector frees it when it is next asked to. So the audio
thread may drop the last reference to anything, and the free still happens
here, on a thread that is allowed to free.

This is the collector and the handle that allocates into it, owned together,
because basedrop asks two things of whoever owns them that are easy to get
wrong apart:

- **A collector that is simply dropped leaks.** It frees its own bookkeeping
  only through `try_cleanup`, and that refuses while any handle or allocation
  is alive. So the handle is dropped first and the queue collected, here.
- **It must never be freed under a live allocation.** An allocation's last
  release writes into its collector's queue. When something is still alive at
  teardown -- a `Shared` kept past its owner -- `try_cleanup` refuses and the
  collector is leaked instead, so that release still finds its queue.

The owner declares this field LAST, so everything that might hold a `Shared`
is dropped before it collects.
*/

use basedrop::{Collector, Handle};
use core::mem::ManuallyDrop;
use std::sync::{Mutex, PoisonError};

pub(crate) struct Reclaim {
    handle: ManuallyDrop<Handle>,
    /* Behind a lock because collecting takes `&mut` and the owner is shared;
     * only the threads that may free ever take it, never the audio thread. */
    collector: ManuallyDrop<Mutex<Collector>>,
}

impl Reclaim {
    /// Allocates; construction only.
    pub(crate) fn new() -> Reclaim {
        let collector = Collector::new();
        Reclaim {
            handle: ManuallyDrop::new(collector.handle()),
            collector: ManuallyDrop::new(Mutex::new(collector)),
        }
    }

    /// What a `Shared` freed here is allocated with.
    pub(crate) fn handle(&self) -> &Handle {
        &self.handle
    }

    /// Free everything let go of so far, and return how many allocations are
    /// still alive. Never the audio thread: this is where the frees happen.
    pub(crate) fn collect(&self) -> usize {
        let mut c = self.collector.lock().unwrap_or_else(PoisonError::into_inner);
        c.collect();
        c.alloc_count()
    }
}

impl Drop for Reclaim {
    fn drop(&mut self) {
        // SAFETY: each field is taken exactly once, here, and never used again.
        let collector = unsafe {
            ManuallyDrop::drop(&mut self.handle);
            ManuallyDrop::take(&mut self.collector)
        };
        let mut collector = collector.into_inner().unwrap_or_else(PoisonError::into_inner);
        collector.collect();
        /* Refused while anything is alive: the collector is then leaked, never
         * freed under that allocation -- see the module comment. */
        let _ = collector.try_cleanup();
    }
}
