// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
EVERY BLOB, SLOT FILE AND BANK A BUILD HAS WRITTEN, READ BY THIS ONE.

`fixtures/state` holds the state blobs, named for where each came from:

- `v<N>-<commit>-fresh|edited|heavy`: WRITTEN BY THAT COMMIT. Every writer the
  git history has had -- the C module's, the C core's after the split, the Rust
  port's and each later version's -- was built from its commit and run, once on
  a fresh patch and once on one edited in every field that writer knew. Where
  two commits wrote the same bytes only the first is kept (the Rust port wrote
  the C v4 writer's bytes exactly).
- `v<N>-<suite>-test-*`: the format examples the test suites load -- the
  `sound`, `paste` and `state` modules, `tests/test_gate.c`, `tests/test_core.c`,
  tg-capi's shell and `tests/cpp` -- including the versionless, v1 and v2 blobs
  that predate the history.
- `v<N>-schwung-*`: a blob as Schwung hands it back. A patch file stores it as
  an object written by `JSON.stringify(_, null, 2)`: indented, and its numbers
  rewritten by JavaScript (`7.50` is `7.5`, `33.00` is `33`). Older Schwung
  passed that text on as it was, newer Schwung compacts it first.

`fixtures/slot` holds the slot and bank files the current writer exports, and
the editor harness's, which the mock host answers an import with.

`expected.txt` beside each is what every fixture READS TO: the whole engine,
every slot's sound and pattern exactly (floats as the shortest text that is
that f32 and no other), loaded into a fresh engine and into one whose every
slot already differs -- which is what pins the keys a blob does not carry. The
lines above each are what the paste classifier and the version probe say about
the same text.

IT WAS WRITTEN BY THE READER THE SERDE ONE REPLACED, before it was replaced. A
port that reads any old blob differently fails here, value by value.

    TG_FIXTURES_BLESS=1 cargo test -p tg-core --test fixtures

rewrites both files, for a change that is MEANT to read something differently
-- and the commit that does it says why.
*/

use std::collections::BTreeMap;
use std::fmt::Write;
use std::path::{Path, PathBuf};
use tg_core::params::Param;
use tg_core::{Instance, MAX_STEPS, SLOTS};

const SR: f64 = 44100.0;

fn dir(kind: &str) -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures").join(kind)
}

/// Every fixture in `fixtures/<kind>`, by name, with its exact bytes.
fn fixtures(kind: &str) -> Vec<(String, String)> {
    let mut out: Vec<(String, String)> = std::fs::read_dir(dir(kind))
        .unwrap()
        .map(|e| e.unwrap().path())
        .filter(|p| p.file_name().is_some_and(|n| n != "expected.txt"))
        .map(|p| {
            let name = p.file_name().unwrap().to_string_lossy().into_owned();
            (name, std::fs::read_to_string(&p).unwrap())
        })
        .collect();
    out.sort();
    out
}

/*
 * AN ENGINE WITH SOMETHING TO LOSE: every slot a different sound, length,
 * mask, tie, accent and arrival order, the sixth slot current and the cursor
 * far along. A load keeps whatever its blob does not carry and an import
 * replaces only its own slots, so a fresh engine alone would not tell a
 * reader that keeps a value from one that resets it to the default.
 *
 * No roll: the generator is not part of what this file pins.
 */
fn busy() -> Instance {
    let mut p = Instance::new(SR);
    let masks = ["1", "F00F", "A5A5A5", "8000000000000001", "FFFFFFFF0F", "3", "123456789ABCDEF0", "77"];
    for s in 0..SLOTS {
        let v = s as f64;
        p.set_num(Param::Slot, v);
        p.set_param("length", &(3 + 13 * s).to_string());
        p.set_param("pattern", masks[s]);
        p.set_param("ties", if s % 2 == 0 { "2" } else { "100" });
        p.set_param("cursor", "1");
        p.set_param("step_amount", &format!("0.{}", s + 1));
        p.set_param("cursor", "0");
        p.set_param("step_order", "2");
        for (param, value) in [
            (Param::Rate, ((s + 2) % 13) as f64),
            (Param::Attack, 10.0 * v + 0.5),
            (Param::Decay, 5.0 * v + 1.0),
            (Param::Sustain, 0.1 * v + 0.05),
            (Param::Release, 3.0 * v + 2.0),
            (Param::Hold, 1.0 - 0.07 * v),
            (Param::Amount, 0.9 - 0.1 * v),
            (Param::Fade, 1.0 - 0.05 * v),
            (Param::FadeSoft, (s % 2) as f64),
            (Param::FadeDir, ((s / 2) % 2) as f64),
            (Param::Legato, ((s / 3) % 2) as f64),
            (Param::TimeMode, (s % 2) as f64),
            (Param::Curve, (s % 3) as f64),
        ] {
            p.set_num(param, value);
        }
    }
    p.set_num(Param::Slot, 5.0);
    p.set_param("cursor", "40");
    p
}

/// A mask's 128 steps as 32 hex digits, the last step first.
fn bits(on: impl Fn(usize) -> bool) -> String {
    let mut s = String::new();
    for nibble in (0..MAX_STEPS / 4).rev() {
        let v = (0..4).fold(0u8, |acc, b| acc | (on(nibble * 4 + b) as u8) << b);
        write!(s, "{v:X}").unwrap();
    }
    s
}

/*
 * THE WHOLE SAVED ENGINE, EXACTLY: the current slot and the cursor, then every
 * slot's thirteen sound values and its pattern -- all 128 levels and arrival
 * ranks, past the length too, because a Length turned back up reads them.
 * Floats are written as `{:?}` of the f32, the shortest text that parses back
 * to those bits and no others, so two dumps agree only when the values do.
 */
