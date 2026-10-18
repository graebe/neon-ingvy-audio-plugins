// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
A node's settings as the parameter table holds them, and the design they
make.

THE DESIGN ITSELF is shared (ni_filter_node::design, re-exported here): one
pure function, which the engine calls with its glided values and the picture
with the host's, so what the editor draws is the filter the engine runs. What
each type uses is written there.

WHAT IS FILTER DESIGNER'S is how its table spells a node -- slot `n`'s ten
fields, the global Matched Cuts, Scale and Adaptive Q -- read here into the
[`Target`] a node plays, and the tail of everything heard.
*/

use crate::params::{Field, Param, NODES};
pub use ni_filter_node::design::*;
use ni_filter::Family;

/// Node `slot`'s settings as `values` hold them: its Type whether or not it
/// is on (a bypassed node still has a shape to draw).
pub fn settings_of(values: &[f64], slot: u8) -> Settings {
    let v = |f: Field| values[Param::Node(slot, f).index()];
    let shape = Shape::from_value(v(Field::Type));
    Settings {
        shape,
        family: Family::from_index(v(Field::Family).round().max(0.0) as usize),
        order: v(Field::Order).round().max(0.0) as usize + 1,
        freq: v(Field::Freq),
        gain_db: v(Field::Gain),
        q: v(Field::Q),
        ripple_db: v(Field::Ripple),
        stopband_db: v(Field::Stopband),
        /* Only a high-pass's or a low-pass's design changes with it, so only
         * they fade when it flips. */
        matched: values[Param::MatchedCuts.index()] >= 0.5 && shape.uses_matched(),
    }
}

/// Node `slot`'s channel as `values` hold it.
pub fn channel_of(values: &[f64], slot: u8) -> Channel {
    Channel::from_value(values[Param::Node(slot, Field::Channel).index()])
}

/// Whether node `slot` is heard: a type, and switched on.
pub fn is_heard(values: &[f64], slot: u8) -> bool {
    target_of(values, slot).is_heard()
}

/// What node `slot` is asked to play, as `values` hold it.
pub fn target_of(values: &[f64], slot: u8) -> Target {
    Target {
        settings: settings_of(values, slot),
        channel: channel_of(values, slot),
        on: values[Param::Node(slot, Field::On).index()] >= 0.5,
    }
}

/// Scale and Adaptive Q as `values` hold them.
pub fn globals_of(values: &[f64]) -> Globals {
    Globals {
        scale: values[Param::Scale.index()] / 100.0,
        adaptive: if values[Param::AdaptiveQ.index()] >= 0.5 { 1.0 } else { 0.0 },
    }
}

/// What a filter added at `hz` is: a High-Pass below 30 Hz, a Low-Pass above
/// 15 kHz, a Bell between them.
pub fn shape_for_new(hz: f64) -> Shape {
    if hz < 30.0 {
        Shape::HighPass
    } else if hz > 15_000.0 {
        Shape::LowPass
    } else {
        Shape::Bell
    }
}

/// The longest tail [`tail_seconds`] reports.
pub const TAIL_MAX_SECONDS: f64 = 30.0;

/// THE TAIL for `values` at `fs`: the seconds the output goes on once the
/// input stops, until it is 60 dB down -- the isolator's slowest band, then
/// every heard node, in series, each section's decay in turn. An upper bound,
/// at most [`TAIL_MAX_SECONDS`]: a host that renders this long after the
/// input truncates no ringing filter. Pure; not the audio thread.
pub fn tail_seconds(values: &[f64], fs: f64) -> f64 {
    const DB: f64 = 60.0;
    if !fs.is_finite() || fs <= 0.0 || values.len() < crate::PARAM_COUNT {
        return 0.0;
    }
    let iso = if values[Param::IsoOn.index()] >= 0.5 {
        let (bands, _) = crate::isolator::picture(values, fs);
        bands.iter().map(|path| path.iter().map(|c| c.decay_samples(DB)).sum::<f64>()).fold(0.0, f64::max)
    } else {
        0.0
    };
    let g = globals_of(values);
    let nodes: f64 = (0..NODES as u8)
        .filter(|&slot| is_heard(values, slot))
        .map(|slot| decay_samples(&settings_of(values, slot), g, fs, DB))
        .sum();
    ((iso + nodes) / fs).min(TAIL_MAX_SECONDS)
}
