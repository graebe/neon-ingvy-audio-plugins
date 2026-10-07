// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
A C ABI crate's headers, written from its Rust source by cbindgen.

Every `*-capi` crate's build.rs includes this file (`#[path]`) and names its
headers; there is one copy of the logic and no crate to hold it, because a
build helper that was a workspace member would be counted as shipping by
cargo-about and scripts/check-licenses.mjs. cbindgen itself is each crate's
build-dependency, which both of them leave out, as they should: it runs on the
build machine and nothing of it is linked.

WHY THE HEADERS ARE GENERATED. They were written by hand beside the Rust they
declare, and nothing but a C caller that happened to use a changed entry point
held the two together: a `*mut` that became `*const`, an argument added on one
side, a constant moved in the engine and not in the header. Generated, the
header IS the Rust, and a signature cbindgen cannot express fails the build
here instead of a host's link later.

ONE HEADER, ONE MODULE TREE. cbindgen follows `mod` declarations from the file
it is given, so a header is the `pub extern "C"` items of one source file and
the modules below it -- `src/shell.rs` is tg_shell.h, for instance -- and its
configuration is `cbindgen/<header stem>.toml` beside Cargo.toml. A type that
lives in another crate (the transport struct, which is ni-dsp's) comes in as a
further source file.

WHERE THEY GO. Always into OUT_DIR, so a plain `cargo build` or `cargo test`
proves every header still generates. And, when the build that wants them says
where with NI_CAPI_INCLUDE_DIR -- CMake does, and so does the Trance Gate's
tests/run.sh -- into that directory too, rewritten only when the text changed:
a C file that includes an unchanged header is not recompiled because the Rust
beside it was. Two builds may run this at once (one per architecture slice),
so a header is written to a temporary name and renamed into place.

THE HEADERS IN THAT DIRECTORY ARE NOT WATCHED. cargo dates a build script's
run from the moment it started, so a file the script itself writes is always
newer than the run and would make the next build run it again -- and relink
every plugin once more for nothing. Both callers keep the directory inside the
cargo target directory it was generated with, so the two are deleted together
and a header can only go missing alongside the build that wrote it.
*/

use std::env;
use std::fs;
use std::path::{Path, PathBuf};

/// One generated header: its file name, and the source files, relative to the
/// crate's manifest directory, whose items it declares.
pub struct Header<'a> {
    pub name: &'a str,
    pub sources: &'a [&'a str],
}

/// Writes every header in `headers`; panics -- which fails the build with the
/// message -- when one cannot be generated or written.
pub fn generate(headers: &[Header]) {
    let crate_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR"));
    let out_dir = PathBuf::from(env::var("OUT_DIR").expect("OUT_DIR"));
    let include_dir = env::var_os("NI_CAPI_INCLUDE_DIR").map(PathBuf::from);

    println!("cargo:rerun-if-env-changed=NI_CAPI_INCLUDE_DIR");
    // The whole src/ tree, not just the named files: cbindgen follows their
    // `mod` declarations, and a module added under one of them is read too.
    println!("cargo:rerun-if-changed={}", crate_dir.join("src").display());

    for header in headers {
        let stem = header.name.strip_suffix(".h").expect("a header's name ends in .h");
        let config_path = crate_dir.join("cbindgen").join(format!("{stem}.toml"));
        println!("cargo:rerun-if-changed={}", config_path.display());
        let config = cbindgen::Config::from_file(&config_path)
            .unwrap_or_else(|e| panic!("{}: {e}", config_path.display()));

        let mut builder = cbindgen::Builder::new().with_config(config);
        for source in header.sources {
            let path = crate_dir.join(source);
            println!("cargo:rerun-if-changed={}", path.display());
            builder = builder.with_src(path);
        }
        let mut text = Vec::new();
        builder
            .generate()
            .unwrap_or_else(|e| panic!("{}: cbindgen: {e}", header.name))
            .write(&mut text);

        write_if_changed(&out_dir.join(header.name), &text);
        if let Some(dir) = &include_dir {
            write_if_changed(&dir.join(header.name), &text);
        }
    }
}

/// `text` into `path` unless it already holds exactly that, through a
/// temporary file renamed into place, so no reader ever sees half a header.
fn write_if_changed(path: &Path, text: &[u8]) {
    if fs::read(path).is_ok_and(|old| old == text) {
        return;
    }
    let dir = path.parent().expect("a header path has a directory");
    fs::create_dir_all(dir).unwrap_or_else(|e| panic!("{}: {e}", dir.display()));
    let tmp = path.with_extension(format!("h.{}.tmp", std::process::id()));
    fs::write(&tmp, text).unwrap_or_else(|e| panic!("{}: {e}", tmp.display()));
    fs::rename(&tmp, path).unwrap_or_else(|e| panic!("{}: {e}", path.display()));
}
