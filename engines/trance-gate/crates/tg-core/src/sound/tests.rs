//! Every slot keeps its own sound: what a switch recalls, what a load fills in,
//! and what the gain does while it happens.

use crate::envelope::Stage;
use crate::params::Param;
use crate::{Instance, Transport, SLOTS};

const SR: f64 = 44100.0;
const BPM: f32 = 120.0;

fn get(p: &Instance, key: &str) -> String {
    let mut buf = [0u8; 8192];
    let n = p.get_param(key, &mut buf);
    String::from_utf8(buf[..n.max(0) as usize].to_vec()).unwrap()
}

/*
 * EVERY PER-SLOT PARAMETER, with two values that read back differently from
 * each other and from a fresh slot's. The keys are the Move's chain_params and
 * the string door; `length` is the pattern's, and is here because a switch
 * recalls it too.
 */
const TABLE: &[(&str, &str, &str)] = &[
    ("rate", "1/8", "1/32T"),
    ("length", "7", "23"),
    ("legato", "1", "0"),
    ("time_mode", "1", "0"),
    ("curve", "2", "1"),
    ("amount", "0.70", "0.25"),
    ("hold", "0.60", "0.35"),
    ("attack", "12.5", "40.0"),
    ("decay", "33.0", "2.5"),
    ("sustain", "0.40", "0.85"),
    ("release", "30.0", "75.5"),
    ("fade", "0.50", "0.20"),
    ("fade_soft", "1", "0"),
    ("fade_dir", "1", "0"),
];

fn set_all(p: &mut Instance, column: usize) {
    for row in TABLE {
        p.set_param(row.0, if column == 0 { row.1 } else { row.2 });
    }
}

/// The slot's whole sound, as the readouts say it, read without disturbing
/// which slot is current.
fn sound_of(p: &mut Instance, slot: usize) -> Vec<String> {
    let was = p.slot();
    p.set_num(Param::Slot, slot as f64);
    let v = TABLE.iter().map(|row| format!("{}={}", row.0, get(p, row.0))).collect();
    p.set_num(Param::Slot, was as f64);
    v
}

fn column(c: usize) -> Vec<String> {
    TABLE.iter().map(|row| format!("{}={}", row.0, if c == 0 { row.1 } else { row.2 })).collect()
}

#[test]
fn a_switch_recalls_every_parameter_of_the_slot_it_lands_on() {
    let mut p = Instance::new(SR);
    let fresh = sound_of(&mut p, 5);
    set_all(&mut p, 0);
    p.set_param("slot", "5");
    assert_eq!(sound_of(&mut p, 5), fresh, "a slot nobody edited still has the fresh sound");
    set_all(&mut p, 1);
    p.set_param("slot", "0");
    assert_eq!(sound_of(&mut p, 0), column(0), "slot 1 came back whole");
    p.set_param("slot", "5");
    assert_eq!(sound_of(&mut p, 5), column(1), "and so did slot 6");
    /* The `params` readout the plugin mirrors is the current slot's, too. */
    let f: Vec<String> = get(&p, "params").split(':').map(str::to_owned).collect();
    assert_eq!((f[0].as_str(), f[4].as_str(), f[6].as_str()), ("5", "1/32T", "0.25"));
}

#[test]
fn each_parameter_on_its_own_belongs_to_its_slot() {
    for row in TABLE {
        let (key, a, b) = *row;
        let mut p = Instance::new(SR);
        let fresh = get(&p, key);
        p.set_param(key, a);
        assert_eq!(get(&p, key), a, "{key} took its value");
        p.set_num(Param::Slot, 3.0);
        assert_eq!(get(&p, key), fresh, "{key} leaked into slot 4");
        p.set_param(key, b);
        p.set_num(Param::Slot, 0.0);
        assert_eq!(get(&p, key), a, "{key} was not recalled for slot 1");
        p.set_num(Param::Slot, 3.0);
        assert_eq!(get(&p, key), b, "{key} was not recalled for slot 4");
    }
}

