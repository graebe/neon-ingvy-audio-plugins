// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Chord-Detector's processor on the real engine: what only the shell
 * around it can get wrong.
 *
 *   the chord     a C major triad played into it is named a chord, from the
 *                 notes it was given
 *   the output    silence, whatever the buffer held
 *   the bypass    it has no bypass parameter of its own, so a host bypasses
 *                 it through processBlockBypassed -- and it still hears the
 *                 lane there: a note released, or started, during the bypass
 *                 is what the engine reads afterwards
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "ChordDetector.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <bitset>
#include <initializer_list>

using ni::chord_detector::ChordDetector;

namespace
{
constexpr int block = 256;

struct Instance
{
    ChordDetector p;
    juce::AudioBuffer<float> buffer { 2, block };

    Instance() { p.prepareToPlay (48000.0, block); }

    static juce::MidiBuffer notes (std::initializer_list<int> keys, bool on)
    {
        juce::MidiBuffer m;
        for (const int k : keys)
            m.addEvent (on ? juce::MidiMessage::noteOn (1, k, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, k), 0);
        return m;
    }

    /* One block, as the host runs it or as it runs it bypassed, over a
     * buffer that is not silent. */
    void run (juce::MidiBuffer midi, bool bypassed = false)
    {
        for (int c = 0; c < buffer.getNumChannels(); ++c)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (c), 0.5f, block);
        auto& host = static_cast<juce::AudioProcessor&> (p);
        if (bypassed)
            host.processBlockBypassed (buffer, midi);
        else
            host.processBlock (buffer, midi);
    }

    bool silent() const { return buffer.getMagnitude (0, block) == 0.0f; }

    bool sounding (std::initializer_list<int> keys)
    {
        const auto& r = p.reading();
        for (const int k : keys)
            if (! r.sounding[(size_t) k])
                return false;
        return r.sounding.count() == keys.size();
    }
};
} // namespace

TEST_CASE ("a triad played into it is named a chord, and its output is silence")
{
    Instance a;
    a.run (Instance::notes ({ 60, 64, 67 }, true));
    CHECK (a.silent());
    CHECK (a.sounding ({ 60, 64, 67 }));
    const auto& r = a.p.reading();
    CHECK (r.kind == 3);
    CHECK (r.root == 0);
    CHECK (r.name.isNotEmpty());
}

TEST_CASE ("it has no bypass parameter: a host bypasses it through processBlockBypassed")
{
    Instance a;
    CHECK (a.p.getBypassParameter() == nullptr);
}

TEST_CASE ("bypassed, it still hears the lane: a note-off then is not lost")
{
    Instance a;
    a.run (Instance::notes ({ 60, 64, 67 }, true));
    REQUIRE (a.sounding ({ 60, 64, 67 }));

    a.run (Instance::notes ({ 60, 64, 67 }, false), true);
    CHECK (a.silent());
    CHECK (a.sounding ({}));
    CHECK (a.p.reading().kind == 0);

    /* And back in: nothing sounds on that the lane released. */
    a.run ({});
    CHECK (a.sounding ({}));
}

TEST_CASE ("bypassed, a note started then is heard as well")
{
    Instance a;
    a.run (Instance::notes ({ 62 }, true), true);
    CHECK (a.silent());
    CHECK (a.sounding ({ 62 }));
    CHECK (a.p.reading().kind == 1);
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    doctest::Context context (argc, argv);
    return context.run();
}
