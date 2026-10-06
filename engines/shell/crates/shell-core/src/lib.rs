// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
How a plugin shell's threads reach its engine.

THE RULE, for every plugin in this repository: the engine belongs to the audio
thread. Nothing else mutates it and nothing else reads it. Other threads
mutate it by queueing a command the audio thread applies at the top of a
block, and read it from a snapshot the audio thread publishes. There is no
lock the audio thread can wait on anywhere in that path.

- [`Bridge`]: one engine, the way in and the way out -- a product's typed
  commands through rtrb's wait-free ring, its frames through triple_buffer's
  -- with the main-side bookkeeping that keeps a reader correct when the audio
  thread is not running at all.
- [`Handoff`]: an object built and freed on the main thread and lent to the
  audio thread, with the free deferred until the audio thread has let go --
  basedrop's reference counting, whose frees are always the collector's.
- [`Shared`] and [`Handle`]: basedrop's, re-exported for what a command
  carries. A product allocates a command's heavy payload with
  [`Bridge::handle`], and it is freed off the audio thread whoever drops it
  last.

This crate knows no product. A product supplies a [`Model`] -- its command
type, how to apply one, what to publish, how to rebuild a view -- and wraps
the result in its own C ABI.
*/

mod bridge;
mod handoff;
mod reclaim;

pub use basedrop::{Handle, Shared};
pub use bridge::{publish_every, Bridge, Frame, Model, Read, PUBLISHES_PER_SECOND};
pub use handoff::Handoff;

/// A fixed-capacity text field for a [`Model::Frame`]: allocated once, then
/// rewritten in place on the audio thread. Cloned only where a frame is: at
/// construction, into the triple buffer's three.
#[derive(Clone)]
pub struct Text {
    buf: Box<[u8]>,
    len: i32,
}

impl Text {
    pub fn new(capacity: usize) -> Text {
        Text { buf: vec![0u8; capacity].into_boxed_slice(), len: -1 }
    }

    /// Rewrite from a formatter that fills a buffer and returns the length
    /// written, or a negative number for "nothing" -- the shape every engine's
    /// `get_param` has.
    pub fn fill(&mut self, f: impl FnOnce(&mut [u8]) -> i32) {
        let n = f(&mut self.buf);
        self.len = if n < 0 { -1 } else { n.min(self.buf.len() as i32) };
    }

    /// The text, or `None` when the formatter declined.
    pub fn get(&self) -> Option<&[u8]> {
        if self.len < 0 {
            None
        } else {
            Some(&self.buf[..self.len as usize])
        }
    }

    /// The text as `&str`, or "" -- for handing back to an engine's string
    /// door.
    pub fn as_str(&self) -> &str {
        self.get().and_then(|b| core::str::from_utf8(b).ok()).unwrap_or("")
    }

    /// Copy out C-style: NUL-terminated, truncated to fit, returning the
    /// length copied or -1 when there is nothing to copy.
    pub fn copy_to(&self, out: &mut [u8]) -> i32 {
        let Some(src) = self.get() else { return -1 };
        if out.is_empty() {
            return -1;
        }
        let n = src.len().min(out.len() - 1);
        out[..n].copy_from_slice(&src[..n]);
        out[n] = 0;
        n as i32
    }
}

#[cfg(test)]
mod tests;
