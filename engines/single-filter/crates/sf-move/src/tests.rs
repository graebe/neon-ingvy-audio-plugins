// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * THE MOVE'S PARAMETER PATH, END TO END, for every product: the entry points
 * the chain host calls (ni_schwung::init's table), with the values it
 * actually sends -- a declared default on a Delete-to-default, a step's label
 * on a turn of a stepped knob -- and the panic through on_midi; and each
 * declaration held to the engine's table.
 */

use super::*;
use ni_schwung::AudioFxApiV2;
use serde_json::{Map, Value};
use sf_core::params::Kind;
use std::ffi::{c_char, c_void, CString};

fn api<P: Product>() -> &'static AudioFxApiV2 {
    /* SAFETY: init returns its module type's table, a constant that lives as
     * long as the process. */
    unsafe { &*ni_schwung::init::<SingleFilter<P>>(std::ptr::null()) }
}

struct Instance<P: Product>(*mut c_void, PhantomData<P>);

impl<P: Product> Instance<P> {
    fn new() -> Self {
        Instance((api::<P>().create_instance.unwrap())(std::ptr::null(), std::ptr::null()), PhantomData)
    }
    fn set(&self, k: &str, v: &str) {
        let (k, v) = (CString::new(k).unwrap(), CString::new(v).unwrap());
        (api::<P>().set_param.unwrap())(self.0, k.as_ptr(), v.as_ptr());
    }
    fn get(&self, k: &str) -> Option<String> {
        let k = CString::new(k).unwrap();
        let mut buf = [0 as c_char; 4096];
        let n = (api::<P>().get_param.unwrap())(self.0, k.as_ptr(), buf.as_mut_ptr(), buf.len() as c_int);
        (n >= 0).then(|| String::from_utf8(buf[..n as usize].iter().map(|c| *c as u8).collect()).unwrap())
    }
    fn process(&self, buf: &mut [i16; 256]) {
        (api::<P>().process_block.unwrap())(self.0, buf.as_mut_ptr(), 128);
    }
    fn midi(&self, msg: &[u8]) {
        /* SAFETY: a live instance and a message of its length. */
        unsafe { ni_schwung::on_midi::<SingleFilter<P>>(self.0, msg.as_ptr(), msg.len() as c_int, 0) }
    }
}

impl<P: Product> Drop for Instance<P> {
    fn drop(&mut self) {
        (api::<P>().destroy_instance.unwrap())(self.0);
    }
}

fn declared<P: Product>() -> Vec<Map<String, Value>> {
    let parsed: Value = serde_json::from_str(P::CHAIN_PARAMS).expect("chain_params is not JSON");
    parsed.as_array().unwrap().iter().map(|e| e.as_object().unwrap().clone()).collect()
}

fn text<'a>(e: &'a Map<String, Value>, field: &str) -> &'a str {
    e.get(field).and_then(Value::as_str).unwrap_or("")
}

/* ---------------------------------------------------- the declaration -- */

fn the_declaration_is_the_engines<P: Product>() {
    let d = declared::<P>();
    let keys: Vec<&str> = d.iter().map(|e| text(e, "key")).collect();
    let want: Vec<&str> =
        params::layout(P::SHAPE).iter().filter(|&&f| f != Field::Analyzer).map(|&f| params::info(P::SHAPE, f).key).collect();
    let mut sorted = keys.clone();
    sorted.sort_unstable();
    let mut wanted = want.clone();
    wanted.sort_unstable();
    assert_eq!(sorted, wanted, "{:?}: every parameter but the Analyzer, once", P::SHAPE);
    assert_eq!(keys[0], "freq");
    assert!(keys.iter().position(|&k| k == "output").unwrap() < 8, "{:?}: Output on the first page", P::SHAPE);
    assert!(P::CHAIN_PARAMS.len() < 4096, "{} bytes", P::CHAIN_PARAMS.len());
    for e in &d {
        let field = params::from_key(P::SHAPE, text(e, "key")).unwrap();
        let info = params::info(P::SHAPE, field);
        match text(e, "type") {
            "float" => {
                let Kind::Float { min, max, .. } = info.kind else { panic!("{field:?} is a float here only") };
                let num = |f: &str| e[f].as_f64().unwrap();
                assert!(num("min") >= min && num("max") <= max, "{field:?}");
                assert!(num("default") >= num("min") && num("default") <= num("max"));
                assert!(num("step") > 0.0);
            }
            "enum" => {
                let options: Vec<&str> = e["options"].as_array().unwrap().iter().map(|o| o.as_str().unwrap()).collect();
                match text(e, "wire_format") {
                    "name" => {
                        assert_eq!(&options[..], steps_of(P::SHAPE, field).unwrap(), "{field:?}");
                        for o in &options {
                            assert!(params::parse_value(P::SHAPE, field, o).is_some(), "{field:?} cannot read {o}");
                        }
                        assert!(options.contains(&text(e, "default")), "{field:?}: the default is a step");
                    }
                    "index" => {
                        let Kind::Choice { labels, .. } = info.kind else { panic!("{field:?} is no choice") };
                        assert_eq!(&options[..], labels, "{field:?}: the engine's own labels");
                    }
                    other => panic!("{field:?}: wire format {other}"),
                }
            }
            other => panic!("{field:?}: type {other}"),
        }
    }
}

