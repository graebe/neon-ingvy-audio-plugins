// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Bit-Crusher's shell. BitCrusher.h says what it does.
 */
#include "BitCrusher.h"

#include "Editor.h"
#include "MachineSettings.h"
#include "PluginEditor.h"

#include <cmath>
#include <limits>

namespace ni::bc
{

namespace
{
/* One of the engine's texts for a parameter: bc_param_key, _name or _text. */
template <typename Read>
juce::String engineText (Read read)
{
    char buf[64] {};
    return read (buf, (int) sizeof buf) >= 0 ? juce::String::fromUTF8 (buf) : juce::String();
}

/*
 * A NUMBER ON THE ENGINE'S TRAVEL AND IN ITS WORDS. The range maps through
 * bc_param_normalise and _denormalise, so Rate turns in equal ratios as the
 * engine's table says, and lands on the table's resolution (bc_param_snap):
 * a value a host keeps as a float comes back as the number that was set --
 * 5.5 bits, not 5.50000000447. The text is bc_param_text, and typing is read
 * by bc_param_parse -- which refuses text with no number in it, and then the
 * value stays where it is rather than going to the bottom of the range.
 */
class EngineFloat final : public juce::AudioParameterFloat
{
public:
    EngineFloat (int i, const juce::String& key, const juce::String& title, const BcParamInfo& info)
        : juce::AudioParameterFloat (
              juce::ParameterID { key, 1 }, title,
              juce::NormalisableRange<float> (
                  (float) info.min, (float) info.max,
                  [i] (float, float, float n) { return (float) bc_param_denormalise (i, (double) n); },
                  [i] (float, float, float v) { return (float) bc_param_normalise (i, (double) v); },
                  [i] (float, float, float v) { return (float) bc_param_snap (i, (double) v); }),
              (float) info.default_value,
              juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                  [i] (float v, int) { return engineText ([i, v] (char* b, int n) { return bc_param_text (i, (double) v, b, n); }); })),
          index (i)
    {
    }

    float getValueForText (const juce::String& text) const override
    {
        double v = 0.0;
        if (! bc_param_parse (index, text.toRawUTF8(), &v))
            return convertTo0to1 (get());
        return convertTo0to1 ((float) v);
    }

private:
    const int index;
};

std::unique_ptr<juce::RangedAudioParameter> fromEngine (int index)
{
    BcParamInfo info {};
    const bool known = bc_param_info (index, &info);
    jassert (known);
    juce::ignoreUnused (known);

    const auto key = engineText ([index] (char* b, int n) { return bc_param_key (index, b, n); });
    const auto name = engineText ([index] (char* b, int n) { return bc_param_name (index, b, n); });
    if (info.kind == BcParamKind_Toggle)
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { key, 1 }, name, info.default_value >= 0.5);
    /* A choice in the engine's words; one of the window's (Refresh) is saved
     * with the set and not offered for automation. */
    if (info.kind == BcParamKind_Choice)
    {
        juce::StringArray choices;
        for (int c = 0; c < info.choice_count; ++c)
            choices.add (engineText ([index, c] (char* b, int n) { return bc_param_choice (index, c, b, n); }));
        return std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { key, 1 }, name, choices, (int) info.default_value,
            juce::AudioParameterChoiceAttributes().withAutomatable (info.automatable));
    }
    return std::make_unique<EngineFloat> (index, key, name, info);
}

bool monoOrStereo (const juce::AudioChannelSet& set)
{
    return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
}
} // namespace

Processor::Processor()
    : ni::Processor (BusesProperties()
                         .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      shell (bc_shell_create (44100.0)),
      published (std::make_unique<BcScope>())
{
    jassert (bc_param_count() == paramCount);
    for (int i = 0; i < paramCount; ++i)
    {
        auto p = fromEngine (i);
        params[(size_t) i] = p.get();
        addParameter (p.release());
        BcParamInfo info {};
        /* Only a number glides across a block; a switch or a choice jumps. */
        toggles[(size_t) i] = bc_param_info (i, &info) && info.kind != BcParamKind_Float;
    }
    /* Constant: Anti-Alias on or off, at any rate (bc_latency). */
    setLatencySamples ((int) bc_latency());
}

Processor::~Processor() = default;

bool Processor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto in = layout.getMainInputChannelSet();
    return monoOrStereo (in) && layout.getMainOutputChannelSet() == in;
}

void Processor::prepare (double sampleRate, int)
{
    const double rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    bc_shell_post_sample_rate (shell.get(), rate);
    /* A host that deactivated the plugin and starts it again hears nothing
     * held from before: the held sample and the filters' tails let go. */
    bc_shell_post_reset (shell.get());
    beat.prepare (rate);
    ramping = false;
}

/* Snapped again in double: the float a parameter holds is the nearest to a
 * value on the resolution, and this is that value -- 12345.6 Hz, not the
 * float beside it. */
double Processor::plain (int index) const
{
    const auto* p = params[(size_t) index];
    return bc_param_snap (index, (double) p->convertFrom0to1 (p->getValue()));
}

/* Parameter `index` a fraction `t` of the way from `from` to `to`: along
 * its knob's travel, so Rate ramps in equal ratios; a switch at once (the
 * engine crossfades it). */
double Processor::between (int index, double from, double to, double t) const
{
    if (toggles[(size_t) index] || juce::exactlyEqual (from, to) || t >= 1.0)
        return to;
    const double a = bc_param_normalise (index, from), b = bc_param_normalise (index, to);
    return bc_param_denormalise (index, a + (b - a) * t);
}

