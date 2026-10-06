// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
An engine owned by the audio thread, and everything the other threads need
from it.

THE RULE. Only the audio thread touches the engine. Every other thread -- the
editor's messages, the host's state calls, the idle timer -- reaches it through
two doors:

- **in**, as a command: [`Bridge::post`] queues one of the product's own typed
  commands, read and checked on the posting side, and the audio thread applies
  it at the top of its next block ([`Bridge::begin`]) -- no bytes to decode, no
  text to parse;
- **out**, as a snapshot: the audio thread formats what readers need into a
  preallocated [`Model::Frame`] and publishes it ([`Bridge::end`]); readers
  take the latest one ([`Bridge::read`]).

The way in is rtrb's wait-free ring. The way out is triple_buffer's: three
frames, each owned by one side at a time and handed across by one atomic swap,
so the writer never waits for a reader and a reader never sees a frame half
written -- which a seqlock cannot promise in Rust without making every byte of
the payload an atomic. A reader slower than the writer skips frames, which is
what a readout wants: the latest, whole.

WHAT A COMMAND MAY HOLD. Posting clones it -- one copy rides the ring, one
waits here until a frame confirms it -- and the audio thread drops its copy. So
a command is small and plain, and anything heavy it carries rides in a
basedrop [`Shared`](basedrop::Shared) allocated with [`Bridge::handle`]: a
clone is a counter increment, and the last drop, on whichever thread, queues
the payload for this bridge's collector instead of freeing it. The audio
thread allocates nothing and frees nothing.

THE AUDIO THREAD MAY NOT BE RUNNING. A host with its engine off still opens
projects, still lets the user edit, still saves. A command that is queued and
never drained must still be visible to a reader, or a save straight after an
edit would write the patch from before it. So the main side keeps every
command until a published frame says it has been applied, and a reader that
arrives while any are outstanding gets a VIEW: a second model, restored from
the latest frame and replayed forward through the outstanding commands. That
is exactly the state the engine will reach when it next drains, computed on
the thread that can afford to compute it. Once the engine catches up, the view
is discarded and the frame answers again.

The same outbox makes a full ring a non-event: commands that did not fit wait
on the main side and are sent, in order, on the next call.
*/

use crate::reclaim::Reclaim;
use basedrop::Handle;
use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicU32, Ordering};
use rtrb::{Consumer, Producer, RingBuffer};
use std::collections::VecDeque;
use std::sync::{Mutex, MutexGuard, PoisonError};
use triple_buffer::{Input, Output, TripleBuffer};

/// What a product supplies. The engine type itself, usually wrapped.
pub trait Model: Send {
    /// One edit, typed and already read: what [`Bridge::post`] queues and
    /// [`Model::apply`] performs. Cloned on every post and dropped on the
    /// audio thread, so anything heavy rides in a `Shared` from
    /// [`Bridge::handle`] -- see the module comment.
    type Command: Clone + Send + 'static;

    /// Everything a non-audio thread may read, preformatted. Must own its
    /// storage outright: it is rewritten in place on the audio thread. Cloned
    /// at construction into the triple buffer's three frames.
    type Frame: Clone + Send;

    /// A frame with all its storage allocated. Construction only.
    fn new_frame(&self) -> Self::Frame;

    /// Apply one command. Runs on the audio thread: must not allocate, lock
    /// or block.
    fn apply(&mut self, cmd: &Self::Command);

    /// Rewrite every field of `frame` from the engine. Runs on the audio
    /// thread: must not allocate, lock or block.
    fn publish(&self, frame: &mut Self::Frame);

    /// Make this (view) model equal to the engine that published `frame`, as
    /// far as the commands it will replay can observe. Main side only.
    fn restore(&mut self, frame: &Self::Frame);
}

/// A published frame, with the last command it includes.
#[derive(Clone)]
pub struct Frame<F> {
    pub applied: u64,
    pub data: F,
}

/// What a reader is given: always the latest frame, and -- while commands are
/// outstanding -- the view that includes them. Which to answer from is the
/// product's decision, per field: a readout the commands cannot change is
/// better served from the frame, where the engine's live values are.
pub struct Read<'a, M: Model> {
    pub frame: &'a M::Frame,
    pub pending: Option<&'a M>,
}

