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
they are what says the port is correct. Every caller is in this repository;
the archived Max for Live external was the last one outside it, and the
promise to keep its headers source-compatible is retired
(docs/tech/compatibility.md, promise 9).

# Safety

Every function here is `unsafe` in the C sense and safe in practice under the
contract the C had: a `tg_core_t*` is a pointer returned by
[`tg_core_create`] and not yet destroyed, and buffers are valid for the frame
counts given. Null is checked because the C checked it, and callers rely on
that; anything else is the caller's bargain, as it was before.
*/

/* The engine's door, `tg_core_*`; see engine.rs. */
mod engine;
pub use engine::*;
/* The plugin shell's door to the engine; see shell.rs. */
#[cfg(feature = "shell")]
pub mod shell;
#[cfg(feature = "shell")]
pub use shell::TgShell;
/* The pattern plot's curve, rendered through a scratch engine; see gate.rs. */
mod envelope;
mod gate;

#[cfg(test)]
mod tests;
