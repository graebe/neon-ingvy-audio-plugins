// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! sf_core.h and sf_shell.h, written from this crate's source. See
//! engines/shared/cbindgen/capi_header.rs.

#[path = "../../../shared/cbindgen/capi_header.rs"]
mod capi_header;

use capi_header::Header;

fn main() {
    capi_header::generate(&[
        Header {
            name: "sf_core.h",
            /* NiParamInfo, which a table is declared with, is ni-params'. */
            sources: &["src/engine.rs", "../../../shared/crates/ni-params/src/lib.rs"],
        },
        Header {
            name: "sf_shell.h",
            sources: &["src/shell.rs"],
        },
    ]);
}
