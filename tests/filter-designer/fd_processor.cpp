// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Filter Designer's processor on the real engine: what only the shell
 * around it can get wrong.
 *
 *   the parameters  declared from the engine's table: its keys as IDs, its
 *                   names, ranges, defaults and words; typing read by the
 *                   engine; the analyzer's mode a choice no host automates
 *   the buses       mono or stereo, the output as the input
 *   the sound       a new instance is the input bit for bit, stereo and
 *                   mono; a Bell is heard at its gain; a kill takes its band
 *                   out; the host's bypass passes the audio
 *   the state       saved and reopened, every value exactly
 *   the model       the picture designed from the host's values, the
 *                   handles and the edges the engine's; what a type uses
 *   the window      opens at the design's size; the inspector binds to the
 *                   selected filter, or the isolator, and lights what the
 *                   type uses
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "Editor.h"
#include "FilterDesigner.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <set>

using ni::fd::Processor;
namespace param = ni::fd::param;
namespace type = ni::fd::type;

namespace
{
constexpr int block = 512;
constexpr double rate = 48000.0;

struct Instance
{
    Processor p;
    juce::AudioBuffer<float> buffer { 2, block };
    juce::int64 n = 0;

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

    /* `blocks` blocks of a sine at `hz` (0 for noise), returning the last
     * block's level in dB against the input's. */
    double run (double hz, int blocks = 1, bool bypassed = false)
    {
        double in = 0.0, out = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            juce::AudioBuffer<float> dry (buffer.getNumChannels(), block);
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                for (int i = 0; i < block; ++i)
                {
                    const auto t = (double) (n + i);
                    const float x = hz > 0.0 ? (float) (0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * hz * t / rate))
                                             : (float) (0.25 * std::sin (t * 0.37 + c) * std::cos (t * 0.011));
                    buffer.setSample (c, i, x);
                    dry.setSample (c, i, x);
                }
            juce::MidiBuffer midi;
            auto& host = static_cast<juce::AudioProcessor&> (p);
            if (bypassed)
                host.processBlockBypassed (buffer, midi);
            else
                host.processBlock (buffer, midi);
            n += block;
            if (b == blocks - 1)
            {
                in = dry.getRMSLevel (0, 0, block);
                out = buffer.getRMSLevel (0, 0, block);
            }
        }
        return 20.0 * std::log10 (out / in);
    }
};
} // namespace

TEST_CASE ("the parameters are the engine's table")
{
    Processor p;
    /* The engine's 174, then the shell's Bypass. */
    REQUIRE (p.getParameters().size() == param::count + 1);
    std::set<juce::String> ids;
    for (auto* each : p.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (each))
            ids.insert (withId->getParameterID());
    CHECK (ids.size() == (size_t) param::count + 1);
    CHECK (p.parameter (param::node (2, param::freq)).getParameterID() == "n03_freq");
    CHECK (p.parameter (param::node (2, param::freq)).getName (64) == "Filter 3 Freq");
    CHECK (p.parameter (param::isoLow).getParameterID() == "iso_low");
    auto* t = dynamic_cast<juce::AudioParameterChoice*> (&p.parameter (param::node (0, param::type)));
    REQUIRE (t != nullptr);
    CHECK (t->choices.size() == 10);
    CHECK (t->choices[7] == "Notch");
    CHECK (t->choices[4] == "High-Pass");
    CHECK (t->choices[5] == "Low-Pass");
    CHECK (! p.parameter (param::analyzer).isAutomatable());
    CHECK (p.parameter (param::node (0, param::gain)).isAutomatable());
    auto& f = p.parameter (param::node (0, param::freq));
    f.setValueNotifyingHost (f.getValueForText ("2.5k"));
    CHECK (p.plain (param::node (0, param::freq)) == doctest::Approx (2500.0));
    CHECK (f.getCurrentValueAsText() == "2.50 kHz");
    const float before = f.getValue();
    f.setValueNotifyingHost (f.getValueForText ("loud"));
    CHECK (f.getValue() == before);
    CHECK (p.parameter (param::isoMid).getText (p.parameter (param::isoMid).convertTo0to1 (-60.0f), 64) == "-inf dB");
    CHECK (p.bipolar (param::output));
    CHECK (! p.bipolar (param::node (0, param::freq)));
}

TEST_CASE ("mono or stereo, the output as the input")
{
    Processor p;
    auto layout = p.getBusesLayout();
    CHECK (p.checkBusesLayoutSupported (layout));
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    CHECK (p.checkBusesLayoutSupported (layout));
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    CHECK (! p.checkBusesLayoutSupported (layout));
    CHECK (p.getLatencySamples() == 0);
}

