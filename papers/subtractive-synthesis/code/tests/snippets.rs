// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Every Rust listing in the paper is this crate's code, verbatim.
 *
 * A listing opens with a comment naming where it comes from:
 *
 *   ```rust
 *   // code/src/osc/polyblep.rs#polyblep
 *   ...
 *   ```
 *
 * and the rest of the block must equal the region of that file between
 * `// ANCHOR: polyblep` and `// ANCHOR_END: polyblep`, with the region's
 * common indentation removed. A listing without a source line fails too: a
 * listing nobody compiles is the drift this test exists to stop. So does an
 * anchor the paper never prints, which is dead weight in the code.
 */

use std::collections::BTreeSet;
use std::fs;
use std::path::Path;

const PAPER: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../paper.md");

fn listings(md: &str) -> Vec<(usize, Vec<&str>)> {
    let mut out = Vec::new();
    let mut lines = md.lines().enumerate();
    while let Some((n, line)) = lines.next() {
        if line.trim_start().starts_with("```rust") {
            let body: Vec<&str> = lines.by_ref().map(|(_, l)| l).take_while(|l| !l.trim_start().starts_with("```")).collect();
            out.push((n + 1, body));
        }
    }
    out
}

fn region(src: &str, name: &str) -> Option<Vec<String>> {
    let open = format!("// ANCHOR: {name}");
    let close = format!("// ANCHOR_END: {name}");
    let lines: Vec<&str> = src.lines().collect();
    let a = lines.iter().position(|l| l.trim() == open)?;
    let b = lines.iter().position(|l| l.trim() == close)?;
    let body = &lines[a + 1..b];
    let indent = body.iter().filter(|l| !l.trim().is_empty()).map(|l| l.len() - l.trim_start().len()).min().unwrap_or(0);
    Some(body.iter().map(|l| if l.len() >= indent { l[indent..].to_string() } else { String::new() }).collect())
}

fn anchors(dir: &Path, out: &mut BTreeSet<(String, String)>) {
    for entry in fs::read_dir(dir).unwrap() {
        let path = entry.unwrap().path();
        if path.is_dir() {
            anchors(&path, out);
        } else if path.extension().is_some_and(|e| e == "rs") {
            let rel = path.strip_prefix(concat!(env!("CARGO_MANIFEST_DIR"), "/..")).unwrap();
            for line in fs::read_to_string(&path).unwrap().lines() {
                if let Some(name) = line.trim().strip_prefix("// ANCHOR: ") {
                    out.insert((rel.display().to_string(), name.to_string()));
                }
            }
        }
    }
}

#[test]
fn every_listing_is_the_code_it_names() {
    let md = fs::read_to_string(PAPER).expect("../paper.md");
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("..");
    let mut printed = BTreeSet::new();
    let found = listings(&md);
    assert!(!found.is_empty(), "the paper prints no Rust at all");

    for (line, body) in found {
        let head = body.first().copied().unwrap_or_default();
        let source = head.trim().strip_prefix("// ").unwrap_or_else(|| panic!("paper.md:{line}: a listing must open with `// <file>#<anchor>`"));
        let (file, name) = source.split_once('#').unwrap_or_else(|| panic!("paper.md:{line}: `{source}` names no anchor"));
        let src = fs::read_to_string(root.join(file)).unwrap_or_else(|_| panic!("paper.md:{line}: no file {file}"));
        let want = region(&src, name).unwrap_or_else(|| panic!("paper.md:{line}: {file} has no anchor {name}"));
        let got: Vec<String> = body[1..].iter().map(|l| l.to_string()).collect();
        assert_eq!(got, want, "paper.md:{line}: the listing differs from {file}#{name}");
        printed.insert((file.to_string(), name.to_string()));
    }

    let mut all = BTreeSet::new();
    anchors(&root.join("code/src"), &mut all);
    let unprinted: Vec<_> = all.difference(&printed).collect();
    assert!(unprinted.is_empty(), "anchors the paper never prints: {unprinted:?}");
}
