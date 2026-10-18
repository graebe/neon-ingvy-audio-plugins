// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
`chain_params` -- how Schwung's knob grid draws NI Filter Designer.

THE STRIPPED-DOWN MODULE: the isolator a DJ reaches for, and two cuts to
frame it. Two pages of eight knobs, the plugin's keys, so a value means the
same on both:

```text
  page 1   Low  Mid  High  Low-Mid  Mid-High  Slope  Isolator  Output
  page 2   HP On  HP Freq  HP Slope  HP Q   LP On  LP Freq  LP Slope  LP Q
```

Node 1 is a high-pass and node 2 a low-pass, both Butterworth and switched
off until wanted (lib.rs sets them up); the isolator starts on. The other
fourteen nodes, the families, ripple, stopband, channels and Scale stay at
their defaults: a 16-bit device without a screen has no curve to design on.

THE FREQUENCIES ARE ENUMS, AND SEND THEIR LABELS. A Schwung float turns
linearly, which would spend a knob's whole travel above a few kHz. So each
frequency knob offers twelve steps and declares `wire_format: "name"`: the
host writes "250" or "2.5k", which the engine reads in Hz (`Param::
parse_value`), and the module reads the value back in the same words
(lib.rs). Orders, Slope and the switches are index enums with the engine's
own labels.

WRITTEN BY build.rs FROM THE ENGINE'S TABLE (fd_core::Param::info, through
ni_params::chain), so every range, default, label and unit is the engine's
unless build.rs says otherwise, where it shows: the grid's names, the two Q
ranges narrowed, the frequency steps (steps.rs), and the defaults the module
sets the engine to (setup.rs). THE TESTS BELOW still hold the result to the
engine: every key one the engine answers to, every float's range inside the
engine's own, every option one the engine reads.
*/

pub const CHAIN_PARAMS: &str = include_str!(concat!(env!("OUT_DIR"), "/chain_params.json"));

pub use steps::FREQUENCY_OPTIONS;

#[path = "steps.rs"]
mod steps;

#[cfg(test)]
mod tests {
    use super::*;
    use fd_core::{Kind, Param};
    use serde_json::{Map, Value};

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
    fn two_pages_of_the_engines_keys() {
        let d = declared();
        assert_eq!(d.len(), 16, "two pages of eight");
        let mut keys: Vec<_> = d.iter().map(|e| text(e, "key")).collect();
        for k in &keys {
            assert!(Param::from_key(k).is_some(), "\"{k}\" is not a parameter");
        }
        keys.sort_unstable();
        keys.dedup();
        assert_eq!(keys.len(), 16);
    }

    #[test]
    fn every_float_is_inside_the_engines_range() {
        for e in declared().iter().filter(|e| text(e, "type") == "float") {
            let p = Param::from_key(text(e, "key")).unwrap();
            let Kind::Float { min, max, .. } = p.info().kind else { panic!("{p:?} is a float here only") };
            let num = |f: &str| e[f].as_f64().unwrap();
            assert!(num("min") >= min && num("max") <= max, "{p:?}");
            assert!(num("default") >= num("min") && num("default") <= num("max"), "{p:?}");
            assert!(num("step") > 0.0);
        }
    }

    #[test]
    fn every_option_is_one_the_engine_reads() {
        for e in declared().iter().filter(|e| text(e, "type") == "enum") {
            let p = Param::from_key(text(e, "key")).unwrap();
            let options: Vec<&str> = e["options"].as_array().unwrap().iter().map(|o| o.as_str().unwrap()).collect();
            match text(e, "wire_format") {
                "name" => {
                    for o in &options {
                        assert!(p.parse_value(o).is_some(), "{p:?} cannot read {o}");
                    }
                    let (_, labels) = FREQUENCY_OPTIONS.iter().find(|(k, _)| *k == text(e, "key")).unwrap();
                    assert_eq!(&options[..], &labels[..]);
                    assert!(options.contains(&text(e, "default")));
                }
                "index" => {
                    let Kind::Choice { labels, .. } = p.info().kind else { panic!("{p:?} is no choice") };
                    assert_eq!(&options[..], labels, "{p:?}: the engine's own labels");
                }
                other => panic!("{p:?}: wire format {other}"),
            }
        }
    }

    #[test]
    fn it_fits_the_buffer_the_host_offers() {
        assert!(CHAIN_PARAMS.len() < 4096, "{} bytes", CHAIN_PARAMS.len());
    }
}
