// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The code behind "Subtractive Synthesis in Practice" (../paper.md).

One module per section of the paper's implementation chapter: oscillators,
filters, the waveshaper, the oversampler, and the voice around them. Every
listing the paper prints is a region of this crate between `// ANCHOR: name`
and `// ANCHOR_END: name`, and tests/snippets.rs fails when the two differ.

Audio is f32, as in a plugin; [`measure`] works in f64, so the floor it
measures is the algorithm's and not the measurement's. Nothing that renders
allocates (tests/no_alloc.rs); constructors may.
*/
#![forbid(unsafe_code)]

pub mod filter;
pub mod measure;
pub mod osc;
pub mod oversample;
pub mod shaper;
pub mod voice;

/// The sample rate the paper's measurements use.
pub const FS: f32 = 48_000.0;
