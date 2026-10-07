// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The Trance Gate's C ABI, in two headers that build.rs writes from this crate.

  trance_gate_core.h   `tg_core_*`, the engine itself      src/engine.rs
  tg_shell.h           `tg_shell_*`, the engine as a        src/shell.rs
                       plugin's threads hold it

Byte-for-byte the surface the C engine exported, so every existing caller --
the Schwung shell, the plugin, and the engine's C tests (tests/test_core.c,
tests/test_gate.c) -- links this instead without changing a line. That is the
whole point: the tests are not rewritten for the port, they are relinked, and
they are what says the port is correct. One caller is not in this repository:
the archived Max for Live external links this crate too, so the two headers'
last hand-written versions are fixtures (tests/fixtures/tg-capi) that the
generated ones are held to, declaration for declaration (tests/capi-compat.mjs).

# Safety

Every function here is `unsafe` in the C sense and safe in practice under the
contract the C had: a `tg_core_t*` is a pointer returned by
[`tg_core_create`] and not yet destroyed, and buffers are valid for the frame
counts given. Null is checked because the C checked it, and callers rely on
that; anything else is the caller's bargain, as it was before.
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

/* The engine's door, `tg_core_*`; see engine.rs. */
mod engine;
pub use engine::*;
/* The plugin shell's door to the engine; see shell.rs. */
#[cfg(feature = "shell")]
mod shell;
#[cfg(feature = "shell")]
pub use shell::TgShell;
/* The pattern plot's curve, rendered through a scratch engine; see gate.rs. */
mod envelope;
mod gate;

#[cfg(test)]
mod tests;
