/*!
The Schwung shell. The engine is [`sc_core`].

What lives here is everything that only means something inside Schwung: the
`chain_params` contract the knob grid reads, the `ui_hierarchy` refusal that
routes the editor to `ui_chain.js`, the `audio_fx_api_v2` vtable, and the
transport callbacks. What lives in the core is the ducker itself.

The split exists because the same engine runs in a VST3/AU/CLAP plugin, and a
second copy of the envelope would drift from this one within a month -- with the
symptom "it sounds different in Live", which is the hardest kind of bug to chase.
One engine, two shells. AGENTS.md requires both, and `sc_render_ab` is what
proves they agree.

# What the Move does not have

**A sidechain bus.** The chain host hands over one interleaved buffer and there
is no aux input anywhere in the API, so `Source` offers Cycle and MIDI here and
the Sidechain option is absent from `chain_params`. Selecting it is not possible
rather than possible-but-silent.

# What the Move does have that the plugin does not

**A MIDI path that matters more.** `move_audio_fx_on_midi` is a real body here,
unlike `tg-move`'s empty one -- MIDI is one of Side-Chain's two trigger sources, not a
side channel. It is also the ONLY way a host panic can reach this module: the v2
vtable has no reset hook, so CC 120 and CC 123 are the whole of it.

# Threading

Every entry point runs on the SPI audio callback: SCHED_FIFO 70, pinned to core
3, ~2370us of slack per 128-frame block. `create_instance`, `destroy_instance`,
`set_param`, `get_param`, `on_midi` and `process_block` -- all of them. There is
no control thread, so no allocation outside `create_instance`, no I/O, no locks,
and **no logging at all**, including the host's, which buffers.
*/

/* Forces the sc_core_* C ABI into this staticlib. Without the `extern crate`,
 * nothing here references sc-capi and the linker drops every one of its
 * #[no_mangle] symbols -- leaving a library that satisfies the Schwung vtable
 * and none of the engine's own entry points. */
extern crate sc_capi as _sc_capi;

use sc_core::{Instance, Transport};
use std::ffi::{c_char, c_int, c_void, CStr};
use std::os::raw::c_uchar;

mod params;

/*
 * THE HOST'S VTABLE, MIRRORED FIELD FOR FIELD.
 *
 * Only `get_bpm` and `get_beat_position` are read, but every field before them
 * has to be here and the right size or the two are read from the wrong offsets
 * -- which would not crash, it would return whatever a neighbouring pointer
 * happens to hold and run the ducker at a tempo nobody chose. The scalars are
 * interleaved with the function pointers, so this cannot be shortened to "a
 * table of pointers".
 */
#[repr(C)]
pub struct HostApiV1 {
    api_version: u32,
    sample_rate: c_int,
    frames_per_block: c_int,
    mapped_memory: *mut u8,
    audio_out_offset: c_int,
    audio_in_offset: c_int,
    log: Option<extern "C" fn(*const c_char)>,
    midi_send_internal: Option<extern "C" fn(*const c_uchar, c_int) -> c_int>,
    midi_send_external: Option<extern "C" fn(*const c_uchar, c_int) -> c_int>,
    get_clock_status: Option<extern "C" fn() -> c_int>,
    mod_emit_value: *mut c_void,
    mod_clear_source: *mut c_void,
    mod_host_ctx: *mut c_void,
    get_bpm: Option<extern "C" fn() -> f32>,
    midi_inject_to_move: Option<extern "C" fn(*const c_uchar, c_int) -> c_int>,
    slot_recv_channel: Option<extern "C" fn(*mut c_void) -> c_int>,
    get_beat_position: Option<extern "C" fn() -> f64>,
    reserved: [*mut c_void; 8],
}

#[repr(C)]
pub struct AudioFxApiV2 {
    api_version: u32,
    create_instance: Option<extern "C" fn(*const c_char, *const c_char) -> *mut c_void>,
    destroy_instance: Option<extern "C" fn(*mut c_void)>,
    process_block: Option<extern "C" fn(*mut c_void, *mut i16, c_int)>,
    set_param: Option<extern "C" fn(*mut c_void, *const c_char, *const c_char)>,
    get_param: Option<extern "C" fn(*mut c_void, *const c_char, *mut c_char, c_int) -> c_int>,
    on_midi: Option<extern "C" fn(*mut c_void, *const c_uchar, c_int, c_int)>,
}

