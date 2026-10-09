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
 * engine's table says; the text is bc_param_text, and typing is read by
 * bc_param_parse -- which refuses text with no number in it, and then the
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
                  [] (float, float, float v) { return v; }),
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
    return std::make_unique<EngineFloat> (index, key, name, info);
}

bool monoOrStereo (const juce::AudioChannelSet& set)
{
    return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
}
} // namespace

BitCrusher::BitCrusher()
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
    }
}

BitCrusher::~BitCrusher() = default;

bool BitCrusher::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto in = layout.getMainInputChannelSet();
    return monoOrStereo (in) && layout.getMainOutputChannelSet() == in;
}

void BitCrusher::prepareToPlay (double sampleRate, int)
{
    const double rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    bc_shell_post_sample_rate (shell.get(), rate);
    /* A host that deactivated the plugin and starts it again hears nothing
     * held from before: the held sample and the filters' tails let go. */
    bc_shell_post_reset (shell.get());
    beat.prepare (rate);
}

double BitCrusher::plain (int index) const
{
    const auto* p = params[(size_t) index];
    return (double) p->convertFrom0to1 (p->getValue());
}

void BitCrusher::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int frames = buffer.getNumSamples();
    beat.tick (readClock (getPlayHead()), frames);
    if (frames <= 0 || buffer.getNumChannels() < 1)
        return;

    BcCore* core = bc_shell_begin (shell.get());
    for (int i = 0; i < paramCount; ++i)
        bc_core_set_num (core, i, plain (i));
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    bc_core_process_f32 (core, left, right, (uint32_t) frames);
    bc_shell_end (shell.get(), (uint32_t) frames);
}

void BitCrusher::processBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    beat.tick (readClock (getPlayHead()), buffer.getNumSamples());
    ni::Processor::processBypassed (buffer, midi);
}

/* ------------------------------------------------------------ the model -- */

int BitCrusher::takeRings (float* strengths, int capacity)
{
    return beat.takeRings (strengths, capacity);
}

bool BitCrusher::motion() const
{
    return ni::MachineSettings::motion ("bit-crusher");
}

void BitCrusher::setMotion (bool on)
{
    ni::MachineSettings::setMotion ("bit-crusher", on);
}

bool BitCrusher::bipolar (int index) const
{
    BcParamInfo info {};
    return bc_param_info (index, &info) && info.bipolar;
}

void BitCrusher::steps (float* out, int count)
{
    if (out == nullptr || count <= 0)
        return;
    bc_transfer_render (plain (param::drive), plain (param::bits), plain (param::shape), plain (param::midRise) >= 0.5,
                        out, (uint32_t) count);
}

const Scope& BitCrusher::scope()
{
    if (! bc_shell_read (shell.get(), published.get()) || published->serial == latest.serial)
        return latest;
    latest.serial = published->serial;
    latest.triggered = published->triggered;
    latest.sampleRate = published->sample_rate;
    latest.dry.assign (published->dry, published->dry + BC_SCOPE);
    latest.wet.assign (published->wet, published->wet + BC_SCOPE);
    return latest;
}

juce::AudioProcessorEditor* BitCrusher::createEditor()
{
    return new ni::PluginEditor (*this, std::make_unique<Editor> (*this), ni::PluginEditor::FollowDesign {});
}

void BitCrusher::editorOpened()
{
    beat.setActive (true);
}

void BitCrusher::editorClosed()
{
    beat.setActive (false);
}

} // namespace ni::bc

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ni::bc::BitCrusher();
}
