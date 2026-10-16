// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
`chain_params` -- how Schwung's knob grid draws NI Vocalize.

THE STRIPPED-DOWN MODULE. Seven knobs, one page: how far the sound has become
the vowel (Vocal), which vowel (Vowel), the voice (Size, Resonance), what a
pure source is given (Excite), and the level (Mix, Output). The keys are the
plugin's, so a value means the same on both.

NO MODE. The module is built without the Spectral engine (vz-move's
Cargo.toml): the Time engine has no latency, which on a device that plays
live is the one that fits, and the module carries no FFT. A saved Mode is
read and changes nothing.

THE VOWEL IS A NUMBER HERE: 0 A, 1 E, 2 I, 3 O, 4 U, and the way between --
a float knob, so a turn sweeps through the vowels as the plugin's does.

WRITTEN BY build.rs FROM THE ENGINE'S TABLE (vz_core::Param::info, through
ni_params::chain), so every range, default and unit is the engine's; build.rs
says only the order and each knob's step. THE TESTS BELOW still hold the
result to the engine: every key is one the engine answers to, every range
and default the engine's own.
*/

pub const CHAIN_PARAMS: &str = include_str!(concat!(env!("OUT_DIR"), "/chain_params.json"));

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::{Map, Value};
    use vz_core::{Kind, Param};

    fn declared() -> Vec<Map<String, Value>> {
        let parsed: Value = serde_json::from_str(CHAIN_PARAMS).expect("chain_params is not JSON");
        let Value::Array(entries) = parsed else { panic!("chain_params is not a JSON array") };
        entries
            .into_iter()
            .map(|e| match e {
                Value::Object(fields) => fields,
                other => panic!("an entry is not an object: {other}"),
            })
            .collect()
    }

    fn text<'a>(e: &'a Map<String, Value>, field: &str) -> &'a str {
        e.get(field).and_then(Value::as_str).unwrap_or("")
    }

    #[test]
    fn every_declared_key_is_the_engines_and_fits_a_page() {
        let d = declared();
        assert_eq!(d.len(), 7, "one page");
        for e in &d {
            let key = text(e, "key");
            assert!(Param::from_key(key).is_some(), "\"{key}\" is not a parameter");
            assert_ne!(key, "mode", "the module has no Spectral engine to switch to");
        }
        let mut keys: Vec<_> = d.iter().map(|e| text(e, "key")).collect();
        keys.sort_unstable();
        keys.dedup();
        assert_eq!(keys.len(), 7);
    }

    #[test]
    fn every_float_is_the_engines_range_default_and_unit() {
        for e in declared() {
            assert_eq!(text(&e, "type"), "float");
            let p = Param::from_key(text(&e, "key")).unwrap();
            let (Kind::Float { min, max, default, .. }, Some(unit)) = (p.info().kind, p.info().unit) else {
                panic!("{p:?} is a float here and not in the engine");
            };
            let num = |f: &str| e[f].as_f64().unwrap_or_else(|| panic!("{p:?} has no {f}"));
            assert_eq!((num("min"), num("max"), num("default")), (min, max, default), "{p:?}");
            assert_eq!(text(&e, "unit"), unit.label(), "{p:?}");
            assert!(num("step") > 0.0);
        }
    }

    #[test]
    fn it_fits_the_buffer_the_host_offers() {
        /* chain_host.c reads chain_params into a buffer of this size. */
        assert!(CHAIN_PARAMS.len() < 4096, "{} bytes", CHAIN_PARAMS.len());
    }
}