/// How often a bridge republishes when nothing was posted: faster than any
/// idle timer reads it, so a readout that moves on its own -- a playhead -- is
/// never more than a tick behind, and an editor interpolates between frames.
pub const PUBLISHES_PER_SECOND: f64 = 100.0;

/// [`PUBLISHES_PER_SECOND`] as a period in frames at `sample_rate`, which is
/// what [`Bridge::new`] and [`Bridge::set_publish_every`] take. A rate that is
/// not positive is a host that has not said yet, and 44.1 kHz stands in.
pub fn publish_every(sample_rate: f64) -> u32 {
    let sr = if sample_rate > 0.0 { sample_rate } else { 44100.0 };
    (sr / PUBLISHES_PER_SECOND).max(1.0) as u32
}

/// A command, and its place in the order commands were posted.
#[derive(Clone)]
struct Posted<C> {
    seq: u64,
    cmd: C,
}

struct Audio<M: Model> {
    engine: M,
    commands: Consumer<Posted<M::Command>>,
    frames: Input<Frame<M::Frame>>,
    applied: u64,
    since: u32,
    dirty: bool,
}

struct Main<M: Model> {
    commands: Producer<Posted<M::Command>>,
    frames: Output<Frame<M::Frame>>,
    view: Option<M>,
    /// Posted and not yet confirmed by a frame, oldest first. The first `sent`
    /// of them are in the ring already.
    outbox: VecDeque<Posted<M::Command>>,
    sent: usize,
    next_seq: u64,
    /// (frame applied, newest seq) the view was built for.
    view_key: Option<(u64, u64)>,
}

pub struct Bridge<M: Model> {
    audio: UnsafeCell<Audio<M>>,
    main: Mutex<Main<M>>,
    publish_every: AtomicU32,
    /// Declared last, so dropped last: the ring, the outbox, the engine and
    /// the view have let go of every payload by the time it collects.
    reclaim: Reclaim,
}

// SAFETY: `audio` is touched only through `begin`/`touch`/`end`, whose
// contract is one audio thread at a time; the ring and the triple buffer each
// have one end on that thread and the other under the `main` mutex.
unsafe impl<M: Model> Sync for Bridge<M> {}

impl<M: Model> Bridge<M> {
    /// `view` is the second model a reader is answered from while commands
    /// are outstanding; `None` for a product whose readers only ever want the
    /// frame. `capacity` is how many commands the ring holds between two
    /// blocks -- more wait on the main side, never lost. `publish_every` is in
    /// frames. Allocates; construction only.
    pub fn new(engine: M, view: Option<M>, capacity: usize, publish_every: u32) -> Bridge<M> {
        /* The first frame, published before any other thread exists, so a
         * reader never sees an unwritten one: all three of the triple
         * buffer's start as copies of it. */
        let mut first = Frame { applied: 0, data: engine.new_frame() };
        engine.publish(&mut first.data);
        let (input, output) = TripleBuffer::new(&first).split();
        let (tx, rx) = RingBuffer::new(capacity.max(1));
        Bridge {
            audio: UnsafeCell::new(Audio {
                engine,
                commands: rx,
                frames: input,
                applied: 0,
                since: 0,
                dirty: false,
            }),
            main: Mutex::new(Main {
                commands: tx,
                frames: output,
                view,
                outbox: VecDeque::new(),
                sent: 0,
                next_seq: 1,
                view_key: None,
            }),
            publish_every: AtomicU32::new(publish_every.max(1)),
            reclaim: Reclaim::new(),
        }
    }

    /// What a command's heavy payload is allocated with: a
    /// `basedrop::Shared::new(bridge.handle(), ..)` is freed by this bridge's
    /// collector, off the audio thread, whoever drops it last.
    pub fn handle(&self) -> &Handle {
        self.reclaim.handle()
    }

    /// How often, in frames, the audio thread republishes without having
    /// applied a command -- the cadence of anything that moves on its own,
    /// such as a playhead. Any thread.
    pub fn set_publish_every(&self, frames: u32) {
        self.publish_every.store(frames.max(1), Ordering::Relaxed);
    }

    /* ------------------------------------------------------------ audio */

