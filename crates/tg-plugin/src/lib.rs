/*!
Trance Gate — the Ableton Live plugin.

Copyright (C) 2026 Torben Graeber. **GPL-3.0-or-later**: this program is free
software, redistributable and modifiable under the GNU General Public License
as published by the Free Software Foundation, either version 3 or (at your
option) any later version, and distributed WITHOUT ANY WARRANTY. See `LICENSE`
at the repository root.

The licence is not a preference. nih-plug's VST3 bindings are GPLv3, so the
binary this crate produces has to be.

The DSP is [`tg_core`], the same crate the Schwung module on the Move builds
into its `.so`, pinned here by submodule commit. This crate is the host side of
it: twelve automatable parameters, the transport, and the patch.

# What changed when this stopped being JUCE

The engine used to be reached through its C ABI even here — `tg_core_set_num`
with an integer parameter wire, `tg_core_get_param` formatting numbers into a
buffer for the editor to parse back, and a lock the audio thread took for every
block so the message thread could read a string out from under it. None of that
survives: the engine is a Rust struct with public fields, so a value goes in as
a value and comes out as one.

# Parameters are the source of truth

A patch has two halves and they are kept in different places on purpose.

The twelve values with a host parameter behind them — rate, length, slot,
legato, time mode, curve, amount, width, and the four envelope stages — belong
to the *host*. It owns them, automates them, and restores them. Every block
pushes them into the engine, so nothing else can win an argument with an
automation lane.

Everything else — the pattern, the ties, the per-step depths, all eight slots —
has no host parameter and lives in [`TranceGateParams::patch`] as the engine's
own state blob. That blob is the same text the Move module writes, which is
what makes Copy/Paste gate config work between the two.
*/

use nih_plug::params::persist::PersistentField;
use nih_plug::prelude::*;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use tg_core::params::Param as TgParam;
use tg_core::{rates, Instance, Transport as TgTransport, MAX_STEPS, SLOTS, STAGE_MAX_PCT};

/// The engine's own default rate index — 1/16. Read from the engine rather
/// than written down, so the two cannot disagree.
const RATE_DEFAULT: i32 = rates::RATE_DEFAULT as i32;

/// The patch text, plus a flag saying it has been written since the audio
/// thread last looked.
///
/// A plain `Arc<Mutex<String>>` is a `PersistentField` already, and it would
/// have been enough to *store* the patch. It is not enough to *notice* one
/// arriving: the host deserialises straight into the field, and nothing tells
/// the plugin it happened. This wrapper sets a flag when written, which
/// `initialize` reads.
#[derive(Default)]
struct Patch {
    text: Mutex<String>,
    dirty: AtomicBool,
}

impl<'a> PersistentField<'a, String> for Patch {
    fn set(&self, new_value: String) {
        *self.text.lock().unwrap() = new_value;
        self.dirty.store(true, Ordering::Release);
    }

    fn map<F, R>(&self, f: F) -> R
    where
        F: Fn(&String) -> R,
    {
        f(&self.text.lock().unwrap())
    }
}

#[derive(Params)]
struct TranceGateParams {
    /*
     * THE IDS ARE THE ONES THE JUCE BUILD PUBLISHED, and they are not free to
     * change: a host that saved an automation lane saved these strings. Note
     * `hold` for the control the UI calls Width -- it is the engine's name for
     * how much of a step the gate stays open, and renaming it here would break
     * every session that automated it.
     */
    #[id = "rate"]
    rate: IntParam,
    #[id = "length"]
    length: IntParam,
    #[id = "slot"]
    slot: IntParam,
    #[id = "legato"]
    legato: BoolParam,
    #[id = "time_mode"]
    time_mode: IntParam,
    #[id = "curve"]
    curve: IntParam,
    #[id = "amount"]
    amount: FloatParam,
    #[id = "hold"]
    hold: FloatParam,
    #[id = "attack"]
    attack: FloatParam,
    #[id = "decay"]
    decay: FloatParam,
    #[id = "sustain"]
    sustain: FloatParam,
    #[id = "release"]
    release: FloatParam,

    /// The half of the patch with no host parameter behind it: the pattern,
    /// the ties, the per-step depths, all eight slots. The engine's own state
    /// blob, byte-for-byte what the Move module writes.
    ///
    /// THIS TEXT IS THE AUTHORITY, NOT THE ENGINE'S COPY OF IT. The engine is
    /// loaded *from* here and never serialised *back*, which is what keeps the
    /// save path off the audio thread entirely -- `state::save` allocates, and
    /// the only thread that can see the engine is the one that must not.
    ///
    /// Nothing mutates the pattern yet, so the two cannot disagree. When the
    /// editor lands it becomes the rule rather than an accident: a pad edit
    /// writes this text on the message thread and sends the same edit to the
    /// audio thread, so what gets saved is what the editor drew.
    #[persist = "patch"]
    patch: Patch,
}

