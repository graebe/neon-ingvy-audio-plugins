//! Slot files: what they hold, what they refuse, and that a refusal changes
//! nothing.

use super::{check, Error, Kind, BANK_FORMAT, SLOT_FORMAT};
use crate::params::Param;
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

/// Every slot different in sound and pattern, accents and orders included.
fn busy() -> Instance {
    let mut p = Instance::new(SR);
    for s in 0..SLOTS {
        p.set_num(Param::Slot, s as f64);
        p.set_param("length", &(7 + 8 * s).to_string());
        p.set_param("randomize", &(300 + s).to_string());
        p.set_param("cursor", "2");
        p.set_param("step_amount", "0.4");
        p.set_param("amount", &format!("0.{}", 2 + s));
        p.set_param("attack", &format!("{}.5", 10 * s));
        p.set_param("rate", &s.to_string());
        p.set_param("curve", &(s % 3).to_string());
        p.set_param("fade_dir", &(s % 2).to_string());
    }
    p.set_num(Param::Slot, 3.0);
    p
}

#[test]
fn a_slot_file_says_what_it_is_and_reads_as_text() {
    let p = Instance::new(SR);
    let f = export(&p, Kind::Slot);
    assert_eq!(
        f,
        format!(
            "{{\n  \"format\": \"{SLOT_FORMAT}\",\n  \"version\": 1,\n  \
             \"sound\": \"1/16:1.60:16.00:1.000:16.00:1.000:1.000:1.0000:0:0:0:0:0\",\n  \
             \"pattern\": \"5555:0:16:\"\n}}\n"
        )
    );
    assert_eq!(check(&f), Ok(Kind::Slot));
}

#[test]
fn one_slot_round_trips_into_the_current_slot_and_no_other() {
    let a = busy();
    let file = export(&a, Kind::Slot);
    let mut b = Instance::new(SR);
    b.set_num(Param::Slot, 6.0);
    let before = export(&b, Kind::Bank);
    assert_eq!(b.import(&file), Ok(Kind::Slot));
    assert_eq!(export(&b, Kind::Slot), file, "byte for byte");
    /* Slot 7 is a's slot 4 now; every other slot is as it was. */
    let mut fresh = Instance::new(SR);
    fresh.set_num(Param::Slot, 6.0);
    let after = export(&b, Kind::Bank);
    let differing: Vec<&str> = before.lines().zip(after.lines()).filter(|(x, y)| x != y).map(|(x, _)| x).collect();
    assert_eq!(differing.len(), 2, "only slot 7's sound and pattern lines: {differing:?}");
    assert!(differing.iter().all(|l| l.contains("sound7") || l.contains("pattern7")));
}

#[test]
fn all_eight_round_trip_byte_for_byte() {
    let a = busy();
    let file = export(&a, Kind::Bank);
    assert!(file.contains(&format!("\"format\": \"{BANK_FORMAT}\"")));
    let mut b = Instance::new(SR);
    assert_eq!(b.import(&file), Ok(Kind::Bank));
    assert_eq!(export(&b, Kind::Bank), file);
    /* The current slot is still b's own choice; the slots are a's. */
    assert_eq!(b.slot(), 0);
    b.set_num(Param::Slot, 3.0);
    let mut a = a;
    assert_eq!(get(&b, "params"), get(&a, "params"));
    assert_eq!(get(&b, "ui"), {
        a.set_param("cursor", &b.cursor().to_string());
        get(&a, "ui")
    });
}

#[test]
fn an_import_moves_the_revision() {
    let a = busy();
    let mut b = Instance::new(SR);
    let rev = b.state_rev();
    b.import(&export(&a, Kind::Bank)).unwrap();
    assert_ne!(b.state_rev(), rev);
}

#[test]
fn values_out_of_range_are_clamped_as_a_state_load_clamps_them() {
    let file = format!(
        "{{\"format\":\"{SLOT_FORMAT}\",\"version\":1,\
         \"sound\":\"1/4:900:5:7:20:0.5:-3:2:1:1:1:1:2\",\"pattern\":\"F:0:4\"}}"
    );
    let mut p = Instance::new(SR);
    assert_eq!(p.import(&file), Ok(Kind::Slot));
    for (k, v) in [("attack", "200.0"), ("sustain", "1.00"), ("amount", "0.00"), ("fade", "1.00"),
                   ("curve", "2"), ("length", "3"), ("pattern", "F")] {
        assert_eq!(get(&p, k), v, "{k}");
    }
}

#[test]
fn a_newer_version_is_refused_and_says_so() {
    let p = busy();
    let file = export(&p, Kind::Slot).replace("\"version\": 1", "\"version\": 2");
    assert_eq!(check(&file), Err(Error::Newer(2)));
    let mut s = String::new();
    Error::Newer(2).describe(&mut s).unwrap();
    assert!(s.contains("newer"), "{s}");
    for v in ["0", "1.5", "\"1\"", "-1"] {
        let f = export(&p, Kind::Slot).replace("\"version\": 1", &format!("\"version\": {v}"));
        assert_eq!(check(&f), Err(Error::BadVersion), "{v}");
    }
}

