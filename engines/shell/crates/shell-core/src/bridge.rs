// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
An engine owned by the audio thread, and everything the other threads need
from it.

THE RULE. Only the audio thread touches the engine. Every other thread -- the
editor's messages, the host's state calls, the idle timer -- reaches it through
two doors:

- **in**, as a command: [`Bridge::post`] queues bytes that the audio thread
  applies at the top of its next block ([`Bridge::begin`]);
- **out**, as a snapshot: the audio thread formats what readers need into a
  preallocated [`Model::Frame`] and publishes it ([`Bridge::end`]); readers
  take the latest one ([`Bridge::read`]).

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

The same outbox makes a full queue a non-event: commands that did not fit wait
on the main side and are sent, in order, on the next call.
*/

use crate::queue::Queue;
use crate::snapshot::TripleBuffer;
use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicU32, Ordering};
use std::collections::VecDeque;
use std::sync::Mutex;

/// What a product supplies. The engine type itself, usually wrapped.
pub trait Model: Send {
    /// Everything a non-audio thread may read, preformatted. Must own its
    /// storage outright: it is written in place on the audio thread.
    type Frame: Send;

    /// A frame with all its storage allocated. Construction only.
    fn new_frame(&self) -> Self::Frame;

    /// Apply one command. Runs on the audio thread: must not allocate, lock
    /// or block.
    fn apply(&mut self, cmd: &[u8]);

    /// Rewrite every field of `frame` from the engine. Runs on the audio
    /// thread: must not allocate, lock or block.
    fn publish(&self, frame: &mut Self::Frame);

    /// Make this (view) model equal to the engine that published `frame`, as
    /// far as the commands it will replay can observe. Main side only.
    fn restore(&mut self, frame: &Self::Frame);
}

/// A published frame, with the last command it includes.
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

const SEQ: usize = 8;

struct Audio<M> {
    engine: M,
    scratch: Box<[u8]>,
    applied: u64,
    since: u32,
    dirty: bool,
}

struct Main<M> {
    view: Option<M>,
    /// Encoded records (seq + command), oldest first. The first `sent` of
    /// them are in the queue already.
    outbox: VecDeque<Vec<u8>>,
    sent: usize,
    next_seq: u64,
    /// (frame applied, newest seq) the view was built for.
    view_key: Option<(u64, u64)>,
}

pub struct Bridge<M: Model> {
    audio: UnsafeCell<Audio<M>>,
    queue: Queue,
    frames: TripleBuffer<Frame<M::Frame>>,
    main: Mutex<Main<M>>,
    publish_every: AtomicU32,
}

// SAFETY: `audio` is touched only through `begin`/`end`, whose contract is one
// audio thread at a time; the queue and the triple buffer each have exactly one
// producer and one consumer, and every main-side access to their other end is
// under the `main` mutex.
unsafe impl<M: Model> Sync for Bridge<M> {}
unsafe impl<M: Model> Send for Bridge<M> {}

fn seq_of(rec: &[u8]) -> u64 {
    let mut b = [0u8; SEQ];
    b.copy_from_slice(&rec[..SEQ]);
    u64::from_le_bytes(b)
}

