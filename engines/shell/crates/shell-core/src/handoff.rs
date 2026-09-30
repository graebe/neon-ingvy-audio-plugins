/*!
An object the main thread builds and frees, lent to the audio thread.

The shell's resources that allocate or map memory -- a bus writer, an analyzer
receiver -- have to be created and destroyed on the main thread, and the audio
thread has to use them in between. A plain pointer swap is not enough: the old
object may be mid-use on the audio thread when it is replaced, and freeing it
then is a use-after-free.

So the audio thread announces what it holds (one hazard pointer; there is one
audio thread), and the main thread frees a retired object only once it can see
that the audio thread is not holding it:

  audio: p = live; hazard = p; if live != p, retry   (then use p, then hazard = null)
  main:  old = swap(live, next); retire old; later free old unless hazard == old

Every access is SeqCst, which is what makes the two re-checks agree: if the
main thread's hazard load does not see `old`, the audio thread's own re-check of
`live` comes after the swap and refuses `old`. Nothing here allocates on the
audio side, and nothing waits.
*/

use core::ptr;
use core::sync::atomic::{AtomicPtr, Ordering::SeqCst};
use std::sync::Mutex;

struct Retired<T>(*mut T);
// SAFETY: a retired pointer is only ever dereferenced by the caller's free
// function, on the thread that calls `collect` -- the main thread.
unsafe impl<T> Send for Retired<T> {}

pub struct Handoff<T> {
    live: AtomicPtr<T>,
    hazard: AtomicPtr<T>,
    retired: Mutex<Vec<Retired<T>>>,
}

impl<T> Default for Handoff<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> Handoff<T> {
    pub fn new() -> Handoff<T> {
        Handoff {
            live: AtomicPtr::new(ptr::null_mut()),
            hazard: AtomicPtr::new(ptr::null_mut()),
            retired: Mutex::new(Vec::new()),
        }
    }

    /// The audio thread's hold on the live object, or null. Must be paired
    /// with [`Handoff::release`] before the block ends. Wait-free: a swap that
    /// races the acquire twice in a row yields null for this block.
    pub fn acquire(&self) -> *mut T {
        for _ in 0..2 {
            let p = self.live.load(SeqCst);
            self.hazard.store(p, SeqCst);
            if self.live.load(SeqCst) == p {
                return p;
            }
        }
        self.hazard.store(ptr::null_mut(), SeqCst);
        ptr::null_mut()
    }

    /// The audio thread is done with what it acquired.
    pub fn release(&self) {
        self.hazard.store(ptr::null_mut(), SeqCst);
    }

    /// The object the main thread last installed. Main thread only.
    pub fn current(&self) -> *mut T {
        self.live.load(SeqCst)
    }

    /// Install `next` and retire the previous object, which [`Handoff::collect`]
    /// frees once the audio thread cannot be holding it. Main thread only.
    pub fn set(&self, next: *mut T) {
        let old = self.live.swap(next, SeqCst);
        if !old.is_null() && old != next {
            self.retired.lock().unwrap_or_else(|e| e.into_inner()).push(Retired(old));
        }
    }

    /// Free every retired object the audio thread is not holding, through
    /// `free`. Returns how many are still waiting. Main thread only.
    pub fn collect(&self, mut free: impl FnMut(*mut T)) -> usize {
        let mut retired = self.retired.lock().unwrap_or_else(|e| e.into_inner());
        let held = self.hazard.load(SeqCst);
        retired.retain(|r| {
            if r.0 == held {
                true
            } else {
                free(r.0);
                false
            }
        });
        retired.len()
    }

    /// Free everything, the live object included. Only once no audio thread
    /// can call [`Handoff::acquire`] again -- in the owner's destructor.
    pub fn clear(&mut self, mut free: impl FnMut(*mut T)) {
        let live = self.live.swap(ptr::null_mut(), SeqCst);
        if !live.is_null() {
            free(live);
        }
        let retired = self.retired.get_mut().unwrap_or_else(|e| e.into_inner());
        for r in retired.drain(..) {
            free(r.0);
        }
    }
}
