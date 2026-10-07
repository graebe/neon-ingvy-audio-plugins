// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! trance_gate_core.h and tg_shell.h, written from this crate's source. See
//! engines/shared/cbindgen/capi_header.rs.

#[path = "../../../shared/cbindgen/capi_header.rs"]
mod capi_header;

use capi_header::Header;

fn main() {
    capi_header::generate(&[
        Header {
            name: "trance_gate_core.h",
            sources: &["src/engine.rs", "../../../shared/crates/ni-dsp/src/ffi.rs"],
        },
        Header {
            name: "tg_shell.h",
            sources: &["src/shell.rs"],
        },
    ]);
}