fn dump(p: &mut Instance) -> String {
    let mut out = String::new();
    let (slot, cursor) = (p.slot(), p.cursor());
    writeln!(out, "slot {slot} cursor {cursor}").unwrap();
    for s in 0..SLOTS {
        p.set_num(Param::Slot, s as f64);
        let n = p.numbers();
        let f = |param: Param| n[param as usize] as f32;
        let i = |param: Param| n[param as usize] as i64;
        writeln!(
            out,
            "s{s} rate {} attack {:?} decay {:?} sustain {:?} release {:?} hold {:?} amount {:?} \
             fade {:?} fsoft {} fdir {} legato {} tmode {} curve {}",
            i(Param::Rate),
            f(Param::Attack),
            f(Param::Decay),
            f(Param::Sustain),
            f(Param::Release),
            f(Param::Hold),
            f(Param::Amount),
            f(Param::Fade),
            i(Param::FadeSoft),
            i(Param::FadeDir),
            i(Param::Legato),
            i(Param::TimeMode),
            i(Param::Curve),
        )
        .unwrap();
    }
    p.set_num(Param::Slot, slot as f64);
    for s in 0..SLOTS {
        let pat = p.pattern_in(s).unwrap();
        let mut depth = String::new();
        let mut order = String::new();
        for i in 0..MAX_STEPS {
            write!(depth, "{:02X}", pat.depth(i).unwrap()).unwrap();
            write!(order, "{:02X}", pat.order(i).unwrap()).unwrap();
        }
        writeln!(
            out,
            "p{s} length {} steps {} ties {} depth {depth} order {order}",
            pat.length(),
            bits(|i| pat.on(i)),
            bits(|i| pat.tied(i)),
        )
        .unwrap();
    }
    out
}

/// What one fixture reads to, as `expected.txt` holds it.
fn reads_to(kind: &str, text: &str) -> String {
    let mut out = String::new();
    writeln!(out, "paste {:?}", tg_core::paste::classify(text)).unwrap();
    if kind == "state" {
        writeln!(out, "version {}", tg_core::state::version(text)).unwrap();
    } else {
        writeln!(out, "check {:?}", tg_core::slotfile::check(text)).unwrap();
    }
    for (base, mut p) in [("fresh", Instance::new(SR)), ("busy", busy())] {
        writeln!(out, "-- into a {base} engine").unwrap();
        if kind == "state" {
            p.set_param("state", text);
        } else {
            writeln!(out, "import {:?}", p.import(text)).unwrap();
        }
        out.push_str(&dump(&mut p));
    }
    out
}

/// `expected.txt`, as fixture name -> what it reads to: a line `== <name>`
/// opens each one.
fn sections(text: &str) -> BTreeMap<String, String> {
    let mut out = BTreeMap::new();
    let mut current: Option<(String, String)> = None;
    for line in text.lines() {
        if let Some(name) = line.strip_prefix("== ") {
            out.extend(current.take());
            current = Some((name.to_owned(), String::new()));
        } else if let Some((_, body)) = current.as_mut() {
            body.push_str(line);
            body.push('\n');
        }
    }
    out.extend(current);
    out
}

fn check(kind: &str) {
    let actual: BTreeMap<String, String> =
        fixtures(kind).into_iter().map(|(name, text)| (name, reads_to(kind, &text))).collect();
    let path = dir(kind).join("expected.txt");
    if std::env::var_os("TG_FIXTURES_BLESS").is_some() {
        let mut text = String::new();
        for (name, body) in &actual {
            write!(text, "== {name}\n{body}").unwrap();
        }
        std::fs::write(&path, text).unwrap();
        return;
    }
    let expected = sections(&std::fs::read_to_string(&path).unwrap());
    assert_eq!(
        actual.keys().collect::<Vec<_>>(),
        expected.keys().collect::<Vec<_>>(),
        "a fixture without an expectation, or an expectation without its fixture"
    );
    for (name, body) in &actual {
        assert_eq!(body, &expected[name], "{kind}/{name} no longer reads to what it did");
    }
}

#[test]
fn every_state_blob_ever_written_reads_to_the_engine_it_always_did() {
    check("state");
}

#[test]
fn every_slot_and_bank_file_reads_to_the_engine_it_always_did() {
    check("slot");
}

/*
 * WHAT THE CURRENT WRITER WROTE, IT WRITES AGAIN: a fixture from this format's
 * own writer, loaded or imported and written back, is the same bytes. The
 * older formats are not, by design -- they come back as the current one.
 */
#[test]
fn the_current_formats_round_trip_byte_for_byte() {
    for (name, text) in fixtures("state").into_iter().filter(|(n, _)| n.starts_with("v7-") && !n.contains("schwung") && !n.contains("test")) {
        let mut p = Instance::new(SR);
        p.set_param("state", &text);
        let mut buf = vec![0u8; 16 * 1024];
        let n = p.get_param("state", &mut buf) as usize;
        assert_eq!(std::str::from_utf8(&buf[..n]).unwrap(), text, "{name}");
    }
    for (name, text) in fixtures("slot") {
        let mut p = Instance::new(SR);
        let kind = p.import(&text).unwrap();
        let mut buf = vec![0u8; 16 * 1024];
        let n = p.export(kind, &mut buf) as usize;
        assert_eq!(std::str::from_utf8(&buf[..n]).unwrap(), text, "{name}");
    }
}
