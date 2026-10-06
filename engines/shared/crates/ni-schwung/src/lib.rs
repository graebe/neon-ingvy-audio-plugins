// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The Schwung audio_fx v2 glue: the host's vtable mirrored, the transport read
from it, and the instance entry points -- generic over a [`Module`], so a
product's `*-move` crate is its `chain_params` and a thin impl.

# Threading

Every entry point runs on the SPI audio callback (SCHED_FIFO 70, core 3,
~2370us per 128-frame block). No control thread: no allocation outside
`create_instance`, no I/O, no locks, and no logging, including the host's.
*/

use ni_dsp::ffi::cstr;
use ni_dsp::Transport;
use std::ffi::{c_char, c_int, c_void};
use std::os::raw::c_uchar;

/// The Move's mailbox rate. The engines take a rate only so a DAW can differ.
pub const SAMPLE_RATE: f64 = 44100.0;

/// What a product supplies to become a Schwung audio_fx module.
pub trait Module: Sized + 'static {
    /// The `chain_params` JSON the knob grid draws the module from.
    const CHAIN_PARAMS: &'static str;
    fn new(sample_rate: f64) -> Self;
    fn process_i16(&mut self, buf: &mut [i16], frames: usize, t: Option<&Transport>);
    fn set_param(&mut self, key: &str, val: &str);
    /// The value into `out`, NUL-terminated; its length, or -1 for a key the
    /// engine does not serve.
    fn get_param(&self, key: &str, out: &mut [u8]) -> c_int;
    /// Raw MIDI, at offset 0 -- the v2 API has no offset. A host panic
    /// arrives here as CC 120/123: the vtable has no reset hook.
    fn on_midi(&mut self, _msg: &[u8]) {}
}

/*
 * THE HOST'S VTABLE, MIRRORED FIELD FOR FIELD. Only `get_bpm` and
 * `get_beat_position` are read, but every field before them must be here and
 * the right size or they are read from the wrong offsets -- no crash, just a
 * tempo nobody chose. Scalars are interleaved with pointers, so this cannot be
 * a table of pointers.
 */
#[repr(C)]
pub struct HostApiV1 {
    pub api_version: u32,
    pub sample_rate: c_int,
    pub frames_per_block: c_int,
    pub mapped_memory: *mut u8,
    pub audio_out_offset: c_int,
    pub audio_in_offset: c_int,
    pub log: Option<extern "C" fn(*const c_char)>,
    pub midi_send_internal: Option<extern "C" fn(*const c_uchar, c_int) -> c_int>,
    pub midi_send_external: Option<extern "C" fn(*const c_uchar, c_int) -> c_int>,
    pub get_clock_status: Option<extern "C" fn() -> c_int>,
    pub mod_emit_value: *mut c_void,
    pub mod_clear_source: *mut c_void,
    pub mod_host_ctx: *mut c_void,
    pub get_bpm: Option<extern "C" fn() -> f32>,
    pub midi_inject_to_move: Option<extern "C" fn(*const c_uchar, c_int) -> c_int>,
    pub slot_recv_channel: Option<extern "C" fn(*mut c_void) -> c_int>,
    pub get_beat_position: Option<extern "C" fn() -> f64>,
    pub reserved: [*mut c_void; 8],
}

impl HostApiV1 {
    /// A host that offers only a clock, and nothing else.
    pub fn with_clock(get_bpm: extern "C" fn() -> f32, get_beat_position: extern "C" fn() -> f64) -> Self {
        let null = std::ptr::null_mut();
        HostApiV1 {
            api_version: 1,
            sample_rate: SAMPLE_RATE as c_int,
            frames_per_block: 128,
            mapped_memory: null as *mut u8,
            audio_out_offset: 0,
            audio_in_offset: 0,
            log: None,
            midi_send_internal: None,
            midi_send_external: None,
            get_clock_status: None,
            mod_emit_value: null,
            mod_clear_source: null,
            mod_host_ctx: null,
            get_bpm: Some(get_bpm),
            midi_inject_to_move: None,
            slot_recv_channel: None,
            get_beat_position: Some(get_beat_position),
            reserved: [null; 8],
        }
    }
}

#[repr(C)]
pub struct AudioFxApiV2 {
    pub api_version: u32,
    pub create_instance: Option<extern "C" fn(*const c_char, *const c_char) -> *mut c_void>,
    pub destroy_instance: Option<extern "C" fn(*mut c_void)>,
    pub process_block: Option<extern "C" fn(*mut c_void, *mut i16, c_int)>,
    pub set_param: Option<extern "C" fn(*mut c_void, *const c_char, *const c_char)>,
    pub get_param: Option<extern "C" fn(*mut c_void, *const c_char, *mut c_char, c_int) -> c_int>,
    pub on_midi: Option<extern "C" fn(*mut c_void, *const c_uchar, c_int, c_int)>,
}

/*
 * The host hands its table to `move_audio_fx_init_v2` once and every later
 * call needs it. `static mut` is the shape the ABI forces; it is sound because
 * init runs once, before any instance exists, and every reader afterwards is
 * the same single audio thread. One module per .so, so one of each.
 */