TEST_CASE ("a new instance is the input, bit for bit")
{
    for (const int channels : { 2, 1 })
    {
        Instance x (channels);
        juce::AudioBuffer<float> before (channels, block);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < block; ++i)
                x.buffer.setSample (c, i, std::sin (0.1f * (float) i + (float) c) * 0.6f);
        before.makeCopyOf (x.buffer);
        juce::MidiBuffer midi;
        static_cast<juce::AudioProcessor&> (x.p).processBlock (x.buffer, midi);
        for (int c = 0; c < channels; ++c)
            CHECK (std::memcmp (x.buffer.getReadPointer (c), before.getReadPointer (c), sizeof (float) * block) == 0);
    }
}

TEST_CASE ("a bell is heard at its gain, a kill takes its band out, the bypass passes")
{
    Instance x;
    x.set (param::node (0, param::type), type::bell);
    x.set (param::node (0, param::freq), 1000.0);
    x.set (param::node (0, param::gain), 6.0);
    CHECK (x.run (1000.0, 60) == doctest::Approx (6.0).epsilon (0.01));
    /* Whole cycles in a block (48000 / 512 = 93.75 Hz), so the level is
     * the RMS of whole periods; a decade below the bell, its skirt. */
    CHECK (std::abs (x.run (93.75, 60)) < 0.15);
    CHECK (std::abs (x.run (1000.0, 60, true)) < 0.001);

    Instance iso;
    iso.set (param::isoOn, 1.0);
    iso.set (param::isoKillHigh, 1.0);
    CHECK (iso.run (12000.0, 60) < -30.0);
    CHECK (std::abs (iso.run (93.75, 60)) < 0.01);
}

TEST_CASE ("the Bypass fades, and the filters come back from the music they heard")
{
    Instance x;
    REQUIRE (x.p.getBypassParameter() == &x.p.bypassParameter());
    CHECK (x.p.getBypassParameter()->getName (64) == "Bypass");
    x.set (param::node (0, param::type), type::bell);
    x.set (param::node (0, param::freq), 100.0);
    x.set (param::node (0, param::gain), 12.0);
    x.set (param::node (0, param::q), 4.0);
    /* A 100 Hz sine at -6 dBFS, the Bypass switched every 0.5 s. */
    const auto step = [&x] (int blocks)
    {
        float largest = 0.0f, last = 0.0f;
        bool first = true;
        for (int b = 0; b < blocks; ++b)
        {
            x.run (100.0);
            for (int i = 0; i < block; ++i)
            {
                const float v = x.buffer.getSample (0, i);
                if (! first)
                    largest = std::max (largest, std::abs (v - last));
                last = v;
                first = false;
            }
        }
        return largest;
    };
    step (60);
    float worst = 0.0f;
    for (int k = 0; k < 6; ++k)
    {
        x.p.bypassParameter().setValueNotifyingHost (k % 2 == 0 ? 1.0f : 0.0f);
        worst = std::max (worst, step (47));
    }
    /* The sine's own largest step, at its loudest (+12 dB): 0.5 x 4 x w. */
    const float own = (float) (0.25 * 3.981 * 2.0 * juce::MathConstants<double>::pi * 100.0 / rate);
    CHECK (worst < own * 1.25f);
}

TEST_CASE ("the tail is the engine's: how long the filters ring on")
{
    Instance x;
    CHECK (x.p.getTailLengthSeconds() == 0.0);
    x.set (param::node (0, param::type), type::notch);
    x.set (param::node (0, param::order), 0.0);
    x.set (param::node (0, param::freq), 20.0);
    x.set (param::node (0, param::q), 40.0);
    const double tail = x.p.getTailLengthSeconds();
    /* One pole pair at Q 40: 60 dB in ln(1000)·2Q/ω0, about 4.4 s. */
    CHECK (tail == doctest::Approx (4.4).epsilon (0.05));
    x.set (param::node (0, param::on), 0.0);
    CHECK (x.p.getTailLengthSeconds() == 0.0);
}

TEST_CASE ("the analyzer's rings are kept only while a window shows the analyzer")
{
    Instance x;
    x.p.editorOpened();
    x.run (1000.0, 20);
    CHECK (x.p.picture (ni::fd::lane::left).analyzer);
    x.set (param::analyzer, 0.0);
    x.p.picture (ni::fd::lane::left);
    x.run (1000.0, 20);
    CHECK (! x.p.picture (ni::fd::lane::left).analyzer);
    x.p.editorClosed();
}

