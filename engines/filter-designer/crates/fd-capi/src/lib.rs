// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The `fd_*` C ABI: NI Filter Designer's engine as its shells hold it.

WHAT THIS CRATE IS ALLOWED TO DO: null-check, bound-check, copy into C memory,
and hand the call to `fd-core`. What a filter is is decided there -- the
picture too, which designs every curve with the engine's own function.

TWO HEADERS. `fd_core.h` is `engine.rs`: the parameter table, what each type
uses, and one engine instance -- everything the C tests and the Move module
reach. `fd_shell.h` is `shell.rs`, the plugin's: the engine behind
shell-core's bridge, and the picture the editor draws. The Move build leaves
the shell out (the `shell` feature).

THE PARAMETER TABLE IS THE ENGINE'S. A shell declares its parameters from
`fd_param_*` -- count, key, name, kind, range, travel, unit, labels -- prints
a value with `fd_param_text` and reads typing with `fd_param_parse`, so the
plugin, the Move and the manual print the same words.

THREADS. The table and `fd_type_*` are pure: any thread. An instance belongs
to one thread at a time -- on the plugin, the audio thread's between
`fd_shell_begin` and `fd_shell_end`. `fd_shell_read` is the editor's thread's
and the only call that does real work off the audio thread. Nothing an audio
thread calls allocates, and the workspace aborts on panic, because unwinding
out of `extern "C"` is undefined.
*/

mod engine;
pub use engine::*;

#[cfg(feature = "shell")]
pub mod shell;

#[cfg(test)]
mod tests;
