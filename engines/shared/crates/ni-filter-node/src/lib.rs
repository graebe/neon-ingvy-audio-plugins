// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
One filter as an equaliser plays it -- what NI Filter Designer runs sixteen
of, and each single-filter product one.

```text
  Target ── design ── Node ── a cascade gliding, morphing, fading
  (what is    (the one   (node.rs)
   asked)     function)
```

[`design`] is pure: a filter's settings to the cascade it runs (ni-filter),
and what an editor needs to draw it -- its handle, its edges, its tail. The
engine and the picture call the same function, so what is drawn is what
plays.

[`node`] is the filter running: its numbers glide on a control grid of
[`SUB`] samples, how it does what it does morphs, what it is fades between
two instances, and a tick's design work is budgeted. A node reads a
[`Target`], never an engine's parameter table: each engine spells its own
parameters and hands every node what they ask for.

[`picture`] holds the two conversions every editor's curve is made of, and
[`words`] the words a filter's values are printed and read in.

PRODUCT-FREE: this crate names no product, and products depend on it, never
the other way round (the workspace's rule).

THE AUDIO THREAD ALLOCATES NOTHING here: a node is plain data
(tests/no_alloc.rs).
*/

pub mod design;
pub mod node;
pub mod picture;
pub mod words;

pub use design::{Channel, Globals, Settings, Shape, Target};
pub use node::{control_in_turn, Budget, Node, SUB};

#[cfg(test)]
mod tests;