    /// Top of a block: apply every queued command and hand over the engine.
    /// Allocation-free, lock-free, wait-free.
    ///
    /// # Safety
    /// The audio thread only, one at a time, and the reference must not be
    /// used after [`Bridge::end`] or past the block.
    #[allow(clippy::mut_from_ref)]
    pub unsafe fn begin(&self) -> &mut M {
        let a = &mut *self.audio.get();
        /* Each command is dropped as soon as it is applied. Its payload's
         * count only goes down here: the outbox still holds a copy, and a
         * last copy is queued for the collector, never freed. */
        while let Ok(posted) = a.commands.pop() {
            a.engine.apply(&posted.cmd);
            a.applied = posted.seq;
            a.dirty = true;
        }
        &mut a.engine
    }

    /// Publish at the end of this block whatever the cadence says: the engine
    /// changed in a way a reader must not see late. Allocation-free.
    ///
    /// # Safety
    /// As [`Bridge::begin`].
    pub unsafe fn touch(&self) {
        (*self.audio.get()).dirty = true;
    }

    /// End of a block of `frames`: publish a frame if a command was applied,
    /// the block was touched, or the cadence is due. Allocation-free,
    /// lock-free, wait-free.
    ///
    /// # Safety
    /// As [`Bridge::begin`].
    pub unsafe fn end(&self, frames: u32) {
        let a = &mut *self.audio.get();
        a.since = a.since.saturating_add(frames);
        if !a.dirty && a.since < self.publish_every.load(Ordering::Relaxed) {
            return;
        }
        /* The frame being filled is whatever was published two swaps ago, so
         * every field of it is rewritten. */
        let slot = a.frames.input_buffer_mut();
        a.engine.publish(&mut slot.data);
        slot.applied = a.applied;
        a.frames.publish();
        a.since = 0;
        a.dirty = false;
    }

    /* ------------------------------------------------------------- main */

    fn lock(&self) -> MutexGuard<'_, Main<M>> {
        self.main.lock().unwrap_or_else(PoisonError::into_inner)
    }

    /// Queue one command for the engine. Any non-audio thread; never blocks
    /// the audio thread. A full ring is not a refusal: the command waits on
    /// this side and is sent in order.
    pub fn post(&self, cmd: M::Command) {
        let mut m = self.lock();
        /* Retired first, so the outbox is never longer than what the engine
         * has yet to confirm, whether or not anybody reads. */
        m.sync();
        self.reclaim.collect();
        let seq = m.next_seq;
        m.next_seq += 1;
        m.outbox.push_back(Posted { seq, cmd });
        m.flush();
    }

    /// Answer a reader from the latest frame -- and, while commands are
    /// outstanding, from the view that includes them. Any non-audio thread.
    pub fn read<R>(&self, f: impl FnOnce(Read<'_, M>) -> R) -> R {
        let mut guard = self.lock();
        guard.sync();
        self.reclaim.collect();
        let Main { frames, view, outbox, view_key, .. } = &mut *guard;
        let frame = frames.output_buffer();

        let Some(view) = view.as_mut().filter(|_| !outbox.is_empty()) else {
            *view_key = None;
            return f(Read { frame: &frame.data, pending: None });
        };
        let newest = outbox.back().map_or(0, |p| p.seq);
        let key = (frame.applied, newest);
        if *view_key != Some(key) {
            view.restore(&frame.data);
            for posted in outbox.iter() {
                view.apply(&posted.cmd);
            }
            *view_key = Some(key);
        }
        f(Read { frame: &frame.data, pending: Some(view) })
    }

    /// Commands posted but not yet reported applied by a published frame.
    pub fn outstanding(&self) -> usize {
        let mut m = self.lock();
        m.sync();
        self.reclaim.collect();
        m.outbox.len()
    }
}

impl<M: Model> Main<M> {
    /* Send what fits, in order. */
    fn flush(&mut self) {
        while self.sent < self.outbox.len() && !self.commands.is_full() {
            if self.commands.push(self.outbox[self.sent].clone()).is_err() {
                break;
            }
            self.sent += 1;
        }
    }

    /* Take the latest frame, retire every command it includes, and send any
     * that are still waiting for room. A retired command is dropped here: if
     * the audio thread has let go of its copy, this was the last, and its
     * payload is queued for the collector. */
    fn sync(&mut self) {
        if self.frames.update() {
            self.view_key = None;
        }
        let applied = self.frames.output_buffer().applied;
        while self.outbox.front().is_some_and(|p| p.seq <= applied) {
            self.outbox.pop_front();
            self.sent = self.sent.saturating_sub(1);
        }
        self.flush();
    }
}