/// `%.1f %%`, for the values that are fractions shown as percentages.
fn pct() -> Arc<dyn Fn(f32) -> String + Send + Sync> {
    Arc::new(|v| format!("{:.1} %", v * 100.0))
}

fn pct_back() -> Arc<dyn Fn(&str) -> Option<f32> + Send + Sync> {
    Arc::new(|s| {
        s.trim().trim_end_matches('%').trim().parse::<f32>().ok().map(|v| v / 100.0)
    })
}

impl Default for TranceGateParams {
    fn default() -> Self {
        /*
         * A STAGE RUNS FROM 0 TO TWICE THE GATE'S WIDTH, and the stored number
         * IS the percentage -- so "%" is a straight readout and "ms" is
         * `value / 100 * width_ms`. Two readings of one number rather than two
         * modes, which is why changing Env Time moves no knob.
         *
         * The stage knobs therefore print a bare percentage here. Milliseconds
         * need the width, which needs the rate and the tempo, and a parameter's
         * formatter has none of those; the editor prints ms where it can see
         * them.
         */
        let stage = |name: &str, default: f32| {
            FloatParam::new(
                name,
                default,
                FloatRange::Linear { min: 0.0, max: STAGE_MAX_PCT },
            )
            .with_unit(" %")
            .with_value_to_string(Arc::new(|v| format!("{v:.2}")))
            .with_string_to_value(Arc::new(|s| s.trim().trim_end_matches('%').trim().parse().ok()))
        };

        Self {
            /*
             * RATE AND LENGTH AND SLOT ARE DISCRETE. An IntParam publishes a
             * step count, so a host draws and automates it as the ladder it is
             * instead of a sweep through values nobody can land on. The JUCE
             * build needed a subclass to get this; here it is the default.
             */
            rate: IntParam::new(
                "Rate",
                RATE_DEFAULT,
                IntRange::Linear { min: 0, max: rates::RATES.len() as i32 - 1 },
            )
            // Straight out of the engine's table, so the ladder cannot drift
            // from the one the DSP actually uses.
            .with_value_to_string(Arc::new(|i| {
                rates::RATES
                    .get(i as usize)
                    .map(|r| r.label.to_string())
                    .unwrap_or_default()
            }))
            .with_string_to_value(Arc::new(|s| {
                rates::RATES.iter().position(|r| r.label == s.trim()).map(|i| i as i32)
            })),

            length: IntParam::new(
                "Length",
                16,
                IntRange::Linear { min: 1, max: MAX_STEPS as i32 },
            )
            .with_unit(" steps"),

            /* A SELECTION, NOT A QUANTITY. "Slot 4.5" is not a half-way
             * pattern, it is nothing. Displayed 1..8; the engine's wire is the
             * 0-based index. */
            slot: IntParam::new(
                "Slot",
                1,
                IntRange::Linear { min: 1, max: SLOTS as i32 },
            ),

            legato: BoolParam::new("Join Neighbors", false),

            /* What the envelope's times MEAN. In "% Step" a stage is a share
             * of the gate's width rather than of a second, so a patch built at
             * 1/16 still sounds like itself at 1/128 instead of being cut off. */
            time_mode: IntParam::new("Env Time", 0, IntRange::Linear { min: 0, max: 1 })
                .with_value_to_string(Arc::new(|i| {
                    (if i == 0 { "ms" } else { "% Step" }).to_string()
                }))
                .with_string_to_value(Arc::new(|s| Some(i32::from(s.trim() != "ms")))),

            /* Every shape starts where it started and ends where it ended and
             * still lasts as long -- so this changes the feel of the gate
             * without moving any time on any knob. */
            curve: IntParam::new("Env Curve", 0, IntRange::Linear { min: 0, max: 2 })
                .with_value_to_string(Arc::new(|i| {
                    match i {
                        1 => "Exponential",
                        2 => "S-Curve",
                        _ => "Linear",
                    }
                    .to_string()
                }))
                .with_string_to_value(Arc::new(|s| {
                    Some(match s.trim() {
                        "Exponential" | "Exp" => 1,
                        "S-Curve" | "S" => 2,
                        _ => 0,
                    })
                })),

            amount: FloatParam::new("Amount", 1.0, FloatRange::Linear { min: 0.0, max: 1.0 })
                .with_value_to_string(pct())
                .with_string_to_value(pct_back()),

            /* WIDTH, and its id is `hold`. The engine's name for how much of a
             * step the gate stays open; the id is what a saved automation lane
             * carries, so it stays. The floor is 0.05 rather than 0 because a
             * gate that never opens is silence, not a setting. */
            hold: FloatParam::new("Width", 1.0, FloatRange::Linear { min: 0.05, max: 1.0 })
                .with_value_to_string(pct())
                .with_string_to_value(pct_back()),

            attack: stage("Attack", 1.6),
            decay: stage("Decay", 16.0),
            sustain: FloatParam::new("Sustain", 1.0, FloatRange::Linear { min: 0.0, max: 1.0 })
                .with_value_to_string(pct())
                .with_string_to_value(pct_back()),
            release: stage("Release", 16.0),

            patch: Patch::default(),
        }
    }
}

