// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The `bc_*` C ABI: NI Bit-Crusher's engine as its shells hold it.

WHAT THIS CRATE IS ALLOWED TO DO: null-check, bound-check, copy into C memory,
and hand the call to `bc-core`. What a step is is decided there.

TWO HEADERS. `bc_core.h` is `engine.rs`: the parameter table, the steps as a
curve, and one engine instance -- everything the C tests and the Move module
reach. `bc_shell.h` is `shell.rs`, the plugin's: the engine behind shell-core's
bridge, with the scope the editor draws. The Move build leaves the shell out
(the `shell` feature).

THE PARAMETER TABLE IS THE ENGINE'S. A shell declares its parameters from
`bc_param_*` -- count, key, name, kind, range, travel, unit -- prints a value
with `bc_param_text` and reads typing with `bc_param_parse`, so the plugin, the
Move and the manual print the same words.

THE SCOPE. An instance keeps the last `BC_SCOPE_RING` samples of its left
channel, dry and wet, as it processes them; the shell publishes a window of
them for the editor (shell.rs). Two stores a sample, whether or not a window
is open: not worth a switch.

THREADS. The table, `bc_transfer_span` and `bc_transfer_render` are pure:
any thread. An instance belongs to one thread at a time -- on the plugin, the audio thread's between
`bc_shell_begin` and `bc_shell_end`. Nothing an audio thread calls allocates,
and the workspace aborts on panic, because unwinding out of `extern "C"` is
undefined.
*/

/* The ground's C ABI (gnd_*), which every plugin archive carries: without a
 * reference from here the linker would drop its #[no_mangle] symbols. */
#[cfg(feature = "shell")]
use ground_capi as _;

mod engine;
pub use engine::*;

#[cfg(feature = "shell")]
pub mod shell;

#[cfg(test)]
mod tests;
