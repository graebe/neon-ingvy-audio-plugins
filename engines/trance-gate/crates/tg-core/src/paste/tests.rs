// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Paste: what each clipboard text is taken for, and that a refusal changes
//! nothing.

use super::{classify, Holds, Refused};
use crate::params::Param;
use crate::slotfile::{self, Kind};
use crate::{Instance, SLOTS};

const SR: f64 = 44100.0;

fn get(p: &Instance, key: &str) -> String {
    let mut buf = [0u8; 8192];
    let n = p.get_param(key, &mut buf);
    String::from_utf8(buf[..n.max(0) as usize].to_vec()).unwrap()
}

fn export(p: &Instance, kind: Kind) -> String {
    let mut buf = vec![0u8; 16 * 1024];
    let n = p.export(kind, &mut buf);
    String::from_utf8(buf[..n as usize].to_vec()).unwrap()
}

fn words(r: Refused) -> String {
    let mut s = String::new();
    r.describe(&mut s).unwrap();
    s
}

/// Every slot different in sound and pattern, slot 1 current.
fn busy() -> Instance {
    let mut p = Instance::new(SR);
    for s in 0..SLOTS {
        p.set_num(Param::Slot, s as f64);
        p.set_param("length", &(5 + 3 * s).to_string());
        p.set_param("randomize", &(900 + s).to_string());
        p.set_param("cursor", "1");
        p.set_param("step_amount", "0.3");
        p.set_param("amount", &format!("0.{}", 1 + s));
        p.set_param("rate", &(s + 2).to_string());
        p.set_param("curve", &(s % 3).to_string());
    }
    p.set_num(Param::Slot, 0.0);
    p
}

#[test]
fn a_slot_a_bank_and_a_patch_are_each_what_they_are() {
    let p = busy();
    assert_eq!(classify(&export(&p, Kind::Slot)), Ok(Holds::Slot));
    assert_eq!(classify(&export(&p, Kind::Bank)), Ok(Holds::Bank));
    assert_eq!(classify(&get(&p, "state")), Ok(Holds::Patch));
    assert_eq!(classify(&get(&Instance::new(SR), "state")), Ok(Holds::Patch));
    /* With the whitespace a clipboard adds around it. */
    assert_eq!(classify(&format!("\n  {}\n", get(&p, "state"))), Ok(Holds::Patch));
}

#[test]
fn every_older_patch_is_still_a_patch() {
    /* The blobs the C tests load, one per format version, and the oldest with
     * no version at all: a Move patch from any build pastes. */
    for blob in [
        "{\"slot\":0,\"rate\":5,\"p0\":\"FFFF:0:16\"}",
        "{\"sv\":1,\"slot\":0,\"rate\":\"1/16\",\"depth\":1.000,\"mix\":1.000,\"p0\":\"FFFF:0:16\"}",
        "{\"sv\":2,\"slot\":0,\"rate\":\"1/16\",\"mix\":1.000,\"p0\":\"FFFF:0:16\"}",
        "{\"sv\":3,\"slot\":0,\"rate\":\"1/16\",\"attack\":2.00,\"decay\":20.00,\"sustain\":1.000,\
         \"release\":20.00,\"hold\":1.000,\"amount\":1.000,\"legato\":0}",
        "{\"sv\":5,\"slot\":0,\"rate\":\"1/16\",\"attack\":0,\"decay\":0,\"sustain\":1,\"release\":0,\
         \"hold\":1,\"amount\":1,\"fade\":1.0000,\"fsoft\":0,\"legato\":0,\"tmode\":0,\"curve\":0,\
         \"p0\":\"1111:0:16::04000000010000000200000003000000\"}",
    ] {
        assert_eq!(classify(blob), Ok(Holds::Patch), "{blob}");
    }
}