struct TranceGate {
    params: Arc<TranceGateParams>,
    engine: Instance,
}

impl Default for TranceGate {
    fn default() -> Self {
        Self {
            params: Arc::new(TranceGateParams::default()),
            /* Replaced in `initialize` with the host's real rate. 44100 here
             * only so the struct is whole before a host has said anything. */
            engine: Instance::new(44_100.0),
        }
    }
}

impl TranceGate {
    /// Push the host's twelve values into the engine.
    ///
    /// Unconditionally, every block, rather than on change. `set_num` is a
    /// match, a clamp and a store, and the change detection that does matter —
    /// the curve's re-anchor, which keeps a mid-gate shape change from
    /// clicking — already lives inside the engine where both callers get it.
    /// A dirty-mask here would be a second, weaker copy of that.
    #[inline]
    fn push_params(&mut self) {
        let p = &self.params;
        let e = &mut self.engine;
        e.set_num(TgParam::Rate, p.rate.value() as f64);
        /* Both are INDICES on the wire and 1-based on the knob: Length's
         * index 15 is the option named "16". */
        e.set_num(TgParam::Length, (p.length.value() - 1) as f64);
        e.set_num(TgParam::Slot, (p.slot.value() - 1) as f64);
        e.set_num(TgParam::Legato, p.legato.value() as i32 as f64);
        e.set_num(TgParam::TimeMode, p.time_mode.value() as f64);
        e.set_num(TgParam::Curve, p.curve.value() as f64);
        e.set_num(TgParam::Amount, p.amount.value() as f64);
        e.set_num(TgParam::Hold, p.hold.value() as f64);
        e.set_num(TgParam::Attack, p.attack.value() as f64);
        e.set_num(TgParam::Decay, p.decay.value() as f64);
        e.set_num(TgParam::Sustain, p.sustain.value() as f64);
        e.set_num(TgParam::Release, p.release.value() as f64);
    }

    /// Apply a restored patch blob, if one arrived since the last look.
    ///
    /// The blob carries the twelve parameter values too, and they lose: the
    /// very next block overwrites them from the host. That is not a conflict
    /// in the case that matters, because the host saved both halves together
    /// and restores them together, so they already agree. It IS a conflict
    /// when a patch is PASTED, and that is handled where the paste happens —
    /// the editor sets the twelve parameters through the host rather than
    /// letting the blob set them behind its back.
    fn take_patch(&mut self) {
        if self.params.patch.dirty.swap(false, Ordering::Acquire) {
            let text = self.params.patch.text.lock().unwrap().clone();
            if !text.is_empty() {
                tg_core::state::load(&mut self.engine, &text);
            }
        }
    }
}

impl Plugin for TranceGate {
    /*
     * TEMPORARY, AND IT GOES WHEN JUCE DOES.
     *
     * Both builds are installed side by side while this one is brought up to
     * parity, and a host lists them by NAME -- two entries called "Trance
     * Gate" is not an A/B, it is a coin toss. The identity a session actually
     * binds to is VST3_CLASS_ID and CLAP_ID below, and neither carries the
     * suffix, so dropping it later renames the plugin without orphaning
     * anything saved with it.
     */
    const NAME: &'static str = "Trance Gate (Rust)";
    const VENDOR: &'static str = "graebe";
    const URL: &'static str = "https://github.com/graebe/schwung-trance-gate";
    const EMAIL: &'static str = "";
    const VERSION: &'static str = env!("CARGO_PKG_VERSION");