impl<M: Model> Bridge<M> {
    /// `view` is the second model a reader is answered from while commands
    /// are outstanding; `None` for a product whose readers only ever want the
    /// frame. `max_command` bounds one command; `queue_bytes` the queue.
    /// `publish_every` is in frames. Allocates; construction only.
    pub fn new(
        engine: M,
        view: Option<M>,
        queue_bytes: usize,
        max_command: usize,
        publish_every: u32,
    ) -> Bridge<M> {
        let frames = TripleBuffer::new(|| Frame { applied: 0, data: engine.new_frame() });
        /* The first frame, published before any other thread exists, so a
         * reader never sees an unwritten one. */
        unsafe {
            engine.publish(&mut frames.back().data);
            frames.publish();
        }
        Bridge {
            audio: UnsafeCell::new(Audio {
                engine,
                scratch: vec![0u8; SEQ + max_command].into_boxed_slice(),
                applied: 0,
                since: 0,
                dirty: false,
            }),
            queue: Queue::new(queue_bytes, SEQ + max_command),
            frames,
            main: Mutex::new(Main {
                view,
                outbox: VecDeque::new(),
                sent: 0,
                next_seq: 1,
                view_key: None,
            }),
            publish_every: AtomicU32::new(publish_every.max(1)),
        }
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
        while let Some(n) = self.queue.pop(&mut a.scratch) {
            if n < SEQ {
                continue;
            }
            a.engine.apply(&a.scratch[SEQ..n]);
            a.applied = seq_of(&a.scratch[..n]);
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
    /// the block was touched, or the cadence is due. Allocation-free, lock-free, wait-free.
    ///
    /// # Safety
    /// As [`Bridge::begin`].
    pub unsafe fn end(&self, frames: u32) {
        let a = &mut *self.audio.get();
        a.since = a.since.saturating_add(frames);
        if !a.dirty && a.since < self.publish_every.load(Ordering::Relaxed) {
            return;
        }
        let slot = self.frames.back();
        a.engine.publish(&mut slot.data);
        slot.applied = a.applied;
        self.frames.publish();
        a.since = 0;
        a.dirty = false;
    }

    /* ------------------------------------------------------------- main */

    /// Queue one command for the engine. False only for a command longer
    /// than `max_command`, which could never be delivered. Any non-audio
    /// thread; never blocks the audio thread.
    pub fn post(&self, cmd: &[u8]) -> bool {
        if SEQ + cmd.len() > self.queue.max_record() {
            return false;
        }
        let mut m = self.main.lock().unwrap_or_else(|e| e.into_inner());
        let seq = m.next_seq;
        m.next_seq += 1;
        let mut rec = Vec::with_capacity(SEQ + cmd.len());
        rec.extend_from_slice(&seq.to_le_bytes());
        rec.extend_from_slice(cmd);
        m.outbox.push_back(rec);
        self.flush(&mut m);
        true
    }

    fn flush(&self, m: &mut Main<M>) {
        while m.sent < m.outbox.len() {
            // SAFETY: the producer end is only used under the `main` mutex.
            if !unsafe { self.queue.push(&[&m.outbox[m.sent]]) } {
                break;
            }
            m.sent += 1;
        }
    }

    /* Take the latest frame, retire every command it includes, and send any
     * that are still waiting for room. */
    fn sync<'a>(&'a self, m: &mut Main<M>) -> &'a Frame<M::Frame> {
        // SAFETY: the reader end is only used under the `main` mutex.
        let (frame, fresh) = unsafe { self.frames.latest() };
        if fresh {
            m.view_key = None;
        }
        while let Some(rec) = m.outbox.front() {
            if seq_of(rec) > frame.applied {
                break;
            }
            m.outbox.pop_front();
            m.sent = m.sent.saturating_sub(1);
        }
        self.flush(m);
        frame
    }

    /// Answer a reader from the latest frame -- and, while commands are
    /// outstanding, from the view that includes them. Any non-audio thread.
    pub fn read<R>(&self, f: impl FnOnce(Read<'_, M>) -> R) -> R {
        let mut guard = self.main.lock().unwrap_or_else(|e| e.into_inner());
        let m = &mut *guard;
        let frame = self.sync(m);

        if m.outbox.is_empty() || m.view.is_none() {
            m.view_key = None;
            return f(Read { frame: &frame.data, pending: None });
        }

        let newest = m.outbox.back().map(|r| seq_of(r)).unwrap_or(0);
        let key = (frame.applied, newest);
        let view = m.view.as_mut().expect("checked above");
        if m.view_key != Some(key) {
            view.restore(&frame.data);
            for rec in &m.outbox {
                view.apply(&rec[SEQ..]);
            }
            m.view_key = Some(key);
        }
        f(Read { frame: &frame.data, pending: Some(view) })
    }

    /// Commands posted but not yet reported applied by a published frame.
    pub fn outstanding(&self) -> usize {
        let mut m = self.main.lock().unwrap_or_else(|e| e.into_inner());
        self.sync(&mut m);
        m.outbox.len()
    }
}