/*
 * The host hands its table to `move_audio_fx_init_v2` once, and every later call
 * needs it. A `static mut` is the shape the C had and the shape the ABI forces
 * -- there is nowhere else to put it -- and it is sound here for the reason the
 * C's was: init runs once, before any instance exists, and every reader
 * afterwards is the same single audio thread.
 */
static mut HOST: *const HostApiV1 = std::ptr::null();

fn read_transport() -> Transport {
    let mut t = Transport { bpm: 120.0, running: false, beats: -1.0 };
    let host = unsafe { HOST };
    let Some(h) = (unsafe { host.as_ref() }) else { return t };
    if let Some(f) = h.get_bpm {
        let b = f();
        if b > 1.0 && b < 1000.0 {
            t.bpm = b;
        }
    }
    if let Some(f) = h.get_beat_position {
        let beats = f();
        if beats >= 0.0 {
            t.running = true;
            t.beats = beats;
        }
    }
    t
}

extern "C" fn v2_create_instance(_dir: *const c_char, _cfg: *const c_char) -> *mut c_void {
    /* Move's mailbox is 44100 and always has been; the core takes it as a
     * parameter only so a DAW can say otherwise. */
    Box::into_raw(Box::new(Instance::new(44100.0))) as *mut c_void
}

extern "C" fn v2_destroy_instance(inst: *mut c_void) {
    if !inst.is_null() {
        drop(unsafe { Box::from_raw(inst as *mut Instance) });
    }
}

extern "C" fn v2_process_block(inst: *mut c_void, audio: *mut i16, frames: c_int) {
    let Some(inst) = (unsafe { (inst as *mut Instance).as_mut() }) else { return };
    if audio.is_null() || frames <= 0 {
        return;
    }
    let t = read_transport();
    let buf = unsafe { std::slice::from_raw_parts_mut(audio, frames as usize * 2) };
    inst.process_i16(buf, frames as usize, Some(&t));
}

unsafe fn s<'a>(p: *const c_char) -> &'a str {
    if p.is_null() {
        return "";
    }
    CStr::from_ptr(p).to_str().unwrap_or("")
}

extern "C" fn v2_set_param(inst: *mut c_void, key: *const c_char, val: *const c_char) {
    let Some(inst) = (unsafe { (inst as *mut Instance).as_mut() }) else { return };
    if key.is_null() || val.is_null() {
        return;
    }
    inst.set_param(unsafe { s(key) }, unsafe { s(val) });
}

/*
 * The core serves every parameter it owns and answers -1 for the rest, which is
 * how the two keys below stay here: they describe how SCHWUNG should draw this
 * module and mean nothing to a plugin.
 */
extern "C" fn v2_get_param(
    inst: *mut c_void,
    key: *const c_char,
    buf: *mut c_char,
    buf_len: c_int,
) -> c_int {
    let Some(inst) = (unsafe { (inst as *mut Instance).as_ref() }) else { return -1 };
    if key.is_null() || buf.is_null() || buf_len <= 0 {
        return -1;
    }
    let key = unsafe { s(key) };
    let out = unsafe { std::slice::from_raw_parts_mut(buf as *mut u8, buf_len as usize) };

    /*
     * SERVED-EMPTY, NOT REFUSED.
     *
     * Returning -1 reads as "the read did not complete", and the component entry
     * gate then HOLDS for HOLD_UNSERVED_READ_LIMIT attempts before falling back
     * -- a delay on every entry, for a question we can answer instantly. "" means
     * "I have none", which is the truth: the hierarchy is supplied by
     * ui_chain.js to its own controller.
     */
    if key == "ui_hierarchy" {
        out[0] = 0;
        return 0;
    }

    if key == "chain_params" {
        let p = params::CHAIN_PARAMS.as_bytes();
        if p.len() >= out.len() {
            return -1;
        }
        out[..p.len()].copy_from_slice(p);
        out[p.len()] = 0;
        return p.len() as c_int;
    }

    inst.get_param(key, out)
}

