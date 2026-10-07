// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

// NI Cross Smoke: the smallest JUCE plugin that still exercises what the real
// ones depend on -- a parameter, saved state, an editor, and an audio callback
// that hands every channel to the Rust engine (rust/src/lib.rs) through its C
// ABI. It ships nowhere; it is what scripts/build-*.sh build and validate.

#include <juce_audio_processors/juce_audio_processors.h>

#include "ni_smoke.h"

namespace
{
juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    return { std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "gain", 1 }, "Gain",
                                                          juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f) };
}
} // namespace

class SmokeProcessor final : public juce::AudioProcessor
{
public:
    SmokeProcessor()
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo())
                                           .withOutput ("Output", juce::AudioChannelSet::stereo()))
    {
    }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto& out = layouts.getMainOutputChannelSet();
        return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
            && out == layouts.getMainInputChannelSet();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
            buffer.clear (channel, 0, buffer.getNumSamples());

        const auto gain = gain_->load();
        for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
            ni_smoke_apply_gain (buffer.getWritePointer (channel), static_cast<size_t> (buffer.getNumSamples()), gain);
    }

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }

    void getStateInformation (juce::MemoryBlock& destination) override
    {
        if (const auto xml = state_.copyState().createXml())
            copyXmlToBinary (*xml, destination);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (const auto xml = getXmlFromBinary (data, size); xml != nullptr && xml->hasTagName (state_.state.getType()))
            state_.replaceState (juce::ValueTree::fromXml (*xml));
    }

private:
    juce::AudioProcessorValueTreeState state_ { *this, nullptr, "NICrossSmoke", createLayout() };
    std::atomic<float>* gain_ = state_.getRawParameterValue ("gain");

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmokeProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SmokeProcessor();
}
