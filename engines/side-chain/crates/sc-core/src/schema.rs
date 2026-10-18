// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
THE FIFTEEN PARAMETERS, DECLARED ONCE: Trance Gate's schema pattern
(tg-core/src/schema.rs), for the Side-Chain.

The plugin's host table (plugins/side-chain/Params.cpp) used to keep its own
copy of the rows, with a conversion of its own for the two percentages, while
the engine kept its clamps and defaults in `set_num` and `Instance::new`. This
table is now the one place each parameter's behaviour is written:

- the ENGINE clamps a number to `min..=max` and starts at `default`
  (`Instance::set_num`, `Instance::new`), and reads a word by the host's words;
- the PLUGIN builds its host table from [`Spec::host`] through the C ABI
  (`sc_core_param_spec`, `sc_core_param_option`), as the iPlug2 build
  declared it -- a project stores plain host values, so those numbers are
  frozen, and tests/fixtures/iplug2/NISideChain holds them. Depth and Vel are
  percentages at the host and 0..1 in the engine; every other number is the
  same at both.

What a surface only SHOWS -- a continuous value's text, a rate's label, a
note's name -- is the surface's.
*/

use core::ffi::CStr;

use ni_params::host::{host, percent, words, Host, HostKind};

use crate::params::{Param, LOCKOUT_MAX_MS, PARAM_COUNT, THRESHOLD_MAX_DB, THRESHOLD_MIN_DB};
use crate::{rates, DELAY_RANGE_PCT, STAGE_MAX_PCT};

/// One automatable parameter.
#[derive(Clone, Copy, Debug)]
pub struct Spec {
    pub param: Param,
    /// The text door's key (`set_param`, `get_param`), which is also the
    /// saved state's and the Move's knob key.
    pub key: &'static str,
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

    /// The host's range and default, in its plain values.
    pub fn host_min(&self) -> f64 {
        self.host.to_host(self.min)
    }
    pub fn host_max(&self) -> f64 {
        self.host.to_host(self.max)
    }
    pub fn host_default(&self) -> f64 {
        self.host.to_host(self.default)
    }
}

const SOURCES: &[&CStr] = &[c"Cycle", c"MIDI", c"Sidechain"];
/*
 * TIME IS A READING AND CHANGES NO SOUND. The four stages are percentages of
 * the cycle, always (lib.rs has the long note): a unit switch that changed
 * their meaning would give the host four parameters that do something
 * different depending on a fifth, and a host caches parameter displays. The
 * editor shows the engine's stage_ms when this says ms.
 */
const TIME_MODES: &[&CStr] = &[c"ms", c"% of cycle"];
/* Three, not four: the asymmetric Pump curve is gone. */
const CURVES: &[&CStr] = &[c"Linear", c"Exponential", c"S-Curve"];
/* Omni and the sixteen channels, as a choice, so the host's menu reads Omni. */
const CHANNELS: &[&CStr] = &[
    c"Omni", c"1", c"2", c"3", c"4", c"5", c"6", c"7", c"8", c"9", c"10", c"11", c"12", c"13", c"14", c"15", c"16",
];
const MIDI_MODES: &[&CStr] = &[c"Trigger", c"Gate"];

const fn continuous(id: &'static CStr, name: &'static CStr) -> Host {
    host(id, name, HostKind::Continuous)
}

const fn choice(id: &'static CStr, name: &'static CStr, options: &'static [&'static CStr]) -> Host {
    words(host(id, name, HostKind::Choice), options)
}

/*
 * IN `Param` ORDER, WHICH IS THE ABI AND THE HOST'S INDEX -- `spec` relies on
 * it and a test holds it. The host columns are the iPlug2 build's Declare,
 * row for row (tests/fixtures/iplug2/NISideChain/parameters.json).
 *
 * DELAY RUNS BOTH WAYS, -100..+100, AND THE NEGATIVE HALF IS THE POINT: on
 * Cycle "20 % early" is "80 % into the previous cycle", a position already
 * passed. MIDI and Sidechain have nothing periodic to anticipate, so the
 * engine clamps a negative Delay to no wait there; the parameter still travels
 * the whole range, because an automation lane may sweep through it. Attack,
 * Hold and Release run to twice the cycle, as far as a stage can go and still
 * finish before the trigger after next.
 *
 * A NOTE IS AN ADDRESS, NOT A POSITION ON A RANGE: Trigger is a choice of the
 * 128 notes by name, so a host draws "C1" and steps a semitone at a time. Its
 * names, like Rate's labels, are given at run time.
 */
pub const PARAMS: [Spec; PARAM_COUNT as usize] = [
    Spec { param: Param::Source, key: "source", min: 0.0, max: 2.0, default: 0.0, host: choice(c"source", c"Source", SOURCES) },
    Spec {
        param: Param::Rate,
        key: "rate",
        min: 0.0,
        max: (rates::RATES.len() - 1) as f64,
        default: rates::RATE_DEFAULT as f64,
        host: choice(c"rate", c"Rate", &[]),
    },
    Spec {
        param: Param::TimeMode,
        key: "time_mode",
        min: 0.0,
        max: 1.0,
        default: 0.0,
        host: choice(c"timeMode", c"Time", TIME_MODES),
    },
    Spec {
        param: Param::Delay,
        key: "delay",
        min: -DELAY_RANGE_PCT,
        max: DELAY_RANGE_PCT,
        default: 0.0,
        host: continuous(c"delay", c"Delay"),
    },
    Spec { param: Param::Attack, key: "attack", min: 0.0, max: STAGE_MAX_PCT, default: 2.0, host: continuous(c"attack", c"Attack") },
    Spec { param: Param::Hold, key: "hold", min: 0.0, max: STAGE_MAX_PCT, default: 8.0, host: continuous(c"hold", c"Hold") },
    Spec { param: Param::Release, key: "release", min: 0.0, max: STAGE_MAX_PCT, default: 35.0, host: continuous(c"release", c"Release") },
    Spec { param: Param::Depth, key: "depth", min: 0.0, max: 1.0, default: 1.0, host: percent(c"depth", c"Depth") },
    Spec { param: Param::Curve, key: "curve", min: 0.0, max: 2.0, default: 1.0, host: choice(c"curve", c"Curve", CURVES) },
    Spec { param: Param::Channel, key: "channel", min: 0.0, max: 16.0, default: 1.0, host: choice(c"channel", c"Channel", CHANNELS) },
    Spec { param: Param::Note, key: "trigger_note", min: 0.0, max: 127.0, default: 36.0, host: choice(c"note", c"Trigger", &[]) },
    Spec { param: Param::MidiMode, key: "midi_mode", min: 0.0, max: 1.0, default: 0.0, host: choice(c"midiMode", c"Mode", MIDI_MODES) },
    Spec { param: Param::VelSens, key: "vel_sens", min: 0.0, max: 1.0, default: 0.0, host: percent(c"velSens", c"Vel") },
    Spec {
        param: Param::Threshold,
        key: "threshold",
        min: THRESHOLD_MIN_DB,
        max: THRESHOLD_MAX_DB,
        default: -24.0,
        host: continuous(c"threshold", c"Threshold"),
    },
    Spec { param: Param::Lockout, key: "lockout", min: 0.0, max: LOCKOUT_MAX_MS, default: 20.0, host: continuous(c"lockout", c"Lockout") },
];

/// The table as a static, so a row can be lent out.
static TABLE: [Spec; PARAM_COUNT as usize] = PARAMS;

/// The row for `p`.
#[inline]
pub fn spec(p: Param) -> &'static Spec {
    &TABLE[p as usize]
}

#[cfg(test)]
mod tests;
