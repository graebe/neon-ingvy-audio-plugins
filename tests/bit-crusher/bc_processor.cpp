// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Bit-Crusher's processor on the real engine: what only the shell around
 * it can get wrong.
 *
 *   the parameters  declared from the engine's table: its keys as IDs, its
 *                   names, defaults, travel and words; typing read by the
 *                   engine, junk changing nothing; a value on the resolution
 *                   coming back exactly
 *   the buses       mono or stereo, the output as the input
 *   the sound       both layouts crushed in place; a reactivation lets go of
 *                   what was held; the host's bypass passes audio untouched
 *   the state       saved and reopened, every value exactly
 *   the model       the staircase from the parameters now, the scope after
 *                   audio, the knobs that run both ways
 *   the window      opens at the design's size
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "BitCrusher.h"
#include "Editor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <set>

using ni::bc::Processor;
namespace param = ni::bc::param;

namespace
{
constexpr int block = 512;
constexpr double rate = 48000.0;

struct Instance
{
    Processor p;
    juce::AudioBuffer<float> buffer { 2, block };

    explicit Instance (int channels = 2)
    {
        if (channels == 1)
        {
            auto layout = p.getBusesLayout();
            layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
            layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
            REQUIRE (p.setBusesLayout (layout));
            buffer.setSize (1, block);
        }
        p.prepareToPlay (rate, block);
    }

    void set (int index, double value)
    {
        auto& prm = p.parameter (index);
        prm.setValueNotifyingHost (prm.convertTo0to1 ((float) value));
    }

    /* `blocks` blocks of `level`, as the host runs them. */
    void run (float level, int blocks = 1, bool bypassed = false)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                juce::FloatVectorOperations::fill (buffer.getWritePointer (c), level, block);
            juce::MidiBuffer midi;
            auto& host = static_cast<juce::AudioProcessor&> (p);
            if (bypassed)
                host.processBlockBypassed (buffer, midi);
            else
                host.processBlock (buffer, midi);
        }
    }

    /* A second of a steady level: every glide has landed. */
    void settle (float level) { run (level, (int) rate / block + 1); }
};
} // namespace

TEST_CASE ("the parameters are the engine's table, in its order and words")
{
    Instance a;
    CHECK (a.p.getParameters().size() == Processor::paramCount);
    CHECK (a.p.getBypassParameter() == nullptr);
    std::set<juce::String> ids;
    for (int i = 0; i < Processor::paramCount; ++i)
    {
        auto& prm = a.p.parameter (i);
        CHECK (prm.getParameterIndex() == i);
        ids.insert (prm.getParameterID());
    }
    CHECK (ids.size() == (size_t) Processor::paramCount);
    CHECK (a.p.parameter (param::bits).getParameterID() == "bits");
    CHECK (a.p.parameter (param::rate).getName (32) == "Rate");
    CHECK (a.p.parameter (param::midRise).getName (32) == "Mid-Rise");

    CHECK (a.p.plain (param::bits) == 8.0);
    CHECK (a.p.plain (param::rate) == 12000.0);
    CHECK (a.p.plain (param::mix) == 100.0);
    CHECK (a.p.plain (param::antiAlias) == 0.0);
    CHECK (a.p.parameter (param::rate).getCurrentValueAsText() == "12.0 kHz");
    CHECK (a.p.parameter (param::drive).getCurrentValueAsText() == "0.0 dB");
    CHECK (a.p.parameter (param::bits).getCurrentValueAsText() == "8.0 bits");
}

TEST_CASE ("Rate turns in equal ratios: its geometric middle is the middle of the turn")
{
    Instance a;
    auto& r = a.p.parameter (param::rate);
    CHECK (std::abs (r.convertFrom0to1 (0.5f) - (float) std::sqrt (50.0 * 96000.0)) < 1.0f);
    CHECK (r.convertFrom0to1 (0.0f) == 50.0f);
    CHECK (r.convertFrom0to1 (1.0f) == 96000.0f);
}

TEST_CASE ("typing is the engine's to read, and junk changes nothing")
{
    Instance a;
    auto& r = a.p.parameter (param::rate);
    r.setValueNotifyingHost (r.getValueForText ("8k"));
    CHECK (a.p.plain (param::rate) == 8000.0);
    r.setValueNotifyingHost (r.getValueForText ("very fast"));
    CHECK (a.p.plain (param::rate) == 8000.0);
    r.setValueNotifyingHost (r.getValueForText ("12345.6 Hz"));
    CHECK (a.p.plain (param::rate) == 12345.6);

    auto& b = a.p.parameter (param::bits);
    b.setValueNotifyingHost (b.getValueForText ("5.5"));
    CHECK (a.p.plain (param::bits) == 5.5);
    CHECK (b.getCurrentValueAsText() == "5.5 bits");
}

