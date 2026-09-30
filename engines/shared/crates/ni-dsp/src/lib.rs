/*!
The pieces every Neon Ingvy engine needs and no product owns: C-compatible
formatting and parsing, the rate table's parser, the envelope curves, the
one-pole, the parameter glide, the transport-following phase, a scope's cycle
sweep, and the C helpers a capi crate's entry points are built from.

It knows no product. A product's crates depend on it; it depends on nothing.
Everything here runs on an audio callback, so nothing here allocates.
*/

pub mod curve;
pub mod ffi;
pub mod fmt;
pub mod onepole;
pub mod phase;
pub mod rate;
pub mod smooth;
pub mod sweep;

/// What the host says about the transport.
#[derive(Clone, Copy, Default, Debug)]
pub struct Transport {
    pub running: bool,
    /// Quarter notes since the start of the timeline. Negative means "no
    /// transport", which is NOT the same as beat zero.
    pub beats: f64,
    pub bpm: f32,
}