#[test]
fn what_is_not_a_slot_file_is_refused_and_changes_nothing() {
    /* A fresh slot, whose every field is known text. */
    let good = export(&Instance::new(SR), Kind::Slot);
    let bank = export(&busy(), Kind::Bank);
    let cases: Vec<(String, Error)> = vec![
        (String::new(), Error::Empty),
        ("   \n".into(), Error::Empty),
        ("x".repeat(20_000), Error::TooLarge),
        ("hello".into(), Error::NotAFile),
        ("[1,2]".into(), Error::NotAFile),
        (good.replace('}', ""), Error::NotAFile),
        (format!("{good} trailing"), Error::NotAFile),
        (good.replace("\"version\": 1,", "\"version\": 1,,"), Error::NotAFile),
        /* A state blob is a Trance Gate text, but not a slot file. */
        (get(&Instance::new(SR), "state"), Error::WrongFormat),
        (good.replace(SLOT_FORMAT, "ni-side-chain-slot"), Error::WrongFormat),
        (good.replace("\"version\": 1", "\"version\": 1, \"extra\": \"x\""), Error::UnknownKey),
        (good.replace("\"version\": 1", "\"version\": 1, \"version\": 1"), Error::Duplicate),
        (good.replace("\"sound\"", "\"sound1\""), Error::Missing { pattern: false, slot: 0 }),
        (good.replace("1/16:", "1/17:"), Error::Malformed { pattern: false, slot: 0 }),
        (good.replace("1.60:", "1,60:"), Error::Malformed { pattern: false, slot: 0 }),
        (good.replace(":0:0:0:0:0\"", ":0:0:0:0:7\""), Error::Malformed { pattern: false, slot: 0 }),
        (good.replace(":0:0:0:0:0\"", ":0:0:0:0\""), Error::Malformed { pattern: false, slot: 0 }),
        (bank.replace("\"pattern5\"", "\"pattern9\""), Error::Missing { pattern: true, slot: 5 }),
        (bank.replace("\"pattern8\": \"", "\"pattern8\": \"G"), Error::Malformed { pattern: true, slot: 8 }),
        (good.replace(SLOT_FORMAT, BANK_FORMAT), Error::Missing { pattern: false, slot: 1 }),
        (bank.replace(BANK_FORMAT, SLOT_FORMAT), Error::Missing { pattern: false, slot: 0 }),
    ];
    for (text, want) in cases {
        let mut p = busy();
        let before = get(&p, "state");
        let rev = p.state_rev();
        assert_eq!(p.import(&text), Err(want), "{:.80}", text);
        assert_eq!(get(&p, "state"), before, "a refused file changed the patch");
        assert_eq!(p.state_rev(), rev);
    }
    /* Patterns this build could not have written. */
    for pat in ["F:0", "F:0:0", "F:0:129", "F:0:16:ABC", "F:0:16::1", "1FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF:0:16", "F:0:16:FF:01:0"] {
        let text = good.replace(&good[good.find("\"pattern\": \"").unwrap() + 12..good.rfind('"').unwrap()], pat);
        assert_eq!(check(&text), Err(Error::Malformed { pattern: true, slot: 0 }), "{pat}");
    }
}

#[test]
fn every_refusal_has_words() {
    for e in [Error::Empty, Error::TooLarge, Error::NotAFile, Error::WrongFormat, Error::Newer(3),
              Error::BadVersion, Error::UnknownKey, Error::Duplicate,
              Error::Missing { pattern: true, slot: 2 }, Error::Malformed { pattern: false, slot: 0 }] {
        let mut s = String::new();
        e.describe(&mut s).unwrap();
        assert!(s.ends_with('.') && s.len() > 12, "{e:?}: {s}");
    }
}

#[test]
fn the_editor_harness_fixtures_are_files_this_engine_reads() {
    /* The mock host answers IMPORT with these; they must be real files, or the
     * e2e suite is testing a format nobody writes. */
    let slot = include_str!("../../../../../../plugins/trance-gate/ui/test/harness/fixtures/slot.nitgslot");
    let bank = include_str!("../../../../../../plugins/trance-gate/ui/test/harness/fixtures/bank.nitgbank");
    let mut p = Instance::new(SR);
    assert_eq!(p.import(slot), Ok(Kind::Slot));
    assert_eq!(export(&p, Kind::Slot), slot, "and byte for byte what this build writes");
    assert_eq!(p.import(bank), Ok(Kind::Bank));
    assert_eq!(export(&p, Kind::Bank), bank);
}
