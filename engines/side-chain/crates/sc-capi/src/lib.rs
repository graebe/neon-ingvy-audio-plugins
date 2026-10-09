// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
NI Side-Chain's C ABI, in two headers that build.rs writes from this crate.

  sc_core.h    `sc_core_*`, the engine itself              src/engine.rs
  sc_shell.h   `sc_shell_*`, the engine as a plugin's      src/shell.rs
               threads hold it
  sc_kick.h    `sc_kick_*`, a Listen-In's kick filed       src/kick.rs
               under the ducker's own sweep

EVERY ENTRY POINT RUNS ON AN AUDIO CALLBACK. No allocation, no locking, no
panicking -- the workspace sets `panic = "abort"` because unwinding out of
`extern "C"` into a C++ host is undefined behaviour.
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
 *
 * It and the shell are the `shell` feature, the plugin's half of this crate:
 * the Move module has no editor and one thread, and builds without either
 * (see Cargo.toml).
 */
#[cfg(feature = "shell")]
use ground_capi as _;

/* The engine's door, `sc_core_*`; see engine.rs. */
mod engine;
pub use engine::*;
/* The plugin shell's door to the engine; see shell.rs. */
#[cfg(feature = "shell")]
mod shell;
#[cfg(feature = "shell")]
pub use shell::ScShell;
/* The kick behind the duck, from a Listen-In bus; see kick.rs. */
#[cfg(feature = "shell")]
pub mod kick;

#[cfg(test)]
mod tests;
