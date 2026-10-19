// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The `sf_*` C ABI: the single-filter engine as its shells hold it -- one ABI
for nine products, each named by its filter's shape (SF_SHAPE_*).

WHAT THIS CRATE IS ALLOWED TO DO: null-check, bound-check, copy into C memory,
and hand the call to `sf-core`. What a filter is is decided there -- the
picture too, which designs the curve with the engine's own function.

TWO HEADERS. `sf_core.h` is `engine.rs`: each product's parameter table, what
it uses, and one engine instance -- everything the C tests and the Move
modules reach. `sf_shell.h` is `shell.rs`, the plugins': the engine behind
shell-core's bridge, and the picture the editor draws. The Move build leaves
the shell out (the `shell` feature).

THE PARAMETER TABLE IS THE ENGINE'S. A shell declares its product's
parameters from `sf_param_*(shape, …)` -- count, key, name, kind, range,
travel, unit, labels -- prints a value with `sf_param_text` and reads typing
with `sf_param_parse`, so the plugin, the Move and the manual print the same
words. `sf_field_index` says where a product declares a field, if it does.

THREADS. The table and `sf_uses`/`sf_order_played` are pure: any thread. An
instance belongs to one thread at a time -- on the plugin, the audio thread's
between `sf_shell_begin` and `sf_shell_end`. `sf_shell_read` is the editor's
thread's and the only call that does real work off the audio thread. Nothing
an audio thread calls allocates, and the workspace aborts on panic, because
unwinding out of `extern "C"` is undefined.
*/

mod engine;
pub use engine::*;

#[cfg(feature = "shell")]
pub mod shell;

#[cfg(test)]
mod tests;
