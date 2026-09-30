/*
 * The receiver's own thread: the pump and every transform, off the host's.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * A receiver with four sources at 96 kHz runs four 16384-point transforms
 * about 47 times a second each. On the message thread that is work the host's
 * UI waits behind; here it is a thread of its own, below the UI's priority,
 * that wakes every TICK, pumps, and parks again. The message thread only
 * drains the finished columns, which were always behind lock-free rings.
 *
 * OWNERSHIP. The worker owns the `Engine` -- the own channel's ring consumer,
 * every analyzer's producing half, every bus reader -- from the moment `start`
 * hands it over until `stop` joins the thread and takes it back, so the
 * readers and buffers are freed on the thread that built them. Nothing is
 * shared with the worker but three atomics in `Shared`.
 *
 * A CHANGE OF SOURCES is a `Plan` posted through `plan` and answered through
 * `done`: the main thread opens the readers and sizes the vectors, the worker
 * moves buses between them at the top of its next tick, and the main thread
 * waits for that -- at most a tick and one pump -- and drops what was retired.
 * Only one plan is ever in flight, because the only thread that posts one waits
 * for it.
 *
 * NO LOCK anywhere, and the audio thread never sees any of this: it still only
 * pushes into the own channel's ring.
 */
use core::ptr;
use core::sync::atomic::{AtomicBool, AtomicPtr, Ordering};
use std::sync::Arc;
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use crate::engine::{Engine, Plan, PUMP_FRAMES};

/// How often the worker looks for new audio. A hop is ~21 ms at every rate
/// (see spectro_core::TARGET_COLUMNS_PER_S), so a column waits at most a
/// quarter of one before it is computed.
pub const TICK: Duration = Duration::from_millis(5);

struct Shared {
    stop: AtomicBool,
    /// The engine, on its way to the worker once the thread exists.
    engine: AtomicPtr<Engine>,
    plan: AtomicPtr<Plan>,
    done: AtomicPtr<Plan>,
}

/* The engine crosses to the worker as a raw pointer, which the compiler cannot
 * check -- so it is checked here instead. */
const _: fn() = || {
    fn send<T: Send>() {}
    send::<Engine>();
    send::<Plan>();
};

pub struct Worker {
    shared: Arc<Shared>,
    /// `None` once stopped.
    thread: Option<JoinHandle<Option<Box<Engine>>>>,
}

impl Worker {
    /// Start a thread and hand it `engine`. If the thread cannot be created
    /// the engine comes back, so the caller can go on pumping it itself.
    pub fn start(engine: Box<Engine>) -> Result<Self, Box<Engine>> {
        let shared = Arc::new(Shared {
            stop: AtomicBool::new(false),
            engine: AtomicPtr::new(ptr::null_mut()),
            plan: AtomicPtr::new(ptr::null_mut()),
            done: AtomicPtr::new(ptr::null_mut()),
        });
        let theirs = shared.clone();
        let spawned = thread::Builder::new()
            .name("spectro-analysis".into())
            .spawn(move || run(&theirs));
        match spawned {
            Ok(thread) => {
                shared.engine.store(Box::into_raw(engine), Ordering::Release);
                thread.thread().unpark();
                Ok(Self { shared, thread: Some(thread) })
            }
            Err(_) => Err(engine),
        }
    }

    /// Have the worker apply `plan`, and wait until it has. **Main thread.**
    pub fn apply(&self, mut plan: Box<Plan>) -> Box<Plan> {
        plan.waiter = Some(thread::current());
        self.shared.plan.store(Box::into_raw(plan), Ordering::Release);
        if let Some(t) = &self.thread {
            t.thread().unpark();
        }
        loop {
            let done = self.shared.done.swap(ptr::null_mut(), Ordering::Acquire);
            if !done.is_null() {
                /* Ours: posted by us above, handed back by the worker. */
                let mut plan = unsafe { Box::from_raw(done) };
                plan.waiter = None;
                return plan;
            }
            thread::park_timeout(TICK);
        }
    }

    /// Stop the thread and take the engine back: the thread has exited when
    /// this returns. **Main thread.**
    pub fn stop(&mut self) -> Option<Box<Engine>> {
        let thread = self.thread.take()?;
        self.shared.stop.store(true, Ordering::Release);
        thread.thread().unpark();
        thread.join().ok().flatten()
    }
}

impl Drop for Worker {
    fn drop(&mut self) {
        /* The engine -- readers, analyzers, buffers -- is freed here, on the
         * thread dropping the receiver, not on the worker. */
        drop(self.stop());
    }
}

fn run(shared: &Shared) -> Option<Box<Engine>> {
    lower_priority();

    /* `start` stores the engine before `stop` can be called, so looking for it
     * before looking at `stop` never leaves it behind. */
    let mut engine = loop {
        let e = shared.engine.swap(ptr::null_mut(), Ordering::Acquire);
        if !e.is_null() {
            /* Handed over exactly once, by `start`. */
            break unsafe { Box::from_raw(e) };
        }
        if shared.stop.load(Ordering::Acquire) {
            return None;
        }
        thread::park_timeout(TICK);
    };

    let mut last = Instant::now();
    while !shared.stop.load(Ordering::Acquire) {
        let p = shared.plan.swap(ptr::null_mut(), Ordering::Acquire);
        if !p.is_null() {
            /* Posted by `apply`, which waits and does not touch it until it
             * comes back through `done`. */
            let plan = unsafe { &mut *p };
            engine.apply(plan);
            let waiter = plan.waiter.clone();
            shared.done.store(p, Ordering::Release);
            if let Some(w) = waiter {
                w.unpark();
            }
        }

        let now = Instant::now();
        let dt = now.duration_since(last).as_micros() as u64;
        last = now;
        /* A full pump means more is waiting -- a stall being caught up -- so go
         * again at once; no time has passed for the grace clocks. */
        let mut moved = engine.pump(dt);
        while moved == PUMP_FRAMES && !shared.stop.load(Ordering::Relaxed) {
            moved = engine.pump(0);
        }

        thread::park_timeout(TICK);
    }
    Some(engine)
}

/*
 * BELOW THE UI, ABOVE BACKGROUND WORK. The host's message thread is
 * user-interactive; this is user-initiated -- a picture someone is watching,
 * which must keep up, but never ahead of the editor it is drawn in. Elsewhere
 * the thread keeps the default priority.
 */
#[cfg(target_vendor = "apple")]
fn lower_priority() {
    extern "C" {
        fn pthread_set_qos_class_self_np(qos_class: u32, relative_priority: i32) -> i32;
    }
    const QOS_CLASS_USER_INITIATED: u32 = 0x19;
    /* Failure leaves the default priority, which is a working analyzer. */
    unsafe { pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0) };
}

#[cfg(not(target_vendor = "apple"))]
fn lower_priority() {}
