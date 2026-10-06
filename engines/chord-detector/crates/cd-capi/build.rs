// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Writes `engines/chord-detector/include/cd_capi.h` from `src/lib.rs`.
//!
//! The header is checked in, so the C++ shell and a reader on GitHub see it
//! without building anything; it is rewritten only when its text changes, so
//! an unchanged build does not touch its timestamp and rebuild the plugin.

use std::path::PathBuf;

fn main() {
    let crate_dir = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let header = crate_dir.join("../../include/cd_capi.h");
    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=cbindgen.toml");

    let config = cbindgen::Config::from_file(crate_dir.join("cbindgen.toml"))
        .expect("cbindgen.toml is readable");
    let bindings = cbindgen::Builder::new()
        .with_crate(&crate_dir)
        .with_config(config)
        .generate()
        .expect("cbindgen can read the C ABI");

    let mut text = Vec::new();
    bindings.write(&mut text);
    if std::fs::read(&header).ok().as_deref() != Some(&text[..]) {
        std::fs::write(&header, &text).expect("the include directory is writable");
    }
}