static mut HOST: *const HostApiV1 = std::ptr::null();
static mut FX_API: AudioFxApiV2 = AudioFxApiV2 {
    api_version: 2,
    create_instance: None,
    destroy_instance: None,
    process_block: None,
    set_param: None,
    get_param: None,
    on_midi: None,
};

/// The host's tempo and beat position. A negative beat is a stopped transport.
pub fn read_transport() -> Transport {
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

extern "C" fn create<M: Module>(_dir: *const c_char, _cfg: *const c_char) -> *mut c_void {
    Box::into_raw(Box::new(M::new(SAMPLE_RATE))) as *mut c_void
}

extern "C" fn destroy<M: Module>(inst: *mut c_void) {
    if !inst.is_null() {
        drop(unsafe { Box::from_raw(inst as *mut M) });
    }
}

extern "C" fn process_block<M: Module>(inst: *mut c_void, audio: *mut i16, frames: c_int) {
    let Some(inst) = (unsafe { (inst as *mut M).as_mut() }) else { return };
    if audio.is_null() || frames <= 0 {
        return;
    }
    let t = read_transport();
    let buf = unsafe { std::slice::from_raw_parts_mut(audio, frames as usize * 2) };
    inst.process_i16(buf, frames as usize, Some(&t));
}

extern "C" fn set_param<M: Module>(inst: *mut c_void, key: *const c_char, val: *const c_char) {
    let Some(inst) = (unsafe { (inst as *mut M).as_mut() }) else { return };
    if key.is_null() || val.is_null() {
        return;
    }
    inst.set_param(unsafe { cstr(key) }, unsafe { cstr(val) });
}

/*
 * The engine serves every parameter it owns and answers -1 for the rest; the
 * two keys here describe how SCHWUNG draws the module. `ui_hierarchy` is
 * SERVED EMPTY, NOT REFUSED: -1 makes the component entry gate hold for
 * HOLD_UNSERVED_READ_LIMIT attempts on every entry, and "" (falsy, like -1)
 * is the truth -- the module supplies no hierarchy.
 */
extern "C" fn get_param<M: Module>(
    inst: *mut c_void,
    key: *const c_char,
    buf: *mut c_char,
    buf_len: c_int,
) -> c_int {
    let Some(inst) = (unsafe { (inst as *mut M).as_ref() }) else { return -1 };
    if key.is_null() || buf.is_null() || buf_len <= 0 {
        return -1;
    }
    let key = unsafe { cstr(key) };
    let out = unsafe { std::slice::from_raw_parts_mut(buf as *mut u8, buf_len as usize) };

    if key == "ui_hierarchy" {
        out[0] = 0;
        return 0;
    }
    if key == "chain_params" {
        let p = M::CHAIN_PARAMS.as_bytes();
        if p.len() >= out.len() {
            return -1;
        }
        out[..p.len()].copy_from_slice(p);
        out[p.len()] = 0;
        return p.len() as c_int;
    }
    inst.get_param(key, out)
}

/// `move_audio_fx_init_v2`'s body; see [`export_audio_fx!`].
///
/// `on_midi` is NOT set in the table: an older host built against a six-field
/// struct reads it off the end of its own idea of the table. The host dlsym's
/// the free symbol `move_audio_fx_on_midi` instead.
pub fn init<M: Module>(host: *const HostApiV1) -> *mut AudioFxApiV2 {
    unsafe {
        HOST = host;
        FX_API = AudioFxApiV2 {
            api_version: 2,
            create_instance: Some(create::<M>),
            destroy_instance: Some(destroy::<M>),
            process_block: Some(process_block::<M>),
            set_param: Some(set_param::<M>),
            get_param: Some(get_param::<M>),
            on_midi: None,
        };
        std::ptr::addr_of_mut!(FX_API)
    }
}

/// `move_audio_fx_on_midi`'s body; see [`export_audio_fx!`]. `source` is
/// ignored: a note is a note whether it came from the pads, USB or the host.
pub fn on_midi<M: Module>(inst: *mut c_void, msg: *const c_uchar, len: c_int, _source: c_int) {
    let Some(inst) = (unsafe { (inst as *mut M).as_mut() }) else { return };
    if msg.is_null() || len <= 0 || len > 8 {
        return;
    }
    let bytes = unsafe { std::slice::from_raw_parts(msg, len as usize) };
    inst.on_midi(bytes);
}

/// Export the two symbols the chain host looks up, for module type `$m`.
#[macro_export]
macro_rules! export_audio_fx {
    ($m:ty) => {
        #[no_mangle]
        pub extern "C" fn move_audio_fx_init_v2(
            host: *const $crate::HostApiV1,
        ) -> *mut $crate::AudioFxApiV2 {
            $crate::init::<$m>(host)
        }

        #[no_mangle]
        pub extern "C" fn move_audio_fx_on_midi(
            inst: *mut ::std::ffi::c_void,
            msg: *const ::std::os::raw::c_uchar,
            len: ::std::ffi::c_int,
            source: ::std::ffi::c_int,
        ) {
            $crate::on_midi::<$m>(inst, msg, len, source)
        }
    };
}

#[cfg(test)]
mod tests;