/* ------------------------------------------------------- the defaults -- */

fn every_default_is_where_the_module_starts_and_reads_back<P: Product>() {
    let fresh = Instance::<P>::new();
    let m = Instance::<P>::new();
    assert_eq!(m.get("chain_params").as_deref(), Some(P::CHAIN_PARAMS));
    assert_eq!(m.get("no_such_key"), None);
    for e in declared::<P>() {
        let key = text(&e, "key");
        let default = match &e["default"] {
            Value::String(s) => s.clone(),
            v => v.to_string(),
        };
        let start = fresh.get(key).unwrap_or_else(|| panic!("{key} not served"));
        m.set(key, &default);
        let got = m.get(key).unwrap();
        if text(&e, "wire_format") == "name" {
            assert_eq!(got, default, "{key} reads back its label");
            assert_eq!(start, default, "{key} starts at its declared default");
        } else {
            assert_eq!(got.parse::<f64>().unwrap(), default.parse::<f64>().unwrap(), "{key}");
            assert_eq!(start.parse::<f64>().unwrap(), default.parse::<f64>().unwrap(), "{key} starts there");
        }
    }
    /* A step sent is read back in its words. */
    m.set("freq", "2.5k");
    assert_eq!(m.get("freq").as_deref(), Some("2.5k"));
    m.set("freq", "1234");
    assert_eq!(m.get("freq").as_deref(), Some("1234"), "a value between steps reads back as a number");
}

/* --------------------------------------------------------- the panic -- */

fn a_host_panic_silences_the_next_block<P: Product>() {
    for cc in [120u8, 123] {
        let m = Instance::<P>::new();
        m.set("gain", "12");
        m.set("q", "8");
        m.set("order", "7");
        let mut loud = [0i16; 256];
        for (i, s) in loud.iter_mut().enumerate() {
            *s = if i % 7 < 3 { 20_000 } else { -20_000 };
        }
        for _ in 0..40 {
            let mut b = loud;
            m.process(&mut b);
        }
        m.midi(&[0xB0, cc, 0]);
        let mut quiet = [0i16; 256];
        m.process(&mut quiet);
        assert!(quiet.iter().all(|&s| s == 0), "{:?}, CC {cc}: the filter still rang", P::SHAPE);
    }
}

macro_rules! every_product {
    ($($test:ident),*) => {$(
        #[test]
        fn $test() {
            super::tests::$test::<HighPass>();
            super::tests::$test::<LowPass>();
            super::tests::$test::<BandPass>();
            super::tests::$test::<Notch>();
            super::tests::$test::<Bell>();
            super::tests::$test::<LowShelf>();
            super::tests::$test::<HighShelf>();
            super::tests::$test::<Tilt>();
            super::tests::$test::<AllPass>();
        }
    )*};
}

mod each {
    use super::super::*;
    every_product!(the_declaration_is_the_engines, every_default_is_where_the_module_starts_and_reads_back, a_host_panic_silences_the_next_block);
}

#[test]
fn a_cut_on_the_move_cuts() {
    let m = Instance::<HighPass>::new();
    m.set("freq", "1k");
    m.set("order", "3");
    let level = |m: &Instance<HighPass>| {
        let mut sum = 0.0f64;
        for k in 0..200 {
            let mut b = [0i16; 256];
            for (i, s) in b.iter_mut().enumerate() {
                let t = (k * 128 + i / 2) as f64;
                *s = (10_000.0 * (2.0 * std::f64::consts::PI * 100.0 * t / 44_100.0).sin()) as i16;
            }
            m.process(&mut b);
            if k >= 150 {
                sum += b.iter().map(|s| (*s as f64).powi(2)).sum::<f64>();
            }
        }
        sum
    };
    let cut = level(&m);
    m.set("freq", "20");
    assert!(cut < level(&m) * 1e-4, "a decade under a 24 dB/oct cut is 80 dB down");
}
