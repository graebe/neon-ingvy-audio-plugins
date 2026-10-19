// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The Schwung shell the nine single filters share: [`SingleFilter`], a
[`Module`] over [`sf_core`] generic over its [`Product`], and each product's
`chain_params`. The vtable, the transport and the entry points are
`ni_schwung`'s; each product's module is a one-line cdylib that exports
`SingleFilter<ItsProduct>` (crates/move/sf-move-<product>).

WHAT A MODULE IS. The plugin's engine and its parameters, by its keys, on
one page of eight knobs (a High Pass's or Low Pass's Matched on a second):
the product's table less the Analyzer, which is a view. Freq and Q are the
Move's own steps, sent by name (steps.rs): a Schwung float turns linearly,
which would spend a knob's travel above a few kHz.

THE PANIC. The v2 vtable has no reset hook, so a host panic reaches a module
only as CC 120 (All Sound Off) or CC 123 (All Notes Off) through on_midi. The
engine resets on either: the filter empties, a fade lands, so the next block
starts from silence.
*/

use core::fmt::Write;
use core::marker::PhantomData;
use ni_dsp::fmt::Buf;
use ni_dsp::Transport;
use ni_schwung::Module;
use sf_core::params::{self, Field};
use sf_core::{Engine, Shape};
use std::ffi::c_int;

pub mod steps;

/// A product: its filter's shape and its knob grid.
pub trait Product: 'static {
    const SHAPE: Shape;
    const CHAIN_PARAMS: &'static str;
}

macro_rules! products {
    ($($name:ident $shape:ident $key:literal),* $(,)?) => {$(
        #[doc = concat!("NI ", $key, ", on the Move.")]
        pub struct $name;
        impl Product for $name {
            const SHAPE: Shape = Shape::$shape;
            const CHAIN_PARAMS: &'static str = include_str!(concat!(env!("OUT_DIR"), "/chain_params_", $key, ".json"));
        }
    )*};
}

products!(
    HighPass HighPass "high-pass",
    LowPass LowPass "low-pass",
    BandPass BandPass "band-pass",
    Notch Notch "notch",
    Bell Bell "bell",
    LowShelf LowShelf "low-shelf",
    HighShelf HighShelf "high-shelf",
    Tilt Tilt "tilt",
    AllPass AllPass "all-pass",
);

/// A single filter, as a Schwung module.
pub struct SingleFilter<P: Product>(Engine, PhantomData<P>);

impl<P: Product> SingleFilter<P> {
    pub fn engine(&self) -> &Engine {
        &self.0
    }
}

impl<P: Product> Module for SingleFilter<P> {
    const CHAIN_PARAMS: &'static str = P::CHAIN_PARAMS;
    fn new(sample_rate: f64) -> Self {
        SingleFilter(Engine::new(P::SHAPE, sample_rate).expect("a product's shape"), PhantomData)
    }
    /* No tempo is needed: a filter keeps no time but the samples'. */
    fn process_i16(&mut self, buf: &mut [i16], frames: usize, _t: Option<&Transport>) {
        self.0.process_i16(buf, frames);
    }
    fn set_param(&mut self, key: &str, val: &str) {
        self.0.set_param(key, val);
    }
    /* A stepped knob reads back the step it sent, in its words. */
    fn get_param(&self, key: &str, out: &mut [u8]) -> c_int {
        if let Some(label) = step_label::<P>(key, &self.0) {
            let mut b = Buf::new(out);
            return if b.write_str(label).is_ok() { b.finish() } else { -1 };
        }
        self.0.get_param(key, out)
    }
    fn on_midi(&mut self, msg: &[u8]) {
        self.0.on_midi(msg);
    }
}

/// The steps a knob of `field` offers on product `shape`'s module, if it is
/// stepped.
pub fn steps_of(shape: Shape, field: Field) -> Option<&'static [&'static str]> {
    match field {
        Field::Freq => Some(&steps::FREQ),
        Field::Q if matches!(shape, Shape::Bell | Shape::BandPass | Shape::Notch) => Some(&steps::Q_WIDTH),
        Field::Q => Some(&steps::Q_SLOPE),
        _ => None,
    }
}

/// The declared step a stepped knob's value is, if it is one.
fn step_label<P: Product>(key: &str, e: &Engine) -> Option<&'static str> {
    let field = params::from_key(P::SHAPE, key)?;
    let labels = steps_of(P::SHAPE, field)?;
    let v = e.get(field);
    labels.iter().copied().find(|l| params::parse_value(P::SHAPE, field, l).is_some_and(|x| (x - v).abs() < 1e-6))
}

#[cfg(test)]
mod no_alloc;
#[cfg(test)]
mod tests;