static mut FX_API: AudioFxApiV2 = AudioFxApiV2 {
    api_version: 2,
    create_instance: None,
    destroy_instance: None,
    process_block: None,
    set_param: None,
    get_param: None,
    on_midi: None,
};

#[no_mangle]
pub extern "C" fn move_audio_fx_init_v2(host: *const HostApiV1) -> *mut AudioFxApiV2 {
    unsafe {
        HOST = host;
        FX_API = AudioFxApiV2 {
            api_version: 2,
            create_instance: Some(v2_create_instance),
            destroy_instance: Some(v2_destroy_instance),
            process_block: Some(v2_process_block),
            set_param: Some(v2_set_param),
            get_param: Some(v2_get_param),
            /*
             * NOT SET IN THE STRUCT, deliberately -- see the free symbol below.
             * ducker.c:552-562 records why: an older host built against a
             * six-field struct reads this field off the end of its own idea of
             * the table.
             */
            on_midi: None,
        };
        std::ptr::addr_of_mut!(FX_API)
    }
}

/*
 * A FREE SYMBOL, not a vtable field: the chain host dlsym's this name rather
 * than reading it off audio_fx_api_v2_t, which is what keeps a module with a
 * MIDI handler loadable by a host that predates the field.
 *
 * `source` is ignored: a trigger note is a trigger note whether it came from the
 * pads, from USB or from the host, and filtering by origin here would make the
 * Channel parameter mean two things at once.
 *
 * OFFSET 0, because this API has no offset to give. That is the one place the
 * Move is worse than the plugin -- up to a block of jitter on the trigger -- and
 * it is also what makes the render A/B meaningful: drive the plugin at offset 0
 * and the two must agree bit for bit.
 */
#[no_mangle]
pub extern "C" fn move_audio_fx_on_midi(
    inst: *mut c_void,
    msg: *const c_uchar,
    len: c_int,
    _source: c_int,
) {
    let Some(inst) = (unsafe { (inst as *mut Instance).as_mut() }) else { return };
    if msg.is_null() || len <= 0 || len > 8 {
        return;
    }
    let bytes = unsafe { std::slice::from_raw_parts(msg, len as usize) };
    inst.on_midi(bytes, 0);
}

/*
 * THE MOVE'S PARAMETER PATH, END TO END.
 *
 * The unit tests in `params.rs` pin the declaration against the engine's key
 * list. These drive the same two entry points the chain host calls, with the
 * values the host actually sends: the declared default (on a Delete-to-default)
 * and the option index its learner settles on (on a knob turn).
 */