#[test]
fn the_numeric_door_writes_the_current_slot_only() {
    /* Host automation arrives through `set_num`. */
    let mut p = Instance::new(SR);
    p.set_num(Param::Slot, 2.0);
    p.set_num(Param::Amount, 0.3);
    p.set_num(Param::Rate, 3.0);
    p.set_num(Param::Slot, 6.0);
    assert_eq!((get(&p, "amount"), get(&p, "rate")), ("1.00".into(), "1/16".into()));
    p.set_num(Param::Slot, 2.0);
    assert_eq!((get(&p, "amount"), get(&p, "rate")), ("0.30".into(), "1/4".into()));
}

#[test]
fn the_slot_itself_is_not_per_slot() {
    /* One number, saved once: a slot cannot remember which slot it is. */
    let mut p = Instance::new(SR);
    for s in 0..SLOTS {
        p.set_num(Param::Slot, s as f64);
        p.set_param("amount", &format!("0.{s}"));
    }
    p.set_num(Param::Slot, 4.0);
    let blob = get(&p, "state");
    assert_eq!(blob.matches("\"slot\":").count(), 1, "{blob}");
    let mut q = Instance::new(SR);
    q.set_param("state", &blob);
    assert_eq!(q.slot(), 4);
    for s in 0..SLOTS {
        q.set_num(Param::Slot, s as f64);
        assert_eq!(get(&q, "slot"), s.to_string());
    }
}

#[test]
fn randomize_rolls_the_current_slot_and_leaves_the_others() {
    let mut p = Instance::new(SR);
    p.set_num(Param::Slot, 1.0);
    p.set_param("amount", "0.4");
    let other = sound_of(&mut p, 0);
    let pattern0 = { p.set_num(Param::Slot, 0.0); let s = get(&p, "pattern"); p.set_num(Param::Slot, 1.0); s };
    p.set_param("randomize", "4242");
    assert_eq!(sound_of(&mut p, 0), other);
    p.set_num(Param::Slot, 0.0);
    assert_eq!(get(&p, "pattern"), pattern0);
    p.set_num(Param::Slot, 1.0);
    assert_eq!(get(&p, "amount"), "0.40", "a roll is the pattern's, not the sound's");
}

/* ------------------------------------------------------------- the state blob */

#[test]
fn every_slots_sound_survives_a_save_and_a_load() {
    let mut a = Instance::new(SR);
    for s in 0..SLOTS {
        a.set_num(Param::Slot, s as f64);
        set_all(&mut a, s % 2);
        a.set_param("attack", &format!("{}.5", 10 + s));
        a.set_param("randomize", &(100 + s).to_string());
    }
    a.set_num(Param::Slot, 6.0);
    let blob = get(&a, "state");
    assert!(blob.starts_with("{\"sv\":7,"), "{blob}");
    let mut b = Instance::new(SR);
    b.set_param("state", &blob);
    assert_eq!(get(&b, "state"), blob, "byte for byte");
    for s in 0..SLOTS {
        assert_eq!(sound_of(&mut b, s), sound_of(&mut a, s), "slot {s}");
    }
    assert_eq!(b.slot(), 6);
}

#[test]
fn slots_that_sound_alike_cost_the_blob_nothing() {
    /* A slot is written only where it differs from the current one, so a patch
     * that never used per-slot sounds is the size it always was -- the bus
     * insert's 1024 bytes are measured against exactly that. */
    let p = Instance::new(SR);
    let blob = get(&p, "state");
    assert!(!blob.contains("\"s0\""), "{blob}");
    let mut q = Instance::new(SR);
    q.set_num(Param::Slot, 3.0);
    q.set_param("amount", "0.5");
    let blob = get(&q, "state");
    let written: Vec<usize> = (0..SLOTS).filter(|s| blob.contains(&format!("\"s{s}\":"))).collect();
    assert_eq!(written, [0, 1, 2, 4, 5, 6, 7], "{blob}");
}

