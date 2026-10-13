// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
`chain_params` -- how Schwung's knob grid draws NI Bit-Crusher.

THE STRIPPED-DOWN MODULE. Two pages of the 2x4 grid. The first, eight knobs:
the crusher's two axes (Bits and Rate), what shapes them (Drive, Shape,
Jitter, Dither) and the level (Mix, Output). The second, the Filter -- on or
off (off by default, as in the plugin), Cutoff, Peak and Slope -- appended
after the eight, so nothing on the first page moved. The switches -- Mid-Rise,
Anti-Alias, Pre and Post -- are not offered: on a 16-bit device with no editor
they stay at their defaults, off, which is the classic crusher. The keys are the plugin's, so a
value means the same on both.

CUTOFF TOO, for the same reason: twelve frequencies from 100 Hz to 20 kHz,
sent and read back as their labels ("1.2k"). Slope sends its word ("24 dB"),
which is what the engine reads a choice by.

RATE IS AN ENUM HERE, AND SENDS ITS LABEL. The plugin's Rate turns in equal
ratios over 50 Hz to 96 kHz; a Schwung float turns linearly, which would put
most of a knob's travel above 10 kHz. So the Move offers twelve rates, and
declares `wire_format: "name"`: the host writes "12k", which the engine reads
as 12000 Hz (`Param::parse_value`), and the module reads the value back in the
same words (lib.rs), so the knob grid shows what it wrote. 48k is the Move's
own rate and above: no hold at all.

THE TESTS BELOW hold this string to the engine: every key is one the engine
answers to, every range and default inside the engine's own, and every Rate
option is a rate the engine reads.
*/

pub const CHAIN_PARAMS: &str = r##"[{"key":"bits","name":"Bits","type":"float","min":1,"max":16,"default":8,"step":0.1,"unit":"bits"},{"key":"rate","name":"Rate","type":"enum","options":["250","500","1k","2k","4k","6k","8k","12k","16k","24k","32k","48k"],"wire_format":"name","default":"12k"},{"key":"drive","name":"Drive","type":"float","min":-24,"max":24,"default":0,"step":0.5,"unit":"dB"},{"key":"shape","name":"Shape","type":"float","min":0,"max":100,"default":0,"step":1,"unit":"%"},{"key":"jitter","name":"Jitter","type":"float","min":0,"max":100,"default":0,"step":1,"unit":"%"},{"key":"dither","name":"Dither","type":"float","min":0,"max":100,"default":0,"step":1,"unit":"%"},{"key":"mix","name":"Mix","type":"float","min":0,"max":100,"default":100,"step":1,"unit":"%"},{"key":"output","name":"Output","type":"float","min":-24,"max":24,"default":0,"step":0.5,"unit":"dB"},{"key":"filter","name":"Filter","type":"enum","options":["Off","On"],"wire_format":"index","default":"0"},{"key":"cutoff","name":"Cutoff","type":"enum","options":["100","200","300","500","800","1.2k","2k","3k","5k","8k","12k","20k"],"wire_format":"name","default":"2k"},{"key":"peak","name":"Peak","type":"float","min":0,"max":24,"default":0,"step":0.5,"unit":"dB"},{"key":"slope","name":"Slope","type":"enum","options":["12 dB","24 dB","48 dB"],"wire_format":"name","default":"24 dB"}]"##;

/// The Cutoff options' labels, in order -- what `chain_params` declares.
pub const CUTOFF_LABELS: [&str; 12] = ["100", "200", "300", "500", "800", "1.2k", "2k", "3k", "5k", "8k", "12k", "20k"];

/// The Rate options' labels, in order -- what `chain_params` declares.
pub const RATE_LABELS: [&str; 12] = ["250", "500", "1k", "2k", "4k", "6k", "8k", "12k", "16k", "24k", "32k", "48k"];

