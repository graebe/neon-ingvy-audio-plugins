// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The Schwung audio_fx v2 glue: the host's vtable mirrored, the transport read
from it, and the instance entry points -- generic over a [`Module`], so a
product's `*-move` crate is its `chain_params` and a thin impl.

# Threading

Every entry point runs on the SPI audio callback (SCHED_FIFO 70, core 3,
~2370us per 128-frame block): no allocation outside `create_instance`, no
I/O, no locks, and no logging, including the host's. The one exception is
the host's, not ours: a chain loads a module into a bus position on its
worker thread, so init and `create_instance` may also run there, at the same
time as a slot's on the callback. Nothing they share is written without
synchronisation.
*/

use ni_dsp::ffi::cstr;
use ni_dsp::Transport;
use std::ffi::{c_char, c_int, c_void};
use std::marker::PhantomData;
use std::os::raw::c_uchar;
use std::ptr::null_mut;
use std::sync::atomic::{AtomicPtr, Ordering};

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
 * THE HOST'S CLOCK, COPIED OUT OF ITS TABLE AT INIT. The table itself is not
 * kept, for two reasons the Move's own source gives.
 *
 * ITS LIFETIME IS THE CALLER'S. A chain hands init `&inst->subplugin_host_api`,
 * a member of the chain instance it frees when the chain goes (chain_host.c),
 * so a pointer kept past init can outlive what it points at -- and the
 * `static mut` this replaced kept the LAST caller's, whichever chain that was.
 *
 * INIT CAN RUN ON TWO THREADS AT ONCE. A slot loads on the SPI callback and a
 * bus position on the chain's worker (chain_bus.c), so the same module in both
 * is two inits racing, and a `static mut` both of them write is a data race.
 *
 * What a module needs from the table is two functions, and every table the
 * Move builds carries the same two: the shim's `shim_get_bpm` and
 * `shadow_transport_beat_position`, which live as long as the process. So
 * init copies them into atomics, and the audio thread's cost is two loads.
 * The latest init wins, as it always did; two racing can leave one's tempo
 * beside the other's beat, which are the same shim's.
 *
 * ADDRESSES, NOT FUNCTION POINTERS, because std has no atomic function
 * pointer: `as *mut ()` is how its documentation says to hold one, and
 * transmuting back is sound because each slot only ever holds its own type, or
 * null. RELAXED, because the address is all there is to publish -- of code
 * the host mapped before it called init.
 */
static HOST_BPM: AtomicPtr<()> = AtomicPtr::new(null_mut());
static HOST_BEATS: AtomicPtr<()> = AtomicPtr::new(null_mut());

type GetBpm = extern "C" fn() -> f32;
type GetBeatPosition = extern "C" fn() -> f64;

/// Copy the clock out of `host`. No table, or a table without a clock, is a
/// host that tells us nothing: a stopped transport at 120.
fn keep_clock(host: Option<&HostApiV1>) {
    let bpm = host.and_then(|h| h.get_bpm).map_or(null_mut(), |f| f as *mut ());
    let beats = host.and_then(|h| h.get_beat_position).map_or(null_mut(), |f| f as *mut ());
    HOST_BPM.store(bpm, Ordering::Relaxed);
    HOST_BEATS.store(beats, Ordering::Relaxed);
}

fn host_bpm() -> Option<GetBpm> {
    let p = HOST_BPM.load(Ordering::Relaxed);
    /* SAFETY: `keep_clock` stores a GetBpm here, or null, and nothing else. */
    (!p.is_null()).then(|| unsafe { std::mem::transmute::<*mut (), GetBpm>(p) })
}

fn host_beat_position() -> Option<GetBeatPosition> {
    let p = HOST_BEATS.load(Ordering::Relaxed);
    /* SAFETY: `keep_clock` stores a GetBeatPosition here, or null. */
    (!p.is_null()).then(|| unsafe { std::mem::transmute::<*mut (), GetBeatPosition>(p) })
}