#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CString;

    /// The entry for `key` in the declaration, as raw JSON text.
    fn entry(key: &str) -> &'static str {
        let tag = format!("{{\"key\":\"{key}\"");
        let at = params::CHAIN_PARAMS.find(&tag).expect("declared");
        let rest = &params::CHAIN_PARAMS[at..];
        &rest[..rest.find('}').unwrap() + 1]
    }

    /// A string field of an entry, e.g. `default` or `wire_format`.
    fn field(key: &str, name: &str) -> Option<String> {
        let e = entry(key);
        let tag = format!("\"{name}\":\"");
        let at = e.find(&tag)? + tag.len();
        Some(e[at..at + e[at..].find('"')?].to_string())
    }

    fn options(key: &str) -> Vec<String> {
        let e = entry(key);
        let at = e.find("\"options\":[").unwrap() + 11;
        e[at..at + e[at..].find(']').unwrap()]
            .split(',')
            .map(|s| s.trim_matches('"').to_string())
            .collect()
    }

    struct Module(*mut c_void);
    impl Module {
        fn new() -> Self {
            Module(v2_create_instance(std::ptr::null(), std::ptr::null()))
        }
        fn set(&self, k: &str, v: &str) {
            let (k, v) = (CString::new(k).unwrap(), CString::new(v).unwrap());
            v2_set_param(self.0, k.as_ptr(), v.as_ptr());
        }
        fn get(&self, k: &str) -> String {
            let k = CString::new(k).unwrap();
            let mut buf = [0 as c_char; 256];
            let n = v2_get_param(self.0, k.as_ptr(), buf.as_mut_ptr(), buf.len() as c_int);
            assert!(n >= 0, "{k:?} not served");
            let bytes: Vec<u8> = buf[..n as usize].iter().map(|c| *c as u8).collect();
            String::from_utf8(bytes).unwrap()
        }
    }
    impl Drop for Module {
        fn drop(&mut self) {
            v2_destroy_instance(self.0);
        }
    }

    #[test]
    fn every_enum_the_engine_reports_by_index_declares_it() {
        /*
         * THE HOST LEARNS AN UNDECLARED ENUM'S CONVENTION FROM A READ, and
         * until it has one it writes whatever it holds -- the declared DEFAULT
         * on a Delete, verbatim. An enum whose getter answers an index and
         * whose default is a label speaks two conventions at once; declaring
         * the index is what makes every write path agree with the read.
         */
        for key in ["source", "rate", "curve", "time_mode", "channel", "trigger_note", "midi_mode"] {
            assert_eq!(
                field(key, "wire_format").as_deref(),
                Some("index"),
                "{key} is read back as an index and must declare it"
            );
            let d = field(key, "default").unwrap();
            let n: usize = d.parse().unwrap_or_else(|_| panic!("{key} default {d:?} is not an index"));
            assert!(n < options(key).len(), "{key} default {n} is past its options");
        }
    }

    #[test]
    fn the_trigger_note_default_is_c1_on_the_device() {
        let m = Module::new();
        m.set("trigger_note", "60");
        m.set("trigger_note", &field("trigger_note", "default").unwrap());
        assert_eq!(m.get("trigger_note"), "36");
        assert_eq!(options("trigger_note")[36], "C1");
    }

    #[test]
    fn every_trigger_note_round_trips_as_an_index() {
        let m = Module::new();
        let opts = options("trigger_note");
        assert_eq!(opts.len(), 128);
        for (i, name) in opts.iter().enumerate() {
            m.set("trigger_note", &i.to_string());
            let got = m.get("trigger_note");
            assert_eq!(got, i.to_string(), "index {i}");
            /* A label reaches the same note -- a hand-written patch, or a
             * host that has not learned the convention yet. */
            m.set("trigger_note", "0");
            m.set("trigger_note", name);
            assert_eq!(m.get("trigger_note"), got, "label {name}");
        }
    }

    #[test]
    fn every_rate_round_trips_as_an_index_and_as_a_label() {
        let m = Module::new();
        for (i, name) in options("rate").iter().enumerate() {
            m.set("rate", &i.to_string());
            assert_eq!(m.get("rate"), i.to_string(), "index {i}");
            m.set("rate", "0");
            m.set("rate", name);
            assert_eq!(m.get("rate"), i.to_string(), "label {name}");
        }
        m.set("rate", &field("rate", "default").unwrap());
        assert_eq!(m.get("rate"), "4", "the default is 1/4");
    }

    #[test]
    fn panic_opens_the_gate() {
        /* The v2 vtable has no reset hook: CC 123 is the whole of a host panic. */
        let m = Module::new();
        m.set("source", "1");
        let on = [0x90u8, 36, 127];
        move_audio_fx_on_midi(m.0, on.as_ptr(), 3, 0);
        let mut buf = [10000i16; 256];
        v2_process_block(m.0, buf.as_mut_ptr(), 128);
        assert!(m.get("duck").parse::<f64>().unwrap() > 0.0);
        let cc = [0xB0u8, 123, 0];
        move_audio_fx_on_midi(m.0, cc.as_ptr(), 3, 0);
        let mut buf = [10000i16; 256];
        v2_process_block(m.0, buf.as_mut_ptr(), 128);
        assert_eq!(m.get("duck"), "0.0000");
        assert!(buf.iter().all(|&s| s == 10000));
    }
}