/*
 * EVERY OLDER FORMAT LOADS INTO EIGHT IDENTICAL SLOTS: the one instance-wide
 * sound it carried, in all of them, beside each slot's own pattern -- and with
 * every migration of its own version still applied.
 */
#[test]
fn every_older_blob_loads_its_sound_into_all_eight_slots() {
    let pats = "\"p0\":\"5555:0:16\",\"p1\":\"FFFF:0:8\",\"p3\":\"F0F0:0:16:80FF\"";
    let v456 = "\"rate\":\"1/8\",\"attack\":12.50,\"decay\":40.00,\"sustain\":0.400,\"release\":30.00,\"hold\":0.600,\"amount\":0.700,\"legato\":1,\"tmode\":1,\"curve\":2";
    let cases: &[(&str, String, &[(&str, &str)])] = &[
        ("v6", format!("{{\"sv\":6,\"slot\":2,{v456},\"fade\":0.5000,\"fsoft\":1,\"fdir\":1,{pats}}}"),
         &[("rate", "1/8"), ("attack", "12.5"), ("decay", "40.0"), ("sustain", "0.40"), ("release", "30.0"),
           ("hold", "0.60"), ("amount", "0.70"), ("legato", "1"), ("time_mode", "1"), ("curve", "2"),
           ("fade", "0.50"), ("fade_soft", "1"), ("fade_dir", "1")]),
        ("v5", format!("{{\"sv\":5,\"slot\":2,{v456},\"fade\":0.5000,\"fsoft\":1,{pats}}}"),
         &[("attack", "12.5"), ("fade", "0.50"), ("fade_soft", "1"), ("fade_dir", "0")]),
        ("v4", format!("{{\"sv\":4,\"slot\":2,{v456},{pats}}}"),
         &[("attack", "12.5"), ("hold", "0.60"), ("fade", "1.00"), ("fade_soft", "0")]),
        /* v3: milliseconds, converted against 1/16 at Width 100%: a 125 ms gate. */
        ("v3", format!("{{\"sv\":3,\"slot\":2,\"rate\":\"1/16\",\"attack\":2.0,\"decay\":20.0,\"sustain\":0.5,\"release\":25.0,\"amount\":0.8,{pats}}}"),
         &[("attack", "1.6"), ("decay", "16.0"), ("release", "20.0"), ("amount", "0.80"), ("hold", "1.00"), ("legato", "0")]),
        /* v2: mix and depth were one quantity; the product is the amount. */
        ("v2", format!("{{\"sv\":2,\"slot\":2,\"rate\":7,\"attack\":12.5,\"decay\":20.0,\"sustain\":0.5,\"release\":25.0,\"mix\":0.5,\"depth\":0.5,{pats}}}"),
         &[("rate", "1/16"), ("attack", "10.0"), ("amount", "0.25")]),
        ("v1", format!("{{\"sv\":1,\"slot\":2,\"rate\":\"1/4\",\"attack\":12.5,\"mix\":0.6,\"p0\":\"5555:0:16\",\"p1\":\"FFFF:0:8\"}}"),
         &[("rate", "1/4"), ("amount", "0.60")]),
        /* No version at all: the oldest blobs, read as they were written. */
        ("v0", format!("{{\"slot\":2,\"rate\":\"1/2\",\"attack\":3.0,\"sustain\":0.25,{pats}}}"),
         &[("rate", "1/2"), ("attack", "3.0"), ("sustain", "0.25")]),
    ];
    for (name, blob, want) in cases {
        let mut p = Instance::new(SR);
        /* A different sound in slot 5 beforehand: a load replaces it too. */
        p.set_num(Param::Slot, 5.0);
        p.set_param("amount", "0.11");
        p.set_param("state", blob);
        assert_eq!(p.slot(), 2, "{name}");
        /* Everything but the length, which is the pattern's own. */
        let sound = |p: &mut Instance, s| -> Vec<String> {
            sound_of(p, s).into_iter().filter(|f| !f.starts_with("length=")).collect()
        };
        let first = sound(&mut p, 0);
        for s in 1..SLOTS {
            assert_eq!(sound(&mut p, s), first, "{name}: slot {s} differs from slot 0");
        }
        for (k, v) in *want {
            assert!(first.contains(&format!("{k}={v}")), "{name}: wanted {k}={v} in {first:?}");
        }
        /* The patterns are still each slot's own. */
        p.set_num(Param::Slot, 1.0);
        assert_eq!(get(&p, "length"), "7", "{name}");
        p.set_num(Param::Slot, 0.0);
        assert_eq!(get(&p, "pattern"), "5555", "{name}");
        /* And saved again it is the new format, still eight alike. */
        let again = get(&p, "state");
        assert!(again.starts_with("{\"sv\":7,"), "{name}: {again}");
        assert!(!again.contains("\"s1\""), "{name}: alike slots are not written: {again}");
    }
}