/// The host's tempo and beat position. A negative beat is a stopped transport.
pub fn read_transport() -> Transport {
    let mut t = Transport { bpm: 120.0, running: false, beats: -1.0 };
    if let Some(f) = host_bpm() {
        let b = f();
        if b > 1.0 && b < 1000.0 {
            t.bpm = b;
        }
    }
    if let Some(f) = host_beat_position() {
        let beats = f();
        if beats >= 0.0 {
            t.running = true;
            t.beats = beats;
        }
    }
    t
}

/// The table init returns for module type `M`. A CONSTANT: init hands out a
/// read-only table and writes nothing a second thread's init could race, and
/// two module types in one binary each keep their own.
///
/// The host only reads it: chain_host.c, chain_bus.c, shadow_chain_mgmt.c and
/// schwung_shim.c call through it and keep the pointer. The `*mut` in init's
/// signature is the C header's spelling, not a promise to write.
struct Vtable<M>(PhantomData<M>);

impl<M: Module> Vtable<M> {
    const API: AudioFxApiV2 = AudioFxApiV2 {
        api_version: 2,
        create_instance: Some(create::<M>),
        destroy_instance: Some(destroy::<M>),
        process_block: Some(process_block::<M>),
        set_param: Some(set_param::<M>),
        get_param: Some(get_param::<M>),
        on_midi: None,
    };
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
///
/// # Safety
/// `host` is null or a host table that is valid for the call -- which is as
/// long as this reads it.
pub unsafe fn init<M: Module>(host: *const HostApiV1) -> *mut AudioFxApiV2 {
    keep_clock(unsafe { host.as_ref() });
    let api: &'static AudioFxApiV2 = &Vtable::<M>::API;
    (api as *const AudioFxApiV2).cast_mut()
}

/// `move_audio_fx_on_midi`'s body; see [`export_audio_fx!`]. `source` is
/// ignored: a note is a note whether it came from the pads, USB or the host.
///
/// # Safety
/// `inst` is null or an instance this module's table created and has not
/// destroyed, and `msg` is null or readable for `len` bytes.
pub unsafe fn on_midi<M: Module>(inst: *mut c_void, msg: *const c_uchar, len: c_int, _source: c_int) {
    let Some(inst) = (unsafe { (inst as *mut M).as_mut() }) else { return };
    if msg.is_null() || len <= 0 || len > 8 {
        return;
    }
    let bytes = unsafe { std::slice::from_raw_parts(msg, len as usize) };
    inst.on_midi(bytes);
}

/// Export the two symbols the chain host looks up, for module type `$m`.
///
/// They are the C host's entry points, so the contract on their pointers is
/// the C header's and their caller keeps it, as with any C function: the
/// `unsafe` inside each is that contract, taken on the host's word. Clippy's
/// rule for a public function that dereferences a pointer argument is for
/// functions Rust code calls, so it is waived here, by name and with that
/// reason; the module crates' tests do call them, on the same terms.
#[macro_export]
macro_rules! export_audio_fx {
    ($m:ty) => {
        #[no_mangle]
        #[allow(clippy::not_unsafe_ptr_arg_deref, reason = "a C entry point: the C header is its contract")]
        pub extern "C" fn move_audio_fx_init_v2(
            host: *const $crate::HostApiV1,
        ) -> *mut $crate::AudioFxApiV2 {
            unsafe { $crate::init::<$m>(host) }
        }

        #[no_mangle]
        #[allow(clippy::not_unsafe_ptr_arg_deref, reason = "a C entry point: the C header is its contract")]
        pub extern "C" fn move_audio_fx_on_midi(
            inst: *mut ::std::ffi::c_void,
            msg: *const ::std::os::raw::c_uchar,
            len: ::std::ffi::c_int,
            source: ::std::ffi::c_int,
        ) {
            unsafe { $crate::on_midi::<$m>(inst, msg, len, source) }
        }
    };
}

#[cfg(test)]
mod tests;
