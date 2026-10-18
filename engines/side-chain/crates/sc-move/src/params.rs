// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
`chain_params` -- how Schwung's knob grid draws NI Side-Chain.

WRITTEN BY build.rs FROM THE ENGINE'S SCHEMA (sc_core::schema, through
ni_params::chain), so every range, default and option is the engine's own;
build.rs says only how the grid draws each knob (its name, short name, step,
unit and envelope grouping) and declares the Move-only controls. The Rust
tests in `sc-move/src/lib.rs` hold the result to the module that serves it:
every enum the engine reads back by index declares that wire format and a
default inside its options, and every option round-trips through the vtable
the device calls.

THE FOUR ENVELOPE KEYS ARE CONTIGUOUS ON PURPOSE. A viz group whose members are
not adjacent is reported by the host's detector as `viz-declared-not-adjacent`,
and the graphic is dropped.

DELAY GOES NEGATIVE HERE TOO. On the Cycle source it is a phase in a periodic
cycle rather than a wait, so -20% is 80% -- an early sidechain, without
anticipating anything. On MIDI the engine clamps it to no wait, and the range
still travels the whole way because the knob is one control.

SIDECHAIN IS NOT AN OPTION HERE. The chain host hands over one interleaved
buffer and the API has no aux input anywhere, so the Move build offers Cycle and
MIDI. Listing a third source that could never fire would be worse than listing
two: a control that does nothing reads as a broken module rather than as a
platform that lacks the bus.

THRESHOLD AND LOCKOUT ARE ABSENT FOR THE SAME REASON -- they are the sidechain
detector's, and the detector has nothing to listen to.

EVERY ENUM IS WIRED BY INDEX, AND SAYS SO. The engine reads each one back as a
number, and the host learns an undeclared enum's convention only from a read --
until then it writes what it holds, which on a Delete-to-default is the
declared `default` verbatim. `trigger_note` used to declare `"C1"` there with no
`wire_format`, so a reset sent a label the engine parsed as 0 and the trigger
moved to C-2. Declared, every write path and the read agree; the engine still
accepts the label, for a patch written by hand.