#[test]
fn a_slots_own_field_wins_over_the_top_level_and_is_clamped() {
    let blob = "{\"sv\":7,\"slot\":0,\"rate\":\"1/16\",\"attack\":1.60,\"decay\":16.00,\"sustain\":1.000,\
                \"release\":16.00,\"hold\":1.000,\"amount\":1.000,\"fade\":1.0000,\"fsoft\":0,\"fdir\":0,\
                \"legato\":0,\"tmode\":0,\"curve\":0,\
                \"s4\":\"1/4:900:5:7:20:0.5:0.25:0.75:1:1:1:1:9\"}";
    let mut p = Instance::new(SR);
    p.set_param("state", blob);
    p.set_num(Param::Slot, 4.0);
    for (k, v) in [
        ("rate", "1/4"), ("attack", "200.0"), ("decay", "5.0"), ("sustain", "1.00"), ("release", "20.0"),
        ("hold", "0.50"), ("amount", "0.25"), ("fade", "0.75"), ("fade_soft", "1"), ("fade_dir", "1"),
        ("legato", "1"), ("time_mode", "1"), ("curve", "0"),
    ] {
        assert_eq!(get(&p, k), v, "{k}");
    }
    p.set_num(Param::Slot, 3.0);
    assert_eq!(get(&p, "amount"), "1.00", "a slot without a field is the top level's");
}

/* ------------------------------------------------------------ the switch, heard */

fn render(p: &mut Instance, blocks: usize, frames: usize, mut edit: impl FnMut(&mut Instance, usize)) -> Vec<f32> {
    let mut out = Vec::with_capacity(blocks * frames);
    let mut beats = 0.0;
    for b in 0..blocks {
        edit(p, b);
        let (mut l, mut r) = (vec![1.0f32; frames], vec![1.0f32; frames]);
        let t = Transport { running: true, beats, bpm: BPM };
        p.process_f32_split(&mut l, &mut r, frames, Some(&t));
        out.extend_from_slice(&l);
        beats += frames as f64 / SR * BPM as f64 / 60.0;
    }
    out
}

fn max_step(v: &[f32]) -> (f32, usize) {
    let mut worst = (0.0f32, 0);
    for i in 1..v.len() {
        let d = (v[i] - v[i - 1]).abs();
        if d > worst.0 {
            worst = (d, i);
        }
    }
    worst
}

/// The same budget as the parameter click tests in `crate::tests`.
const STEP_LIMIT: f32 = 0.01;

/// Slot 2's sound: different in everything that shapes the gain.
const SLOT2: &[(&str, &str)] = &[
    ("amount", "0.2"), ("sustain", "0.9"), ("attack", "60"), ("decay", "30"), ("release", "60"),
    ("hold", "0.4"), ("rate", "1/8"), ("curve", "1"), ("fade", "0.6"), ("fade_soft", "1"),
];

