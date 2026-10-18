// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
THE FIFTEEN PARAMETERS, DECLARED ONCE.

Every surface that shows the gate's parameters used to keep its own copy of
them: the engine's clamps and defaults, the plugin's host table
(plugins/trance-gate/Params.cpp) and the Move's knob grid (tg-move's
`chain_params`). They agreed by care, and not always -- Width ran from 0 in the
engine and from 5 % on both screens, so a saved patch could hold a width no
control could reach. This table is now the one place each parameter's
behaviour is written:

- the ENGINE clamps to `min..=max` and starts at `default`
  (`Instance::set_num`, `Sound::DEFAULT`);
- the PLUGIN builds its host table from [`Spec::host`] through the C ABI
  (`tg_core_param_spec`, `tg_core_param_option`), with Slot and Length one-based and the four levels
  percentages, as the iPlug2 build reported them -- a project stores plain host
  values, so those numbers are frozen, and tests/fixtures/iplug2 holds them;
- the MOVE generates its `chain_params` from it at build time (tg-move's
  build.rs), adding only how the knob grid draws a control.

What a surface only SHOWS -- a host's name, a knob's short label -- is the
surface's, and stays beside its row here or in that surface's generator.
*/

use core::ffi::CStr;

use ni_params::host::{host, percent, words};
/* The host column's shape is ni-params', shared with Side-Chain. */
pub use ni_params::host::{text, Host, HostKind};

use crate::params::Param;
use crate::{rates, MAX_STEPS, SLOTS, STAGE_MAX_PCT};

/// One automatable parameter.
#[derive(Clone, Copy, Debug)]
pub struct Spec {
    pub param: Param,
    /// The text door's key (`set_param`, `get_param`), which is also the
    /// Move's knob key.
    pub key: &'static str,
    /// The saved blob's top-level key for the current slot's value. Length
    /// has none: it is part of each slot's pattern field.
    pub blob: Option<&'static str>,
    /// The engine's range and default, in the engine's own units.
    pub min: f64,
    pub max: f64,
    pub default: f64,
    pub host: Host,
}

impl Spec {
    /// `v` held to the range. A NaN is not a value and is the caller's to drop.
    #[inline]
    pub fn clamp(&self, v: f64) -> f64 {
        v.clamp(self.min, self.max)
    }

    /// The host's plain value for an engine value, and back.
    #[inline]
    pub fn to_host(&self, engine: f64) -> f64 {
        self.host.to_host(engine)
    }
    #[inline]
    pub fn from_host(&self, host: f64) -> f64 {
        self.host.from_host(host)
    }

    /// The host's range and default, in its plain values.
    pub fn host_min(&self) -> f64 {
        self.to_host(self.min)
    }
    pub fn host_max(&self) -> f64 {
        self.to_host(self.max)
    }
    pub fn host_default(&self) -> f64 {
        self.to_host(self.default)
    }
}

const OFF_ON: &[&CStr] = &[c"Off", c"On"];
/* "%", not "% Step": what the percentage is OF is said once, by the name. */
const ENV_TIME: &[&CStr] = &[c"ms", c"%"];
const CURVES: &[&CStr] = &[c"Linear", c"Exponential", c"S-Curve"];
/* The switch does not turn the fade on; it chooses whether an arriving step
 * ramps or jumps. */
const HARD_SOFT: &[&CStr] = &[c"Hard", c"Soft"];
/* Which end the pattern is built up from; In is what the gate did before the
 * direction existed. */
const IN_OUT: &[&CStr] = &[c"In", c"Out"];

/*
 * IN `Param` ORDER, WHICH IS THE ABI AND THE HOST'S INDEX -- `spec` relies on
 * it and a test holds it. The host columns are the iPlug2 build's Declare,
 * row for row (tests/fixtures/iplug2/NITranceGate/parameters.json).
 */
