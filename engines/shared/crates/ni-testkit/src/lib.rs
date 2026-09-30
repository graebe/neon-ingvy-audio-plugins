/*!
The counting allocator behind every `tests/no_alloc.rs`: the audio thread
allocates nothing, asserted rather than claimed.

A test binary installs it and arms it around exactly the calls the audio
thread makes:

```ignore
#[global_allocator]
static A: ni_testkit::Counting = ni_testkit::Counting;
```

Allocations (and reallocations) and frees are counted apart: a free takes the
same allocator lock a malloc does. The counters are global and cargo runs
tests in threads, so a binary that installs this must hold ONE test.
*/

use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

static ARMED: AtomicBool = AtomicBool::new(false);
static ALLOCS: AtomicUsize = AtomicUsize::new(0);
static FREES: AtomicUsize = AtomicUsize::new(0);

/// The system allocator, counting while armed.
pub struct Counting;

unsafe impl GlobalAlloc for Counting {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if ARMED.load(Ordering::Relaxed) {
            ALLOCS.fetch_add(1, Ordering::Relaxed);
        }
        System.alloc(layout)
    }
    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        if ARMED.load(Ordering::Relaxed) {
            FREES.fetch_add(1, Ordering::Relaxed);
        }
        System.dealloc(ptr, layout)
    }
    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        if ARMED.load(Ordering::Relaxed) {
            ALLOCS.fetch_add(1, Ordering::Relaxed);
        }
        System.realloc(ptr, layout, new_size)
    }
}

/// Start counting.
pub fn arm() {
    ARMED.store(true, Ordering::SeqCst);
}

/// Stop counting.
pub fn disarm() {
    ARMED.store(false, Ordering::SeqCst);
}

/// Allocations and reallocations while armed.
pub fn allocs() -> usize {
    ALLOCS.load(Ordering::SeqCst)
}

/// Frees while armed.
pub fn frees() -> usize {
    FREES.load(Ordering::SeqCst)
}