/// Slot 1 a slow gate, slot 2 [`SLOT2`], the same pattern in both.
fn two_slots() -> Instance {
    let mut p = Instance::new(SR);
    for (k, v) in [("pattern", "FFFF"), ("attack", "12"), ("decay", "12"), ("sustain", "0.3"), ("release", "12")] {
        p.set_param(k, v);
    }
    p.set_num(Param::Slot, 1.0);
    p.set_param("pattern", "FFFF");
    for (k, v) in SLOT2 {
        p.set_param(k, v);
    }
    p.set_num(Param::Slot, 0.0);
    p
}

#[test]
fn switching_slots_mid_gate_does_not_step_the_gain() {
    let mut p = two_slots();
    let g = render(&mut p, 400, 64, |p, b| {
        if b > 20 && b % 23 == 0 {
            p.set_num(Param::Slot, ((b / 23) % 2) as f64);
        }
    });
    let (d, at) = max_step(&g);
    assert!(d < STEP_LIMIT, "the gain stepped by {d} at sample {at}");
    /* And the switch was heard: slot 2's Amount of 20% cannot dip below 0.8. */
    let in_slot1: f32 = g[(23 * 5 + 15) * 64..(23 * 6) * 64].iter().copied().fold(1.0, f32::min);
    let in_slot0: f32 = g[(23 * 6 + 15) * 64..(23 * 7) * 64].iter().copied().fold(1.0, f32::min);
    assert!(in_slot1 >= 0.79, "slot 2 gated deeper than its amount allows: {in_slot1}");
    assert!(in_slot0 < 0.5, "slot 1 did not gate as deep as its own sound: {in_slot0}");
}

/*
 * A SWITCH IS EVERY VALUE AUTOMATED AT ONCE -- not a new rule. The Amount and
 * Sustain glides, the Curve's re-anchor, the Rate's re-lock to the host and the
 * envelope carrying on from its level are what each of those values does when
 * a host moves it, so a switch to a slot with the same pattern renders exactly
 * what moving them all on one slot renders.
 */
#[test]
fn a_switch_sounds_exactly_like_automating_every_value_at_once() {
    let mut switched = two_slots();
    let a = render(&mut switched, 120, 64, |p, b| {
        if b == 40 {
            p.set_num(Param::Slot, 1.0);
        }
    });
    let mut automated = two_slots();
    let b = render(&mut automated, 120, 64, |p, blk| {
        if blk == 40 {
            for (k, v) in SLOT2 {
                p.set_param(k, v);
            }
        }
    });
    assert_eq!(a, b);
    let mut stayed = two_slots();
    assert_ne!(a, render(&mut stayed, 120, 64, |_, _| {}), "the switch changed nothing");
}

#[test]
fn a_running_stage_finishes_at_its_own_length_and_the_next_uses_the_new_slot() {
    /*
     * A stage's length is latched when it is entered -- the rule an automated
     * Attack has always followed -- so a switch mid-attack neither cuts the
     * ramp short nor stretches it: the attack in flight keeps its pace, and the
     * decay after it is the new slot's.
     */
    let mut p = Instance::new(SR);
    /* Quarter-note steps and a 250 ms attack: one stage, well inside a step. */
    p.set_param("pattern", "FFFF");
    p.set_param("rate", "1/4");
    p.set_param("attack", "50");
    p.set_num(Param::Slot, 1.0);
    p.set_param("pattern", "FFFF");
    p.set_param("rate", "1/4");
    p.set_param("attack", "10");
    p.set_param("decay", "80");
    p.set_param("sustain", "0.5");
    p.set_num(Param::Slot, 0.0);

    let mut beats = 0.0;
    let mut block = |p: &mut Instance| {
        let (mut l, mut r) = ([1.0f32; 32], [1.0f32; 32]);
        let t = Transport { running: true, beats, bpm: BPM };
        p.process_f32_split(&mut l, &mut r, 32, Some(&t));
        beats += 32.0 / SR * BPM as f64 / 60.0;
    };
    /* Into the first step's attack. */
    while p.env.stage != Stage::Attack {
        block(&mut p);
    }
    let inc = p.env.inc;
    /* 50% of a 500 ms gate at 44.1 kHz. */
    assert!((inc - 1.0 / 11025.0).abs() < 1e-12, "slot 1's attack is not its own: {inc}");
    let level = p.env.level;
    p.set_num(Param::Slot, 1.0);
    assert_eq!((p.env.stage, p.env.inc, p.env.level), (Stage::Attack, inc, level), "the switch touched the stage in flight");
    block(&mut p);
    assert_eq!(p.env.inc, inc, "the attack in flight changed pace");
    while p.env.stage == Stage::Attack {
        block(&mut p);
    }
    assert_eq!(p.env.stage, Stage::Decay);
    let want = 1.0 / p.lens().decay;
    assert!((p.env.inc - want).abs() < 1e-12, "the decay is not slot 2's: {} vs {want}", p.env.inc);
}