pub const PARAMS: [Spec; 15] = [
    Spec {
        param: Param::Slot,
        key: "slot",
        blob: Some("slot"),
        min: 0.0,
        max: (SLOTS - 1) as f64,
        default: 0.0,
        host: Host { offset: 1.0, ..host(c"slot", c"Slot", HostKind::Integer) },
    },
    /* An option INDEX on the wire: index 15 is the option named "16". */
    Spec {
        param: Param::Length,
        key: "length",
        blob: None,
        min: 0.0,
        max: (MAX_STEPS - 1) as f64,
        default: 15.0,
        host: Host { offset: 1.0, units: c"steps", ..host(c"length", c"Length", HostKind::Integer) },
    },
    Spec {
        param: Param::Rate,
        key: "rate",
        blob: Some("rate"),
        min: 0.0,
        max: (rates::RATES.len() - 1) as f64,
        default: rates::RATE_DEFAULT as f64,
        host: host(c"rate", c"Rate", HostKind::Choice),
    },
    Spec {
        param: Param::Legato,
        key: "legato",
        blob: Some("legato"),
        min: 0.0,
        max: 1.0,
        default: 0.0,
        host: words(host(c"legato", c"Join Neighbors", HostKind::Toggle), OFF_ON),
    },
    Spec {
        param: Param::TimeMode,
        key: "time_mode",
        blob: Some("tmode"),
        min: 0.0,
        max: 1.0,
        default: 0.0,
        host: words(host(c"timeMode", c"Env Time", HostKind::Choice), ENV_TIME),
    },
    Spec {
        param: Param::Curve,
        key: "curve",
        blob: Some("curve"),
        min: 0.0,
        max: 2.0,
        default: 0.0,
        host: words(host(c"curve", c"Env Curve", HostKind::Choice), CURVES),
    },
    Spec { param: Param::Amount, key: "amount", blob: Some("amount"), min: 0.0, max: 1.0, default: 1.0, host: percent(c"amount", c"Amount") },
    /*
     * WIDTH FROM 5 %, ENGINE INCLUDED. Both screens always stopped at 5 %; the
     * engine took anything from 0, which only a hand-edited or a damaged patch
     * could reach -- and a gate of no width shuts as it opens.
     */
    Spec { param: Param::Hold, key: "hold", blob: Some("hold"), min: 0.05, max: 1.0, default: 1.0, host: percent(c"width", c"Width") },
    /* The stages are a percentage of the gate's width; see `STAGE_MAX_PCT`. */
    Spec {
        param: Param::Attack,
        key: "attack",
        blob: Some("attack"),
        min: 0.0,
        max: STAGE_MAX_PCT as f64,
        default: 1.6,
        host: host(c"attack", c"Attack", HostKind::Continuous),
    },
    Spec {
        param: Param::Decay,
        key: "decay",
        blob: Some("decay"),
        min: 0.0,
        max: STAGE_MAX_PCT as f64,
        default: 16.0,
        host: host(c"decay", c"Decay", HostKind::Continuous),
    },
    Spec { param: Param::Sustain, key: "sustain", blob: Some("sustain"), min: 0.0, max: 1.0, default: 1.0, host: percent(c"sustain", c"Sustain") },
    Spec {
        param: Param::Release,
        key: "release",
        blob: Some("release"),
        min: 0.0,
        max: STAGE_MAX_PCT as f64,
        default: 16.0,
        host: host(c"release", c"Release", HostKind::Continuous),
    },
    /* 1 IS THE DEFAULT AND HAS TO BE: Fade is how much of the pattern has
     * arrived, so zero is silence -- which a fresh instance must not be. */
    Spec { param: Param::Fade, key: "fade", blob: Some("fade"), min: 0.0, max: 1.0, default: 1.0, host: percent(c"fade", c"Fade") },
    Spec {
        param: Param::FadeSoft,
        key: "fade_soft",
        blob: Some("fsoft"),
        min: 0.0,
        max: 1.0,
        default: 0.0,
        host: words(host(c"fadeSoft", c"Fade Shape", HostKind::Toggle), HARD_SOFT),
    },
    Spec {
        param: Param::FadeDir,
        key: "fade_dir",
        blob: Some("fdir"),
        min: 0.0,
        max: 1.0,
        default: 0.0,
        host: words(host(c"fadeDir", c"Fade Dir", HostKind::Choice), IN_OUT),
    },
];

/// The table as a static, so a row can be lent out.
static TABLE: [Spec; 15] = PARAMS;

/// The row for `p`.
#[inline]
pub fn spec(p: Param) -> &'static Spec {
    &TABLE[p as usize]
}

/// `x` held to `p`'s range, as an `f32` field stores it. The comparisons are
/// written out rather than `f32::clamp` so a NaN passes through as one: every
/// door drops a NaN before it gets here, and none relies on a clamp to.
#[inline]
pub fn held(p: Param, x: f32) -> f32 {
    let s = spec(p);
    let (lo, hi) = (s.min as f32, s.max as f32);
    if x < lo {
        lo
    } else if x > hi {
        hi
    } else {
        x
    }
}

#[cfg(test)]
mod tests;