    const AUDIO_IO_LAYOUTS: &'static [AudioIOLayout] = &[AudioIOLayout {
        main_input_channels: NonZeroU32::new(2),
        main_output_channels: NonZeroU32::new(2),
        ..AudioIOLayout::const_default()
    }];

    const SAMPLE_ACCURATE_AUTOMATION: bool = false;

    type SysExMessage = ();
    type BackgroundTask = ();

    fn params(&self) -> Arc<dyn Params> {
        self.params.clone()
    }

    fn initialize(
        &mut self,
        _layout: &AudioIOLayout,
        buffer_config: &BufferConfig,
        _context: &mut impl InitContext<Self>,
    ) -> bool {
        /*
         * A NEW ENGINE RATHER THAN A SAMPLE-RATE SETTER.
         *
         * `initialize` is also where a restored state has landed by the time
         * it runs, and it may run several times in a row while a host settles.
         * Building a fresh engine makes every one of those identical instead
         * of depending on what the previous one was left holding.
         */
        self.engine = Instance::new(buffer_config.sample_rate as f64);
        self.params.patch.dirty.store(true, Ordering::Release);
        self.take_patch();
        self.push_params();
        true
    }

    fn reset(&mut self) {
        /* Transport-derived state only. The patch and the parameters survive a
         * reset -- a host stopping and starting is not a new patch. */
        self.engine.last_step = None;
        self.engine.was_running = false;
        self.engine.advancing = false;
    }

    fn process(
        &mut self,
        buffer: &mut Buffer,
        _aux: &mut AuxiliaryBuffers,
        context: &mut impl ProcessContext<Self>,
    ) -> ProcessStatus {
        self.take_patch();
        self.push_params();

        /*
         * THE TRANSPORT, TRANSLATED.
         *
         * Two fields matter and they are the ones the engine's PLL locks to:
         * whether we are playing at all, and the song position in BEATS.
         * `pos_beats` is already in beats, so there is no conversion and none
         * should be invented.
         *
         * A host with no position (an offline render, some validators) reports
         * nothing. That is "stopped", which the engine reads as "hold the gate
         * open" -- the dry signal passes, which is the right failure.
         */
        let t = context.transport();
        let mut tg = TgTransport { running: false, beats: -1.0, bpm: 120.0 };
        if let Some(bpm) = t.tempo {
            tg.bpm = bpm as f32;
        }
        if let (true, Some(beats)) = (t.playing, t.pos_beats()) {
            tg.running = true;
            tg.beats = beats;
        }

        let frames = buffer.samples();
        let slices = buffer.as_slice();
        let (left, rest) = slices.split_at_mut(1);
        let l = &mut left[0];
        /* Mono in a stereo layout is not a case a host should produce, but a
         * validator will try it. */
        let r: &mut [f32] = match rest.first_mut() {
            Some(r) => r,
            None => {
                self.engine.process_f32(l, frames, Some(&tg));
                return ProcessStatus::Normal;
            }
        };

        self.engine.process_f32_split(l, r, frames, Some(&tg));

        ProcessStatus::Normal
    }
}

impl ClapPlugin for TranceGate {
    const CLAP_ID: &'static str = "com.graebe.trance-gate";
    const CLAP_DESCRIPTION: Option<&'static str> =
        Some("Tempo-locked step gate with a per-step ADSR, ties and eight pattern slots");
    const CLAP_MANUAL_URL: Option<&'static str> = Some(Self::URL);
    const CLAP_SUPPORT_URL: Option<&'static str> = None;
    const CLAP_FEATURES: &'static [ClapFeature] = &[
        ClapFeature::AudioEffect,
        ClapFeature::Stereo,
        ClapFeature::Utility,
    ];
}

impl Vst3Plugin for TranceGate {
    /*
     * A NEW CLASS ID, AND DELIBERATELY NOT THE JUCE ONE.
     *
     * The JUCE build published ABCDEF019182FAEB4772626554724774 (its scheme;
     * the tail is ASCII "GrbeTrGt"). Reusing it would let an existing session
     * find this plugin and then restore nothing usable, because the state here
     * is nih-plug's format and not an APVTS value tree -- and a patch silently
     * loading as defaults is worse than a plugin the host reports as missing.
     *
     * The migration path needs no code: Copy/Paste gate config moves a patch
     * between the two builds as the engine's own state blob, which is the same
     * text in both because it is the same engine.
     */
    const VST3_CLASS_ID: [u8; 16] = *b"GraebeTranceGate";
    const VST3_SUBCATEGORIES: &'static [Vst3SubCategory] =
        &[Vst3SubCategory::Fx, Vst3SubCategory::Modulation];
}

nih_export_clap!(TranceGate);
nih_export_vst3!(TranceGate);