#[test]
fn spreading_puts_the_current_sound_in_every_slot_and_leaves_the_patterns() {
    let mut p = Instance::new(SR);
    p.set_num(Param::Slot, 1.0);
    p.set_param("length", "5");
    p.set_num(Param::Slot, 3.0);
    set_all(&mut p, 1);
    let rev = p.state_rev();
    p.spread_sound();
    assert_ne!(p.state_rev(), rev);
    let want: Vec<String> = sound_of(&mut p, 3).into_iter().filter(|f| !f.starts_with("length=")).collect();
    for s in 0..SLOTS {
        let got: Vec<String> = sound_of(&mut p, s).into_iter().filter(|f| !f.starts_with("length=")).collect();
        assert_eq!(got, want, "slot {s}");
    }
    p.set_num(Param::Slot, 1.0);
    assert_eq!(get(&p, "length"), "5");
    let rev = p.state_rev();
    p.spread_sound();
    assert_eq!(p.state_rev(), rev, "spreading what is already everywhere changes nothing");
}

#[test]
fn the_numbers_are_what_the_numeric_door_takes() {
    let mut p = Instance::new(SR);
    p.set_num(Param::Slot, 2.0);
    set_all(&mut p, 0);
    let n = p.numbers();
    let mut q = Instance::new(SR);
    for (i, v) in n.iter().enumerate() {
        q.set_num(Param::from_i32(i as i32).unwrap(), *v);
    }
    assert_eq!(get(&q, "params"), get(&p, "params"));
    assert_eq!(n[Param::Slot as usize], 2.0);
    assert_eq!(n[Param::Length as usize], 7.0);
}

#[test]
fn the_heaviest_patch_with_eight_different_sounds_still_fits_the_state_budget() {
    /* TG_STATE_MAX, Schwung's audio-FX cap too: 128 accented, shuffled steps in
     * every slot, and every slot's sound different from every other's. */
    let mut p = Instance::new(SR);
    for s in 0..SLOTS {
        p.set_num(Param::Slot, s as f64);
        p.set_param("length", "127");
        p.set_param("randomize", &(9000 + s).to_string());
        for i in 0..128 {
            p.set_param("cursor", &i.to_string());
            p.set_param("step_amount", "0.5");
        }
        for row in TABLE.iter().filter(|r| r.0 != "length") {
            p.set_param(row.0, if s % 2 == 0 { row.1 } else { row.2 });
        }
        p.set_param("attack", &format!("{}.25", 100 + s));
        p.set_param("release", &format!("{}.75", 150 + s));
    }
    let blob = get(&p, "state");
    assert!(blob.len() < 8192, "{} bytes", blob.len());
    let mut q = Instance::new(SR);
    q.set_param("state", &blob);
    assert_eq!(get(&q, "state"), blob);
}