TEST_CASE ("mono and stereo, the output as the input, and nothing else")
{
    Processor p;
    auto layout = p.getBusesLayout();
    CHECK (p.checkBusesLayoutSupported (layout));
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
    CHECK (! p.checkBusesLayoutSupported (layout));
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    CHECK (p.checkBusesLayoutSupported (layout));
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    CHECK (! p.checkBusesLayoutSupported (layout));
    CHECK (! p.acceptsMidi());
    CHECK (p.getTailLengthSeconds() == 0.0);
}

TEST_CASE ("two bits and no hold put a stereo signal on five levels")
{
    Instance a;
    a.set (param::bits, 2.0);
    a.set (param::rate, 96000.0);
    a.settle (0.3f);
    CHECK (a.buffer.getSample (0, block - 1) == 0.5f);
    CHECK (a.buffer.getSample (1, block - 1) == 0.5f);
}

TEST_CASE ("a mono track is crushed too")
{
    Instance a (1);
    a.set (param::bits, 2.0);
    a.set (param::rate, 96000.0);
    a.settle (-0.3f);
    CHECK (a.buffer.getNumChannels() == 1);
    CHECK (a.buffer.getSample (0, block - 1) == -0.5f);
}

TEST_CASE ("a reactivation lets go of what the hold held")
{
    Instance a;
    a.set (param::rate, 50.0);
    a.settle (0.9f);
    a.p.releaseResources();
    a.p.prepareToPlay (rate, block);
    a.run (0.0f);
    CHECK (a.buffer.getMagnitude (0, block) == 0.0f);
}

TEST_CASE ("the host's bypass passes the audio untouched")
{
    Instance a;
    a.set (param::bits, 1.0);
    a.run (0.3f, 4, true);
    CHECK (a.buffer.getSample (0, 0) == 0.3f);
    CHECK (a.buffer.getSample (1, block - 1) == 0.3f);
}

TEST_CASE ("a saved state reopens with every value exactly")
{
    Instance a;
    a.set (param::bits, 5.5);
    a.set (param::rate, 7000.0);
    a.set (param::shape, 30.0);
    a.set (param::midRise, 1.0);
    a.set (param::postOctave, 0.5);
    a.set (param::output, -2.0);
    juce::MemoryBlock state;
    a.p.getStateInformation (state);

    Instance b;
    b.p.setStateInformation (state.getData(), (int) state.getSize());
    for (int i = 0; i < Processor::paramCount; ++i)
        CHECK_MESSAGE (b.p.plain (i) == a.p.plain (i), a.p.parameter (i).getParameterID());
    CHECK (b.p.plain (param::bits) == 5.5);
    CHECK (b.p.plain (param::rate) == 7000.0);
}

TEST_CASE ("the model: the staircase now, the scope after audio, the bipolar knobs")
{
    Instance a;
    a.set (param::bits, 1.0);
    float out[5] {};
    a.p.steps (out, 5);
    CHECK (out[0] == -1.0f);
    CHECK (out[2] == 0.0f);
    CHECK (out[4] == 1.0f);
    a.set (param::midRise, 1.0);
    a.p.steps (out, 5);
    CHECK (out[2] == 0.5f);

    CHECK (a.p.scope().count() == 0);
    for (int b = 0; b < 20; ++b)
    {
        for (int i = 0; i < block; ++i)
            a.buffer.setSample (0, i, (float) std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * (b * block + i) / rate));
        a.buffer.copyFrom (1, 0, a.buffer, 0, 0, block);
        juce::MidiBuffer midi;
        static_cast<juce::AudioProcessor&> (a.p).processBlock (a.buffer, midi);
    }
    const auto& s = a.p.scope();
    CHECK (s.count() == BC_SCOPE);
    CHECK (s.triggered);
    CHECK (s.sampleRate == rate);
    CHECK (s.ms() > 10.0);

    CHECK (a.p.bipolar (param::drive));
    CHECK (a.p.bipolar (param::postOctave));
    CHECK (! a.p.bipolar (param::bits));
    CHECK (! a.p.bipolar (param::midRise));
    CHECK (! a.p.bipolar (99));
}

TEST_CASE ("the window opens at the design's size")
{
    Instance a;
    std::unique_ptr<juce::AudioProcessorEditor> editor (a.p.createEditorAndMakeActive());
    REQUIRE (editor != nullptr);
    CHECK (editor->getWidth() == ni::bc::Editor::designWidth);
    CHECK (editor->getHeight() == ni::bc::Editor::designHeight);
    a.run (0.2f, 4);
    editor.reset();
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    doctest::Context context (argc, argv);
    return context.run();
}