#[cfg(test)]
mod tests {
    use super::*;
    use bc_core::{Kind, Param};
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
    fn the_first_page_is_the_crusher_and_the_second_the_filter() {
        let keys: Vec<String> = declared().iter().map(|e| text(e, "key").to_string()).collect();
        assert_eq!(&keys[..8], ["bits", "rate", "drive", "shape", "jitter", "dither", "mix", "output"]);
        assert_eq!(&keys[8..], ["filter", "cutoff", "peak", "slope"]);
    }

    #[test]
    fn every_cutoff_and_slope_option_is_one_the_engine_reads() {
        let d = declared();
        let cutoff = d.iter().find(|e| text(e, "key") == "cutoff").unwrap();
        let options: Vec<&str> = cutoff["options"].as_array().unwrap().iter().map(|o| o.as_str().unwrap()).collect();
        assert_eq!(options, CUTOFF_LABELS);
        let mut last = 0.0;
        for o in &options {
            let hz = Param::Cutoff.parse_value(o).expect("a cutoff");
            assert!(hz > last, "{o} out of order");
            last = hz;
        }
        assert_eq!(Param::Cutoff.parse_value(text(cutoff, "default")), Some(Param::Cutoff.default_value()));
        let slope = d.iter().find(|e| text(e, "key") == "slope").unwrap();
        assert_eq!(text(slope, "wire_format"), "name");
        for (i, o) in slope["options"].as_array().unwrap().iter().enumerate() {
            assert_eq!(Param::Slope.parse_value(o.as_str().unwrap()), Some(i as f64));
        }
        assert_eq!(Param::Slope.parse_value(text(slope, "default")), Some(Param::Slope.default_value()));
        let filter = d.iter().find(|e| text(e, "key") == "filter").unwrap();
        assert_eq!(text(filter, "default"), "0", "off by default, as in the plugin");
    }

    #[test]
    fn every_declared_key_is_the_engines_and_fits_two_pages() {
        let d = declared();
        assert_eq!(d.len(), 12, "two pages: eight knobs, then the filter's four");
        for e in &d {
            let key = text(e, "key");
            assert!(Param::from_key(key).is_some(), "\"{key}\" is not a parameter");
        }
        let mut keys: Vec<_> = d.iter().map(|e| text(e, "key")).collect();
        keys.sort_unstable();
        keys.dedup();
        assert_eq!(keys.len(), 12);
    }

    #[test]
    fn every_float_is_the_engines_range_and_default() {
        for e in declared().iter().filter(|e| text(e, "type") == "float") {
            let p = Param::from_key(text(e, "key")).unwrap();
            let Kind::Float { min, max, default, unit, .. } = p.info().kind else {
                panic!("{p:?} is a float here and not in the engine");
            };
            let num = |f: &str| e[f].as_f64().unwrap_or_else(|| panic!("{p:?} has no {f}"));
            assert_eq!((num("min"), num("max"), num("default")), (min, max, default), "{p:?}");
            assert_eq!(text(e, "unit"), unit.label(), "{p:?}");
            assert!(num("step") > 0.0);
        }
    }

    #[test]
    fn every_rate_option_is_a_rate_the_engine_reads_and_the_default_is_its_own() {
        let d = declared();
        let rate = d.iter().find(|e| text(e, "key") == "rate").unwrap();
        assert_eq!(text(rate, "wire_format"), "name");
        let options: Vec<&str> = rate["options"].as_array().unwrap().iter().map(|o| o.as_str().unwrap()).collect();
        assert_eq!(options, RATE_LABELS);
        let mut last = 0.0;
        for o in &options {
            let hz = Param::Rate.parse_value(o).expect("a rate");
            assert!(hz > last, "{o} out of order");
            last = hz;
        }
        assert_eq!(Param::Rate.parse_value(text(rate, "default")), Some(Param::Rate.default_value()));
    }

    #[test]
    fn it_fits_the_buffer_the_host_offers() {
        /* chain_host.c reads chain_params into a buffer of this size. */
        assert!(CHAIN_PARAMS.len() < 4096, "{} bytes", CHAIN_PARAMS.len());
    }
}