#[test]
fn anything_else_is_refused_with_a_reason() {
    let slot = export(&busy(), Kind::Slot);
    let blob = get(&busy(), "state");
    let cases: Vec<(String, Refused)> = vec![
        (String::new(), Refused::Empty),
        ("  \n".into(), Refused::Empty),
        ("hello".into(), Refused::NotTranceGate),
        ("https://example.com".into(), Refused::NotTranceGate),
        ("tg1:slot=0:len=16:steps=5555".into(), Refused::NotTranceGate),
        ("{}".into(), Refused::NotTranceGate),
        ("{\"slot\":0}".into(), Refused::NotTranceGate),
        ("{\"name\":\"x\",\"p0\":\"FFFF:0:16\"}".into(), Refused::NotTranceGate),
        ("{\"format\": \"ni-spectrogram\", \"version\": 1}".into(), Refused::NotTranceGate),
        ("[1, 2, 3]".into(), Refused::NotTranceGate),
        ("x".repeat(slotfile::MAX_BYTES + 1), Refused::NotTranceGate),
        (" ".repeat(slotfile::MAX_BYTES + 1), Refused::NotTranceGate),
        (blob.replace("\"sv\":7", "\"sv\":8"), Refused::Newer(8)),
        (blob.replace("\"slot\":0", "\"slot\":9"), Refused::Damaged),
        (blob.replace("\"rate\":\"1/2T\"", "\"rate\":\"1/5\""), Refused::Damaged),
        (blob.replace("\"amount\":", "\"amount\":\"x\",\"y\":"), Refused::Damaged),
        ("{\"sv\":7,\"p0\":\"GGGG:0:16\"}".into(), Refused::Damaged),
        ("{\"sv\":7,\"p0\":\"FFFF:0:16\",\"s3\":\"1/16:1\"}".into(), Refused::Damaged),
        ("{\"sv\":7,\"p9\":\"FFFF:0:16\"}".into(), Refused::NotTranceGate),
        (slot.replace("\"version\": 1", "\"version\": 4"), Refused::File(slotfile::Error::Newer(4))),
    ];
    assert!(blob.contains("\"rate\":\"1/2T\""), "the fixture's rate moved: {blob}");
    for (text, want) in cases {
        let head: String = text.chars().take(60).collect();
        assert_eq!(classify(&text), Err(want), "{head}");
        let mut p = busy();
        let before = get(&p, "state");
        assert_eq!(p.paste(&text), Err(want));
        assert_eq!(get(&p, "state"), before, "a refused paste changed the patch: {head}");
    }
    assert_eq!(words(Refused::NotTranceGate), "The clipboard doesn't hold a Trance Gate slot.");
    assert_eq!(words(Refused::Empty), "The clipboard is empty.");
    assert_eq!(words(Refused::Damaged), "The patch on the clipboard is damaged.");
    assert!(words(Refused::Newer(8)).starts_with("The patch is version 8, made by a newer"));
    assert!(words(Refused::File(slotfile::Error::Newer(4))).starts_with("The file is version 4"));
}

#[test]
fn a_copied_slot_pastes_into_another_slot_and_leaves_the_rest() {
    /* The owner's case: copy slot 1, select slot 2, paste. */
    let mut p = busy();
    let copied = export(&p, Kind::Slot);
    let bank = export(&p, Kind::Bank);
    p.set_num(Param::Slot, 1.0);
    assert_eq!(p.paste(&copied), Ok(Holds::Slot));
    assert_eq!(export(&p, Kind::Slot), copied, "slot 2 is slot 1's sound and pattern, byte for byte");
    p.set_num(Param::Slot, 0.0);
    assert_eq!(export(&p, Kind::Slot), copied, "slot 1 is unchanged");
    /* Every other slot is as it was: only slot 2's two lines moved. */
    let after = export(&p, Kind::Bank);
    let moved: Vec<&str> = bank.lines().zip(after.lines()).filter(|(a, b)| a != b).map(|(_, b)| b).collect();
    assert_eq!(moved.len(), 2, "{moved:?}");
    assert!(moved.iter().all(|l| l.contains("sound2") || l.contains("pattern2")));
}

#[test]
fn a_slot_pasted_into_a_slot_not_yet_current_lands_there() {
    /* The host moves the Slot in the block the paste is applied in: the paste
     * still goes where the person was looking. */
    let mut p = busy();
    let copied = export(&p, Kind::Slot);
    let one = get(&p, "state");
    assert_eq!(p.paste_into(1, &copied), Ok(Holds::Slot));
    assert_eq!(export(&p, Kind::Slot), copied, "slot 1 is as it was");
    p.set_num(Param::Slot, 1.0);
    assert_eq!(export(&p, Kind::Slot), copied, "slot 2 holds the paste");
    assert_ne!(get(&p, "state"), one);
    /* Out of range is the current slot. */
    let mut q = busy();
    q.set_num(Param::Slot, 4.0);
    assert_eq!(q.paste_into(99, &copied), Ok(Holds::Slot));
    assert_eq!(export(&q, Kind::Slot), copied);
}

#[test]
fn a_bank_replaces_all_eight_and_a_patch_everything() {
    let a = busy();
    let mut b = Instance::new(SR);
    assert_eq!(b.paste(&export(&a, Kind::Bank)), Ok(Holds::Bank));
    assert_eq!(export(&b, Kind::Bank), export(&a, Kind::Bank));

    let mut c = Instance::new(SR);
    c.set_num(Param::Slot, 5.0);
    assert_eq!(c.paste(&get(&a, "state")), Ok(Holds::Patch));
    assert_eq!(get(&c, "state"), get(&a, "state"), "the whole patch, its current slot included");
}
