// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * spectro-capi -- the C ABI. This is the whole surface the plugin shell sees,
 * and the surface a Schwung module on the Move would see, in two headers that
 * build.rs writes from this crate:
 *
 *   spectro_core.h   `spectro_*`, the analyzer           src/analyzer.rs
 *   spectro_recv.h   `srecv_*`, the listen-in receiver   src/recv.rs
 *
 * The shape is the Trance Gate engine's: an opaque handle, a couple of
 * functions, no callbacks and no C structs shared across the boundary. A C ABI
 * that passes only scalars and byte buffers is one that cannot get out of step
 * with its header.
 *
 * THE THREAD RULES ARE PART OF THE ABI, not a note in a README:
 *
 *   spectro_new / free / configure   one thread, with no other call in flight
 *   spectro_push_f32                 the audio thread, and only it
 *   spectro_take_columns             the message thread, and only it
 *
 * push and take_columns may run at the same time -- that is what the ring
 * inside is for -- but two threads pushing, or a configure racing either, is
 * undefined. The shell honours this by construction: OnReset configures while
 * the host guarantees audio is stopped, ProcessBlock pushes, OnIdle drains.
 *
 * PANICS ABORT (see the workspace Cargo.toml). Unwinding out of an `extern "C"`
 * function into C++ is undefined behaviour, and a crash the OS reports beats a
 * host whose stack has been quietly corrupted.
 */

/*
 * THE GROUND'S C ABI RIDES IN THIS ARCHIVE, and this line is what puts it there.
 *
 * `ground-capi` is an rlib holding the gnd_* entry points the editor's animated
 * background needs. It is not a static library of its own on purpose: two Rust
 * staticlibs in one binary duplicate the Rust runtime and fail to link, so this
 * repository keeps one archive per plugin (spectro-capi's Cargo.toml states the
 * rule). Naming the crate here is what makes rustc link it in, so the symbols
 * are exported from this archive rather than dropped as unreachable.
 */
use ground_capi as _;
/* shell_handoff_*, for the same reason: see shell-capi. */
use shell_capi as _;

mod analyzer;
mod recv;
pub use analyzer::*;
pub use recv::*;

#[cfg(test)]
mod tests;
