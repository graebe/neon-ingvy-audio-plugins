/*!
`chain_params` -- how Schwung's knob grid draws NI Side-Chain.

ONE STRING LITERAL, AND IT IS A CAPTURE RATHER THAN A DECLARATION. The shape is
the C shell's own output, and the Rust tests in `sc-move/src/lib.rs` hold it to
the module that serves it: every enum the engine reads back by index declares
that wire format and a default inside its options, and every option round-trips
through the vtable the device calls.

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
*/

pub const CHAIN_PARAMS: &str = r##"[{"key":"source","name":"Source","type":"enum","options":["Cycle","MIDI"],"wire_format":"index","default":"0"},{"key":"rate","name":"Rate","type":"enum","options":["1/1","1/1T","1/2","1/2T","1/4","1/4T","1/8","1/8T","1/16","1/16T","1/32","1/32T"],"wire_format":"index","default":"4"},{"key":"depth","name":"Depth","type":"float","min":0,"max":1,"default":1,"step":0.01,"unit":"%"},{"key":"delay","name":"Delay","short_name":"Dly","type":"float","min":-100,"max":100,"default":0,"step":0.1,"unit":"%","viz":{"group":"duck","role":"delay","kind":"envelope"}},{"key":"attack","name":"Attack","short_name":"Att","type":"float","min":0,"max":200,"default":2,"step":0.1,"unit":"%","viz":{"group":"duck","role":"attack"}},{"key":"hold","name":"Hold","short_name":"Hold","type":"float","min":0,"max":200,"default":8,"step":0.1,"unit":"%","viz":{"group":"duck","role":"hold"}},{"key":"release","name":"Release","short_name":"Rel","type":"float","min":0,"max":200,"default":35,"step":0.1,"unit":"%","viz":{"group":"duck","role":"release"}},{"key":"curve","name":"Curve","type":"enum","options":["Linear","Exponential","S-Curve"],"wire_format":"index","default":"1"},{"key":"time_mode","name":"Time","type":"enum","options":["ms","% of cycle"],"wire_format":"index","default":"0"},{"key":"channel","name":"Channel","short_name":"Ch","type":"enum","options":["Omni","1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16"],"wire_format":"index","default":"1"},{"key":"trigger_note","name":"Trigger","type":"enum","options":["C-2","C#-2","D-2","D#-2","E-2","F-2","F#-2","G-2","G#-2","A-2","A#-2","B-2","C-1","C#-1","D-1","D#-1","E-1","F-1","F#-1","G-1","G#-1","A-1","A#-1","B-1","C0","C#0","D0","D#0","E0","F0","F#0","G0","G#0","A0","A#0","B0","C1","C#1","D1","D#1","E1","F1","F#1","G1","G#1","A1","A#1","B1","C2","C#2","D2","D#2","E2","F2","F#2","G2","G#2","A2","A#2","B2","C3","C#3","D3","D#3","E3","F3","F#3","G3","G#3","A3","A#3","B3","C4","C#4","D4","D#4","E4","F4","F#4","G4","G#4","A4","A#4","B4","C5","C#5","D5","D#5","E5","F5","F#5","G5","G#5","A5","A#5","B5","C6","C#6","D6","D#6","E6","F6","F#6","G6","G#6","A6","A#6","B6","C7","C#7","D7","D#7","E7","F7","F#7","G7","G#7","A7","A#7","B7","C8","C#8","D8","D#8","E8","F8","F#8","G8"],"wire_format":"index","default":"36"},{"key":"midi_mode","name":"Mode","type":"enum","options":["Trigger","Gate"],"wire_format":"index","default":"0"},{"key":"vel_sens","name":"Vel Sens","short_name":"Vel","type":"float","min":0,"max":1,"default":0,"step":0.01,"unit":"%"},{"key":"ui","name":"UI","type":"string","access":"read"},{"key":"sweep","name":"Sweep","type":"string","access":"read","live":true},{"key":"shape","name":"Shape","type":"canvas","as_page":true,"show_value":false,"extra_keys":["ui","rate"]}]"##;


#[cfg(test)]
mod tests {
    use super::CHAIN_PARAMS;
    use sc_core::params::Param;
    use sc_core::Instance;

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
     * Parsed by scanning rather than with a JSON crate: the workspace has no
     * external dependencies and this is not where that changes.
     */

    /// Every `"key":"..."` in declaration order.
    fn keys() -> Vec<String> {
        entries().into_iter().map(|(k, _)| k).collect()
    }

    /// Each entry as (key, type).
    fn entries() -> Vec<(String, String)> {
        let mut out = Vec::new();
        for entry in CHAIN_PARAMS.split("{\"key\":\"").skip(1) {
            let Some(end) = entry.find('"') else { continue };
            let key = entry[..end].to_string();
            let ty = entry
                .find("\"type\":\"")
                .map(|i| {
                    let r = &entry[i + 8..];
                    r[..r.find('"').unwrap_or(0)].to_string()
                })
                .unwrap_or_default();
            out.push((key, ty));
        }
        out
    }

    /// The `viz` group named on each entry, in the same order as `keys()`.
    fn viz_groups() -> Vec<Option<String>> {
        let mut out = Vec::new();
        for entry in CHAIN_PARAMS.split("{\"key\":\"").skip(1) {
            let group = entry.find("\"group\":\"").map(|i| {
                let r = &entry[i + 9..];
                r[..r.find('"').unwrap_or(0)].to_string()
            });
            out.push(group);
        }
        out
    }

    #[test]
    fn the_declaration_is_balanced_and_non_empty() {
        assert!(CHAIN_PARAMS.starts_with('['), "chain_params must be a JSON array");
        assert!(CHAIN_PARAMS.ends_with(']'));
        let opens = CHAIN_PARAMS.matches('{').count();
        let closes = CHAIN_PARAMS.matches('}').count();
        assert_eq!(opens, closes, "unbalanced braces");
        assert!(keys().len() >= 14, "only {} keys", keys().len());
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

    #[test]
    fn every_writable_parameter_the_engine_has_is_declared_or_deliberately_not() {
        /*
         * THE OTHER DIRECTION: a parameter added to the engine and forgotten
         * here is a control that never appears on the device.
         *
         * Three are absent ON PURPOSE, and naming them here is what makes that a
         * decision rather than an oversight -- the Move has no aux input, so the
         * sidechain source and its detector cannot do anything.
         */
        const DELIBERATELY_ABSENT: [&str; 3] = ["threshold", "lockout", "source_sidechain"];
        let declared = keys();
        for i in 0..sc_core::params::PARAM_COUNT {
            let Some(p) = Param::from_i32(i) else { continue };
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