THE EDITOR'S FEED IS THE LAST ENTRY, `web_ui` (ni_schwung::WEB_UI_KEY). Its
`extra_keys` are what the module's page in Schwung's web manager is drawn from,
pushed live because none of them is a declared param: `ui` (the trigger, the
sweep and the stage), `params` (the engine's values in their own units) and
`stage_ms` (each stage's length in ms, for the hint). `ui` is not declared on
its own for that reason; the shape page still lists it as an extra.
*/

pub const CHAIN_PARAMS: &str = include_str!(concat!(env!("OUT_DIR"), "/chain_params.json"));


#[cfg(test)]
mod tests {
    use super::CHAIN_PARAMS;
    use sc_core::params::Param;
    use sc_core::Instance;
    use serde_json::{Map, Value};

    /*
     * WHAT THESE PIN, AND WHY IT IS NOT A JSON-SHAPE TEST.
     *
     * The Trance Gate's equivalent compares this constant against the output of
     * the C shell it was captured from. There was never a C shell here -- this
     * string was written -- so comparing it to itself would prove nothing.
     *
     * What CAN drift is the relationship between this string and the engine: a
     * parameter renamed in params.rs, a key misspelled here, a viz group whose
     * members stop being adjacent. Each of those produces a control that is
     * silently absent or inert on the device, which is the hardest place in this
     * project to notice anything.
     *
     * Read with serde_json, the parser tg-core already links. A string that is
     * not JSON fails as a parse error rather than as a count of braces, and an
     * entry's fields are found wherever they sit in it, not only after a `key`
     * that happens to come first.
     */

    /// The declaration, one object per control, in order. A string that is
    /// not a JSON array of objects fails whichever test asked.
    fn declared() -> Vec<Map<String, Value>> {
        let parsed: Value = serde_json::from_str(CHAIN_PARAMS).expect("chain_params is not JSON");
        let Value::Array(entries) = parsed else { panic!("chain_params is not a JSON array") };
        entries
            .into_iter()
            .map(|entry| match entry {
                Value::Object(fields) => fields,
                other => panic!("a chain_params entry is not an object: {other}"),
            })
            .collect()
    }

    /// A text field of an entry, or "" for one it does not have.
    fn text<'a>(entry: &'a Map<String, Value>, field: &str) -> &'a str {
        entry.get(field).and_then(Value::as_str).unwrap_or("")
    }

    /// Every `key` in declaration order.
    fn keys() -> Vec<String> {
        entries().into_iter().map(|(k, _)| k).collect()
    }

    /// Each entry as (key, type).
    fn entries() -> Vec<(String, String)> {
        declared()
            .iter()
            .map(|e| (text(e, "key").to_string(), text(e, "type").to_string()))
            .collect()
    }

    /// The `viz` group named on each entry, in the same order as `keys()`.
    fn viz_groups() -> Vec<Option<String>> {
        declared()
            .iter()
            .map(|e| e.get("viz").and_then(|viz| viz.get("group")).and_then(Value::as_str).map(str::to_string))
            .collect()
    }

    #[test]
    fn the_declaration_is_a_json_array_of_keyed_entries() {
        let declared = declared();
        for entry in &declared {
            assert!(!text(entry, "key").is_empty(), "an entry has no key: {entry:?}");
        }
        assert!(declared.len() >= 14, "only {} keys", declared.len());
    }

    #[test]
    fn every_declared_key_is_one_the_engine_answers_to() {
        /*
         * A KEY THE ENGINE DOES NOT KNOW IS A DEAD CONTROL ON THE DEVICE. The
         * host draws it from this string, the user turns it, set_param returns
         * false, and nothing happens -- with no error anywhere.
         */
        let mut inst = Instance::new(44100.0);
        let mut buf = [0u8; 4096];
        for (k, ty) in entries() {
            /*
             * A `canvas` ENTRY IS A DECLARATION, NOT A VALUE.
             *
             * It tells the host "there is a page behind this cell"; nothing
             * reads it and `show_value` is false. The engine knowing nothing
             * about it is correct, and this test found that out by failing on
             * `shape` -- which is the useful kind of failure, because the rule
             * is now written down rather than assumed.
             */
            if ty == "canvas" {
                continue;
            }
            /* THE EDITOR'S FEED IS THE SHELL'S, not the engine's: ni_schwung
             * answers it for every module (tested there), so the engine is
             * right not to know it. */
            if k == ni_schwung::WEB_UI_KEY {
                continue;
            }
            /* Read-only entries are served by get_param, the rest by set_param.
             * One of the two must own every remaining key. */
            let readable = inst.get_param(&k, &mut buf) >= 0;
            let writable = inst.set_param(&k, "0");
            assert!(
                readable || writable,
                "chain_params declares \"{k}\", which the engine neither reads nor writes"
            );
        }

        /* And the exemption above is not a blanket one: exactly the entries
         * declared as canvases may skip it. */
        let canvases: Vec<String> = entries()
            .into_iter()
            .filter(|(_, t)| t == "canvas")
            .map(|(k, _)| k)
            .collect();
        assert_eq!(canvases, vec!["shape".to_string()]);
    }

    /*
     * THE EDITOR'S FEED: declared last, read-only, and naming only keys that
     * are NOT declared themselves -- the manager pushes a declared key once and
     * never again, so a declared feed would freeze the page -- each of which
     * the engine answers.
     */
    #[test]
    fn the_web_editor_feed_is_last_and_every_key_it_names_is_live() {
        let declared = declared();
        let last = declared.last().expect("empty declaration");
        assert_eq!(last.get("key").and_then(Value::as_str), Some(ni_schwung::WEB_UI_KEY));
        assert_eq!(last.get("access").and_then(Value::as_str), Some("read"));
        let feeds: Vec<String> = last["extra_keys"]
            .as_array()
            .expect("web_ui names its feeds")
            .iter()
            .map(|v| v.as_str().unwrap().to_string())
            .collect();
        assert!(!feeds.is_empty() && feeds.len() <= 4, "the host takes at most four per declaration");
        let inst = Instance::new(44100.0);
        let mut buf = [0u8; 4096];
        for f in &feeds {
            assert!(!keys().contains(f), "{f} is declared, so the page would never see it move");
            assert!(inst.get_param(f, &mut buf) >= 0, "the engine does not answer {f}");
        }
    }

    #[test]
    fn every_writable_parameter_the_engine_has_is_declared_or_deliberately_not() {
        /*
         * THE OTHER DIRECTION: a parameter added to the engine and forgotten
         * here is a control that never appears on the device.
         *
         * Two are absent ON PURPOSE, and naming them here is what makes that a
         * decision rather than an oversight -- the Move has no aux input, so the
         * sidechain detector they tune cannot do anything. (The Sidechain
         * SOURCE is an option of `source`, not a key; the test below keeps it
         * out.)
         *
         * Param::ALL, not a walk of the indices that skips one it cannot
         * convert: every parameter the engine has, none passed over.
         */
        const DELIBERATELY_ABSENT: [&str; 2] = ["threshold", "lockout"];
        let declared = keys();
        for p in Param::ALL {
            let k = p.key();
            if DELIBERATELY_ABSENT.contains(&k) {
                assert!(
                    !declared.iter().any(|d| d == k),
                    "\"{k}\" is listed as deliberately absent but IS declared"
                );
                continue;
            }
            assert!(
                declared.iter().any(|d| d == k),
                "the engine has \"{k}\" and chain_params does not declare it"
            );
        }
    }

    #[test]
    fn a_viz_group_is_contiguous() {
        /*
         * The host's detector reports a group whose members are not adjacent as
         * `viz-declared-not-adjacent` and DROPS THE GRAPHIC. The envelope keys
         * are next to each other for that reason and no other, so a tidy-up that
         * reorders them would silently cost the shape display.
         */
        let groups = viz_groups();
        let mut seen: Vec<String> = Vec::new();
        let mut current: Option<String> = None;
        for g in groups.iter() {
            if g == &current {
                continue;
            }
            if let Some(name) = g {
                assert!(
                    !seen.contains(name),
                    "viz group \"{name}\" is split -- its members must be adjacent"
                );
                seen.push(name.clone());
            }
            current = g.clone();
        }
        assert!(seen.contains(&"duck".to_string()), "the duck group is missing");
    }

    #[test]
    fn the_sidechain_source_is_not_offered_where_it_cannot_work() {
        /* Listing a source that can never fire is worse than listing two. */
        assert!(
            !CHAIN_PARAMS.contains("Sidechain"),
            "the Move has no aux input; the Sidechain source must not be offered"
        );
        assert!(CHAIN_PARAMS.contains("\"Cycle\""));
        assert!(CHAIN_PARAMS.contains("\"MIDI\""));
    }

    #[test]
    fn it_fits_the_buffer_the_host_offers() {
        /* v2_get_param refuses rather than truncating, so an over-long
         * declaration is a module whose controls simply do not appear. */
        assert!(
            CHAIN_PARAMS.len() < 4096,
            "chain_params is {} bytes; SC_STATE_MAX is 4096",
            CHAIN_PARAMS.len()
        );
    }
}
