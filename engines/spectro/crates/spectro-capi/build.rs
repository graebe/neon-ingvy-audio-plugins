// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! spectro_core.h and spectro_recv.h, written from this crate's source. See
//! engines/shared/cbindgen/capi_header.rs.

#[path = "../../../shared/cbindgen/capi_header.rs"]
mod capi_header;

use capi_header::Header;

fn main() {
    capi_header::generate(&[
        Header {
            name: "spectro_core.h",
            sources: &["src/analyzer.rs"],
        },
        Header {
            name: "spectro_recv.h",
            sources: &["src/recv.rs"],
        },
    ]);
}
