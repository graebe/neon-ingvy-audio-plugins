// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! shell_handoff.h, written from this crate's source. See
//! engines/shared/cbindgen/capi_header.rs.

#[path = "../../../shared/cbindgen/capi_header.rs"]
mod capi_header;

use capi_header::Header;

fn main() {
    capi_header::generate(&[Header {
        name: "shell_handoff.h",
        sources: &["src/lib.rs"],
    }]);
}
