/*!
The Schwung shell. The engine is [`tg_core`].

What lives here is everything that only means something inside Schwung: the
`chain_params` contract the knob grid reads, the `ui_hierarchy` refusal that
routes the editor to `ui_chain.js`, the `audio_fx_api_v2` vtable, and the
transport callbacks. What lives in the core is the gate itself.

The split exists because the same engine runs in a VST3/AU plugin, and a
second copy of the envelope would drift from this one within a month -- with
the symptom "it sounds different in Live", which is the hardest kind of bug to
chase. One engine, two shells.

# Threading

Every entry point here runs on the SPI audio callback: SCHED_FIFO 70, pinned
to core 3, ~2370us of slack per 128-frame block. `create_instance`,
`destroy_instance`, `set_param`, `get_param`, `on_midi` and `process_block` --
all of them. There is no control thread, so no allocation outside
`create_instance`, no I/O, no locks, and **no logging at all**, including the
host's, which buffers.
*/

/* Forces the tg_core_* C ABI into this staticlib. Without the `extern
 * crate`, nothing here references tg-capi and the linker drops every one of
 * its #[no_mangle] symbols -- leaving a library that satisfies the Schwung
 * vtable and none of the engine's own entry points. */
extern crate tg_capi as _tg_capi;

use std::ffi::{c_char, c_int, c_void, CStr};
use std::os::raw::c_uchar;
use tg_core::{Instance, Transport};

mod params;

/*
 * THE HOST'S VTABLE, MIRRORED FIELD FOR FIELD.
 *
 * Only `get_bpm` and `get_beat_position` are read, but every field before
 * them has to be here and the right size or the two are read from the wrong
 * offsets -- which would not crash, it would return whatever a neighbouring
 * pointer happens to hold and run the gate at a tempo nobody chose. The
 * scalars are interleaved with the function pointers, so this cannot be
 * shortened to "a table of pointers".
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
    create_instance:
        Option<extern "C" fn(*const c_char, *const c_char) -> *mut c_void>,
    destroy_instance: Option<extern "C" fn(*mut c_void)>,
    process_block: Option<extern "C" fn(*mut c_void, *mut i16, c_int)>,
    set_param: Option<extern "C" fn(*mut c_void, *const c_char, *const c_char)>,
    get_param:
        Option<extern "C" fn(*mut c_void, *const c_char, *mut c_char, c_int) -> c_int>,
    on_midi: Option<extern "C" fn(*mut c_void, *const c_uchar, c_int, c_int)>,
}

/*
 * The host hands its table to `move_audio_fx_init_v2` once, and every later
 * call needs it. A `static mut` is the shape the C had and the shape the ABI
 * forces -- there is nowhere else to put it -- and it is sound here for the
 * reason the C's was: init runs once, before any instance exists, and every
 * reader afterwards is the same single audio thread.
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
 * The core serves every parameter it owns and answers -1 for the rest, which
 * is how the two keys below stay here: they describe how SCHWUNG should draw
 * this module and mean nothing to a plugin.
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
     * Returning -1 reads as "the read did not complete", and the component
     * entry gate then HOLDS for HOLD_UNSERVED_READ_LIMIT attempts before
     * falling back -- a delay on every entry, for a question we can answer
     * instantly. "" means "I have none", which is the truth: the hierarchy is
     * supplied by ui_chain.js to its own controller.
     *
     * Both spellings are falsy, so getComponentHierarchy still returns null
     * and ui_chain.js still loads. Only the wait goes away.
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
            on_midi: None,
        };
        std::ptr::addr_of_mut!(FX_API)
    }
}

/*
 * A FREE SYMBOL, not a vtable field: the chain host dlsym's this name rather
 * than reading it off audio_fx_api_v2_t.
 */
#[no_mangle]
pub extern "C" fn move_audio_fx_on_midi(
    _inst: *mut c_void,
    _msg: *const c_uchar,
    _len: c_int,
    _source: c_int,
) {
    /* The engine has never used MIDI here: the arrows are claimed as CCs and
     * handled in ui_chain.js, which sees them before this would. */
}

/*
 * THE PANIC, ON THE GATE.
 *
 * A Schwung host panic reaches an audio FX as CC 120/123 through
 * `move_audio_fx_on_midi` (the v2 vtable has no reset hook). The Side-Chain
 * holds a note and must let go of it; the gate holds nothing -- its gain is a
 * function of the transport and the pattern -- so the right answer here is to
 * accept the panic and change nothing. These pin that it does exactly that,
 * rather than assuming an empty handler is harmless.
 */
#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CString;
    use std::sync::atomic::{AtomicU64, Ordering};

    /* The host's clock, as the two callbacks the shell reads. A RUNNING
     * transport, or the gate is open and the comparison proves nothing. */
    static BEATS: AtomicU64 = AtomicU64::new(0);
    extern "C" fn bpm() -> f32 {
        120.0
    }
    extern "C" fn beats() -> f64 {
        f64::from_bits(BEATS.load(Ordering::SeqCst))
    }

    fn host() -> HostApiV1 {
        HostApiV1 {
            api_version: 1,
            sample_rate: 44100,
            frames_per_block: 128,
            mapped_memory: std::ptr::null_mut(),
            audio_out_offset: 0,
            audio_in_offset: 0,
            log: None,
            midi_send_internal: None,
            midi_send_external: None,
            get_clock_status: None,
            mod_emit_value: std::ptr::null_mut(),
            mod_clear_source: std::ptr::null_mut(),
            mod_host_ctx: std::ptr::null_mut(),
            get_bpm: Some(bpm),
            midi_inject_to_move: None,
            slot_recv_channel: None,
            get_beat_position: Some(beats),
            reserved: [std::ptr::null_mut(); 8],
        }
    }

    fn render(panic: bool) -> Vec<i16> {
        BEATS.store(0f64.to_bits(), Ordering::SeqCst);
        let inst = v2_create_instance(std::ptr::null(), std::ptr::null());
        let (k, v) = (CString::new("amount").unwrap(), CString::new("0.8").unwrap());
        v2_set_param(inst, k.as_ptr(), v.as_ptr());
        let mut out = Vec::new();
        for b in 0..200 {
            if panic && b == 60 {
                for cc in [120u8, 123] {
                    let msg = [0xB0u8, cc, 0];
                    move_audio_fx_on_midi(inst, msg.as_ptr(), 3, 0);
                }
                let (k, v) = (CString::new("panic").unwrap(), CString::new("1").unwrap());
                v2_set_param(inst, k.as_ptr(), v.as_ptr());
            }
            let mut buf = [10000i16; 256];
            v2_process_block(inst, buf.as_mut_ptr(), 128);
            out.extend_from_slice(&buf);
            let b = beats() + 128.0 / 44100.0 * 2.0;
            BEATS.store(b.to_bits(), Ordering::SeqCst);
        }
        v2_destroy_instance(inst);
        out
    }

    #[test]
    fn a_host_panic_is_accepted_and_changes_nothing() {
        /* Leaked on purpose: the shell keeps the pointer for the process's
         * life, exactly as it keeps the real host's. */
        move_audio_fx_init_v2(Box::leak(Box::new(host())));
        let with = render(true);
        assert!(with.iter().any(|&v| v < 5000), "the gate never gated -- no transport?");
        assert_eq!(with, render(false));
    }
}
