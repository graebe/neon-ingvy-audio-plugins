// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Vocalize's shell. Vocalize.h says what it does.
 */
#include "Vocalize.h"

#include "Editor.h"
#include "MachineSettings.h"
#include "PluginEditor.h"

#include <cmath>

namespace ni::vz
{

namespace
{
/* One of the engine's texts for a parameter: vz_param_key, _name or _text. */
template <typename Read>
juce::String engineText (Read read)
{
    char buf[64] {};
    return read (buf, (int) sizeof buf) >= 0 ? juce::String::fromUTF8 (buf) : juce::String();
}

/*
 * A NUMBER ON THE ENGINE'S TRAVEL AND IN ITS WORDS. The range maps through
 * vz_param_normalise and _denormalise and lands on the table's resolution
 * (vz_param_snap): a value a host keeps as a float comes back as the number
 * that was set -- 1.3, not 1.29999995. The text is vz_param_text ("E>I 30 %"),
 * and typing is read by vz_param_parse -- which takes a vowel's letter, and
 * refuses text with no value in it, and then the value stays where it is.
 */
class EngineFloat final : public juce::AudioParameterFloat
{
public:
    EngineFloat (int i, const juce::String& key, const juce::String& title, const VzParamInfo& info)
        : juce::AudioParameterFloat (
              juce::ParameterID { key, 1 }, title,
              juce::NormalisableRange<float> (
                  (float) info.min, (float) info.max,
                  [i] (float, float, float n) { return (float) vz_param_denormalise (i, (double) n); },
                  [i] (float, float, float v) { return (float) vz_param_normalise (i, (double) v); },
                  [i] (float, float, float v) { return (float) vz_param_snap (i, (double) v); }),
              (float) info.default_value,
              juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                  [i] (float v, int) { return engineText ([i, v] (char* b, int n) { return vz_param_text (i, (double) v, b, n); }); })),
          index (i)
    {
    }

    float getValueForText (const juce::String& text) const override
    {
        double v = 0.0;
        if (! vz_param_parse (index, text.toRawUTF8(), &v))
            return convertTo0to1 (get());
        return convertTo0to1 ((float) v);
    }

private:
    const int index;
};

std::unique_ptr<juce::RangedAudioParameter> fromEngine (int index)
{
    VzParamInfo info {};
    const bool known = vz_param_info (index, &info);
    jassert (known);
    juce::ignoreUnused (known);

    const auto key = engineText ([index] (char* b, int n) { return vz_param_key (index, b, n); });
    const auto name = engineText ([index] (char* b, int n) { return vz_param_name (index, b, n); });
    if (info.kind == VzParamKind_Choice)
    {
        /* A choice's labels are the engine's; switching engines changes the
         * latency, which a host takes note of between blocks, so it is not
         * offered for automation. */
        juce::StringArray labels;
        for (int option = 0; option <= (int) info.max; ++option)
            labels.add (engineText ([index, option] (char* b, int n) { return vz_param_label (index, option, b, n); }));
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { key, 1 }, name, labels,
                                                             (int) info.default_value,
                                                             juce::AudioParameterChoiceAttributes().withAutomatable (false));
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
      shell (vz_shell_create (44100.0)),
      published (std::make_unique<VzPicture>())
{
    jassert (vz_param_count() == paramCount);
    for (int i = 0; i < paramCount; ++i)
    {
        auto p = fromEngine (i);
        params[(size_t) i] = p.get();
        addParameter (p.release());
    }
    params[(size_t) param::mode]->addListener (this);
    setLatencySamples (latencyForMode());
}

Processor::~Processor()
{
    params[(size_t) param::mode]->removeListener (this);
    cancelPendingUpdate();
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto in = layout.getMainInputChannelSet();
    return monoOrStereo (in) && layout.getMainOutputChannelSet() == in;
}

void Processor::prepareToPlay (double sampleRate, int)
{
    const double r = sampleRate > 0.0 ? sampleRate : 44100.0;
    rate.store (r);
    vz_shell_post_sample_rate (shell.get(), r);
    /* A host that deactivated the plugin and starts it again hears nothing
     * held from before: the history and the resonators let go. */
    vz_shell_post_reset (shell.get());
    setLatencySamples (latencyForMode());
    beat.prepare (r);
}

/* Snapped again in double: the float a parameter holds is the nearest to a
 * value on the resolution, and this is that value. */
double Processor::plain (int index) const
{
    const auto* p = params[(size_t) index];
    return vz_param_snap (index, (double) p->convertFrom0to1 (p->getValue()));
}

int Processor::latencyForMode() const
{
    return (int) vz_latency (plain (param::mode), rate.load());
}

/* Any thread, the audio one included: only a request for the message
 * thread, which allocates nothing. */
void Processor::parameterValueChanged (int, float)
{
    triggerAsyncUpdate();
}

void Processor::handleAsyncUpdate()
{
    setLatencySamples (latencyForMode());
}

void Processor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int frames = buffer.getNumSamples();
    const auto clock = readClock (getPlayHead());
    beat.tick (clock, frames);
    if (frames <= 0 || buffer.getNumChannels() < 1)
        return;

    VzCore* core = vz_shell_begin (shell.get());
    for (int i = 0; i < paramCount; ++i)
        vz_core_set_num (core, i, plain (i));
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    vz_core_process_f32 (core, left, right, (uint32_t) frames);
    vz_shell_end (shell.get(), (uint32_t) frames);
}

void Processor::processBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    beat.tick (readClock (getPlayHead()), buffer.getNumSamples());
    ni::Processor::processBypassed (buffer, midi);
}

/* ------------------------------------------------------------ the model -- */

int Processor::takeRings (float* strengths, int capacity)
{
    return beat.takeRings (strengths, capacity);
}

bool Processor::motion() const
{
    return ni::MachineSettings::motion ("vocalize");
}

void Processor::setMotion (bool on)
{
    ni::MachineSettings::setMotion ("vocalize", on);
}

bool Processor::bipolar (int index) const
{
    VzParamInfo info {};
    return vz_param_info (index, &info) && info.bipolar;
}

Formants Processor::formants (double vowel)
{
    Formants f {};
    vz_formants (vowel, plain (param::size), plain (param::resonance), f.data());
    return f;
}

const Picture& Processor::picture()
{
    if (! vz_shell_read (shell.get(), published.get()) || published->serial == latest.serial)
        return latest;
    latest.serial = published->serial;
    latest.spectral = published->mode == 1;
    latest.latency = (int) published->latency;
    latest.sampleRate = published->sample_rate;
    latest.hz.assign (published->hz, published->hz + VZ_POINTS);
    latest.input.assign (published->input_db, published->input_db + VZ_POINTS);
    latest.output.assign (published->output_db, published->output_db + VZ_POINTS);
    return latest;
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

} // namespace ni::vz

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ni::vz::Processor();
}