/*
 * ONE BLOCK, heard or bypassed. Every parameter is pushed in steps of
 * rampFrames, ramped from where the last block left it to where the host has
 * it now -- a host that sends 8192 frames at a time and automates Rate would
 * otherwise move it in stairs 170 ms apart. The first block after
 * prepareToPlay starts where the host is, with no ramp from a stale block.
 */
void Processor::run (juce::AudioBuffer<float>& buffer, bool heard)
{
    const int frames = buffer.getNumSamples();
    if (frames <= 0 || buffer.getNumChannels() < 1)
        return;
    for (int i = 0; i < paramCount; ++i)
        target[(size_t) i] = plain (i);
    if (! ramping)
    {
        last = target;
        ramping = true;
    }

    BcCore* core = bc_shell_begin (shell.get());
    bc_core_set_active (core, heard);
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    const int steps = (frames + rampFrames - 1) / rampFrames;
    for (int k = 0; k < steps; ++k)
    {
        const int at = k * rampFrames;
        const int n = juce::jmin (rampFrames, frames - at);
        const double t = (double) (k + 1) / (double) steps;
        for (int i = 0; i < paramCount; ++i)
            bc_core_set_num (core, i, between (i, last[(size_t) i], target[(size_t) i], t));
        bc_core_process_f32 (core, left + at, right != nullptr ? right + at : nullptr, (uint32_t) n);
    }
    last = target;
    bc_shell_end (shell.get(), (uint32_t) frames);
}

void Processor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int frames = buffer.getNumSamples();
    const auto clock = readClock (getPlayHead());
    beat.tick (clock, frames);
    run (buffer, true);
}

/* Bypassed, the engine fades to the input, delayed by the latency the host
 * compensates for -- JUCE's own bypass would pass it on time, and the track
 * would jump ahead by that much -- and back from silence on the way out. */
void Processor::processBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const auto clock = readClock (getPlayHead());
    beat.tick (clock, buffer.getNumSamples());
    run (buffer, false);
}

/* ------------------------------------------------------------ the model -- */

int Processor::takeRings (float* strengths, int capacity)
{
    return beat.takeRings (strengths, capacity);
}

bool Processor::motion() const
{
    return ni::MachineSettings::motion ("bit-crusher");
}

void Processor::setMotion (bool on)
{
    ni::MachineSettings::setMotion ("bit-crusher", on);
}

bool Processor::bipolar (int index) const
{
    BcParamInfo info {};
    return bc_param_info (index, &info) && info.bipolar;
}

/* On the quantiser's own input, after Drive: Drive moves the signal up the
 * staircase, never the staircase off the diagonal. */
void Processor::steps (float* out, int count)
{
    if (out != nullptr && count > 0)
        bc_transfer_render (0.0, plain (param::bits), plain (param::shape), plain (param::midRise) >= 0.5, out,
                            (uint32_t) count);
}

const Scope& Processor::scope()
{
    if (! bc_shell_read (shell.get(), published.get()) || published->serial == latest.serial)
        return latest;
    latest.serial = published->serial;
    latest.triggered = published->triggered;
    latest.sampleRate = published->sample_rate;
    latest.dry.assign (published->dry, published->dry + BC_SCOPE);
    latest.wet.assign (published->wet, published->wet + BC_SCOPE);
    latest.ampIn.assign (published->amp_in, published->amp_in + BC_SCOPE);
    latest.ampOut.assign (published->amp_out, published->amp_out + BC_SCOPE);
    const auto holdLen = (size_t) juce::jmin ((int) published->hold_len, (int) BC_HOLD);
    latest.holdIn.assign (published->hold_in, published->hold_in + holdLen);
    latest.holdTook.assign (published->hold_took, published->hold_took + holdLen);
    latest.holdAt.assign (published->hold_at, published->hold_at + holdLen);
    latest.holdTakes = (int) published->hold_takes;
    latest.settled = published->settled;
    latest.settings = {};
    latest.settings[param::drive] = published->drive_db;
    latest.settings[param::bits] = published->bits;
    latest.settings[param::shape] = published->shape;
    latest.settings[param::midRise] = published->mid_rise;
    latest.settings[param::dither] = published->dither;
    latest.settings[param::antiAlias] = published->anti_alias;
    latest.settings[param::rate] = published->rate;
    latest.settings[param::jitter] = published->jitter;
    latest.settings[param::preFilter] = published->pre_filter;
    return latest;
}

/* At the host's rate, or 48 kHz before there is one: the curve near Nyquist
 * is where the two differ. */
double Processor::filterRate() const
{
    return getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
}

void Processor::filterResponse (float* outDb, int count, double loHz, double hiHz) const
{
    if (outDb != nullptr && count > 0)
        bc_filter_response (plain (param::cutoff), plain (param::peak), (int) plain (param::slope), filterRate(), loHz,
                            hiHz, outDb, (uint32_t) count);
}

float Processor::filterGain (double hz) const
{
    return bc_filter_gain (plain (param::cutoff), plain (param::peak), (int) plain (param::slope), filterRate(), hz);
}

int Processor::levelCount() const
{
    return (int) bc_level_count (plain (param::bits), plain (param::midRise) >= 0.5);
}

double Processor::refreshPeriodMs() const
{
    return bc_refresh_period_ms ((int) plain (param::refresh));
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new ni::PluginEditor (*this, std::make_unique<Editor> (*this), ni::PluginEditor::FollowDesign {});
}

void Processor::editorOpened()
{
    beat.setActive (true);
}

void Processor::editorClosed()
{
    beat.setActive (false);
}

} // namespace ni::bc

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ni::bc::Processor();
}