TEST_CASE ("the state comes back exactly")
{
    Instance a;
    a.set (param::node (4, param::type), type::notch);
    a.set (param::node (4, param::family), 4.0);
    a.set (param::node (4, param::freq), 3150.0);
    a.set (param::node (4, param::q), 6.3);
    a.set (param::isoOn, 1.0);
    a.set (param::output, -3.5);
    a.p.bypassParameter().setValueNotifyingHost (1.0f);
    juce::MemoryBlock saved;
    a.p.getStateInformation (saved);
    Instance b;
    b.p.setStateInformation (saved.getData(), (int) saved.getSize());
    for (int i = 0; i < param::count; ++i)
        CHECK (b.p.plain (i) == a.p.plain (i));
    CHECK (b.p.bypassParameter().getValue() == 1.0f);
}

TEST_CASE ("the model's picture is the engine's design of the host's values")
{
    Instance x;
    x.set (param::node (0, param::type), type::bell);
    x.set (param::node (0, param::freq), 1000.0);
    x.set (param::node (0, param::gain), 9.0);
    x.set (param::node (1, param::type), type::highPass);
    x.set (param::node (1, param::freq), 80.0);
    const auto& pic = x.p.picture (ni::fd::lane::left);
    CHECK (pic.handleHz[0] == doctest::Approx (1000.0));
    CHECK (pic.handleDb[0] == doctest::Approx (9.0));
    CHECK (pic.handleDb[1] == doctest::Approx (-3.0103).epsilon (0.001));
    CHECK (std::isnan (pic.handleHz[2]));
    CHECK (pic.edgeHz[0][0] < 1000.0);
    CHECK (pic.edgeHz[0][1] > 1000.0);
    const float peak = *std::max_element (pic.total.begin(), pic.total.end());
    CHECK (peak == doctest::Approx (9.0).epsilon (0.01));
    CHECK (x.p.typeUses (type::bell, 1) == (ni::fd::uses::gain | ni::fd::uses::q));
    CHECK (x.p.orderPlayed (type::highShelf, 0, 7) == 3);
    CHECK (x.p.orderPlayed (type::lowPass, 5, 2) == 3);
    CHECK (x.p.familyUsesRipple (2));
    CHECK (x.p.qForEdge (1000.0, pic.edgeHz[0][1]) == doctest::Approx (0.7071).epsilon (0.001));
    CHECK (x.p.typeForNew (20.0) == type::highPass);
}

TEST_CASE ("the window: its size, and the inspector bound to the selection")
{
    Instance x;
    x.set (param::node (0, param::type), type::highPass);
    ni::fd::Editor e (x.p);
    CHECK (e.getWidth() == 888);
    CHECK (e.getHeight() == 708);
    e.tick (0.0);
    /* A cut: Family and Order lit, Gain not. */
    REQUIRE (e.knobFor (param::node (0, param::gain)) != nullptr);
    CHECK (! e.knobFor (param::node (0, param::gain))->isEnabled());
    CHECK (e.selectFor (param::node (0, param::family))->isEnabled());
    CHECK (e.knobFor (param::node (0, param::q))->isEnabled());
    CHECK (! e.knobFor (param::node (0, param::ripple))->isEnabled());
    /* Chebyshev I: Ripple lit. */
    x.set (param::node (0, param::family), 2.0);
    e.tick (1.0);
    CHECK (e.knobFor (param::node (0, param::ripple))->isEnabled());
    /* Another slot, the isolator. */
    e.select (5);
    CHECK (e.knobFor (param::node (5, param::freq)) != nullptr);
    CHECK (e.knobFor (param::node (0, param::freq)) == nullptr);
    e.select (ni::fd::ResponseWell::isolator);
    CHECK (e.knobFor (param::isoLow) != nullptr);
    CHECK (e.toggleFor (param::isoKillMid) != nullptr);
    /* The well's handle for slot 1 sits where the engine says. */
    e.select (0);
    e.tick (2.0);
    const auto h = e.well().handle (0);
    REQUIRE (h.has_value());
    CHECK (h->x == doctest::Approx (e.well().xOf (x.p.plain (param::node (0, param::freq)))));
    CHECK (! e.well().handle (3).has_value());
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    doctest::Context context;
    context.applyCommandLine (argc, argv);
    return context.run();
}
